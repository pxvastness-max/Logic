/**
 * @file   compression_service.c
 * @brief  DTFH采集文件的两遍压缩、校验与流式加载
 */

/* ================================================== */

#include "compression/compression_service.h"
#include "compression/dtfh_codec.h"
#include "compression/dtfh_format_internal.h"
#include "filesystem/fatfs_port.h"
#include "log/log_service.h"
#include "memory/memory_placement.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "stddef.h"
#include "string.h"

/* ========================= 配置 ========================= */

#define COMPRESSION_READ_COUNT 512U
#define COMPRESSION_IO_BYTES   4096U

#if (LA_SERVICE_CHANNEL_COUNT != 6U) || (DTFH_CODEC_TABLE_COUNT != 5U)
    #error "DTFH 2.0 requires six channels and five byte-position tables"
#endif

/* ================================================== */

typedef struct
{
    Compression_Mode_t mode; /* 本通道的实际无损表示 */
    uint32_t count;          /* 解压后的绝对时间戳数量 */
    uint32_t first_tick;     /* 首个绝对 CNT，空通道为 0 */
    uint32_t last_tick;      /* 最后一个绝对 CNT，空通道为 0 */
    uint64_t offset;         /* 载荷在文件中的绝对字节偏移 */
    uint64_t bytes;          /* 载荷字节数，Huffman 最后一个字节可含填充位 */
    uint64_t bits;           /* 有效载荷位数，不含最后的零填充 */
    uint32_t raw_crc;        /* 解压后小端 uint32_t 时间戳流的 CRC */
    uint32_t payload_crc;    /* 文件中实际载荷字节的 CRC，包含填充字节 */
} Compression_Directory_t;

typedef struct
{
    uint64_t remaining;          /* 尚未从文件读取的本通道载荷字节数 */
    uint64_t bits;               /* 已消费或产生的有效位数量 */
    uint64_t bit_limit;          /* 目录给定的有效位上限，避免跨入填充位 */
    uint32_t crc;                /* 载荷字节的增量 CRC 累计值 */
    uint16_t count;              /* 当前 I/O 缓冲有效字节数量 */
    uint16_t index;              /* 当前 I/O 缓冲下一个读取位置 */
    uint8_t bit_byte;            /* 当前尚未读完或写满的位缓冲字节 */
    uint8_t bit_count;           /* 位缓冲中尚未消费或已积累的位数 */
    Compression_Result_t result; /* 位回调无法直接返回的首个流错误 */
} Compression_Stream_t;

typedef struct
{
    /* 六路按差值字节位置统计的频率矩阵 */
    uint32_t frequency[6][DTFH_CODEC_TABLE_COUNT] [DTFH_CODEC_SYMBOL_COUNT];
    uint32_t combined[DTFH_CODEC_SYMBOL_COUNT];               /* 当前码表位置的跨通道频率合并暂存 */
    DTFH_Codec_Huffman_Table_t table[DTFH_CODEC_TABLE_COUNT]; /* 整次文件操作共享的五张规范码表 */
    DTFH_Codec_Huffman_Builder_t builder;                     /* 可复用 Huffman 构造临时节点区 */
    uint32_t timestamps[COMPRESSION_READ_COUNT]; /* 短查询复制或解码导入的单批时间戳缓冲 */
    uint8_t io[COMPRESSION_IO_BYTES];            /* 文件字节 I/O 缓冲，与 LA 查询锁生命周期分离 */
    uint8_t header[DTFH_MAX_HEADER_BYTES];       /* 要写入文件的头、目录和共享编码表字节 */
    char temporary_path[COMPRESSION_PATH_CAPACITY]; /* 目标路径追加 .tmp 后的独立副本 */
    Compression_Directory_t directory[6];           /* 六路已验证的载荷位置与校验描述 */
    Compression_File_Info_t report;                 /* 本次操作累计报告，End 复制给调用者 */
    FIL file;              /* 模块独占的唯一文件句柄，Close 失败时保留原对象供重试 */
    uint32_t header_bytes; /* 本次实际头部总字节数 */
    uint32_t flags;        /* 完整文件、Huffman 与稀疏码表标志 */
    uint32_t metadata_crc; /* 固定头、目录与码表整体的校验值 */
} Compression_Work_t;

/* ================================================== */

/* 约50KiB，互斥保护，不占任务栈 */
PLATFORM_AXI_SRAM_ZI static Compression_Work_t compression_work;
static SemaphoreHandle_t compression_mutex = NULL;

static uint8_t compression_file_open = 0U; /* Close失败时保留FIL，下一次操作先重试 */

/* ================================================== */

static void Compression_Put_U32(uint8_t *data, uint32_t value)
{
    for (uint8_t i = 0U; i < 4U; i++)
    {
        data[i] = (uint8_t)(value >> (8U * i));
    }
}

static void Compression_Put_U64(uint8_t *data, uint64_t value)
{
    for (uint8_t i = 0U; i < 8U; i++)
    {
        data[i] = (uint8_t)(value >> (8U * i));
    }
}

static uint32_t Compression_Get_U32(const uint8_t *data)
{
    uint32_t value = 0U;

    for (uint8_t i = 0U; i < 4U; i++)
    {
        value |= (uint32_t)data[i] << (8U * i);
    }
    return value;
}

static uint64_t Compression_Get_U64(const uint8_t *data)
{
    uint64_t value = 0U;

    for (uint8_t i = 0U; i < 8U; i++)
    {
        value |= (uint64_t)data[i] << (8U * i);
    }
    return value;
}

static uint32_t Compression_Timestamp_CRC(uint32_t crc, uint32_t timestamp)
{
    uint8_t bytes[4];

    Compression_Put_U32(bytes, timestamp);
    return DTFH_Codec_CRC32_Update(crc, bytes, sizeof(bytes));
}

/**
 * @brief  检查文件和 LA 服务共用的来源信息；这里不按 cutoff 裁剪
 */
static Compression_Result_t Compression_Validate_Capture(
                                                const LA_Service_Info_t *capture)
{
    LA_Service_Result_t result = LA_Service_Import_Validate(capture);

    compression_work.report.la_result = result;
    if (result == LA_SERVICE_ERROR_STORAGE)
    {
        return COMPRESSION_ERROR_CAPACITY;
    }
    return (result == LA_SERVICE_OK) ? COMPRESSION_OK : COMPRESSION_ERROR_FORMAT;
}

static Compression_Result_t Compression_File_Error(FRESULT result)
{
    compression_work.report.filesystem_result = result;
    return COMPRESSION_ERROR_FILESYSTEM;
}

static Compression_Result_t Compression_LA_Error(LA_Service_Result_t result)
{
    compression_work.report.la_result = result;
    return (result == LA_SERVICE_ERROR_BUSY) ? COMPRESSION_ERROR_BUSY :
           COMPRESSION_ERROR_LA_SERVICE;
}

static Compression_Result_t Compression_Read(void *data, uint32_t count)
{
    UINT received = 0U;
    FRESULT result = f_read(&compression_work.file, data, (UINT)count, &received);

    if (result != FR_OK)
    {
        return Compression_File_Error(result);
    }
    return (received == count) ? COMPRESSION_OK : Compression_File_Error(FR_DISK_ERR);
}

static Compression_Result_t Compression_Write(const void *data, uint32_t count)
{
    UINT written = 0U;
    FRESULT result = f_write(&compression_work.file, data, (UINT)count, &written);

    if (result != FR_OK)
    {
        (void)Log_Printf("file: f_write failed fs=%u requested=%lu written=%u\r\n",
                         (unsigned int)result,
                         (unsigned long)count,
                         (unsigned int)written);
        return Compression_File_Error(result);
    }
    if (written != count)
    {
        (void)Log_Printf("file: f_write short requested=%lu written=%u\r\n",
                         (unsigned long)count,
                         (unsigned int)written);
    }
    return (written == count) ? COMPRESSION_OK : Compression_File_Error(FR_DISK_ERR);
}

static Compression_Result_t Compression_Seek(uint64_t offset)
{
    FRESULT result;

    if ((uint64_t)(FSIZE_t)offset != offset)
    {
        return COMPRESSION_ERROR_FORMAT;
    }
    result = f_lseek(&compression_work.file, (FSIZE_t)offset);
    if (result != FR_OK)
    {
        (void)Log_Printf("file: f_lseek failed fs=%u offset=%lu\r\n",
                         (unsigned int)result,
                         (unsigned long)offset);
        return Compression_File_Error(result);
    }
    if ((uint64_t)f_tell(&compression_work.file) != offset)
    {
        (void)Log_Printf("file: f_lseek mismatch offset=%lu tell=%lu\r\n",
                         (unsigned long)offset,
                         (unsigned long)f_tell(&compression_work.file));
    }
    return ((uint64_t)f_tell(&compression_work.file) == offset) ?
               COMPRESSION_OK :
               Compression_File_Error(FR_DISK_ERR);
}

/**
 * @brief  优先返回最早发生的错误，关闭文件时的错误不会覆盖先前的校验错误
 */
static Compression_Result_t Compression_Close(Compression_Result_t result)
{
    FRESULT close_result = f_close(&compression_work.file);

    if ((close_result == FR_OK) || (close_result == FR_INVALID_OBJECT))
    {
        compression_file_open = 0U;
    }
    if (close_result != FR_OK)
    {
        (void)Log_Printf("file: f_close failed fs=%u\r\n", (unsigned int)close_result);
        /* FatFs关闭失败可能仍持有文件锁 */
        if (compression_work.report.filesystem_result == FR_OK)
        {
            compression_work.report.filesystem_result = close_result;
        }
        if (result == COMPRESSION_OK)
        {
            return COMPRESSION_ERROR_FILESYSTEM;
        }
    }
    return result;
}

/**
 * @brief  检查是否在任务中调用以及文件路径；工作区被占用时立即返回
 * @note   上次 Close 失败时先重试原 FIL，关闭再次失败即释放锁并返回
 */
static Compression_Result_t Compression_Begin(const char *path,
                                              Compression_File_Info_t *info)
{
    size_t length = 0U;

    if (info != NULL)
    {
        memset(info, 0, sizeof(*info));
    }
    if ((path == NULL) || (FileSystem_Port_Is_Task_Context() == 0U))
    {
        return COMPRESSION_ERROR_INVALID_PARAMETER;
    }
    while ((length < COMPRESSION_PATH_CAPACITY) && (path[length] != '\0'))
    {
        length++;
    }
    if ((length == 0U) || (length + sizeof(".tmp") > COMPRESSION_PATH_CAPACITY))
    {
        return COMPRESSION_ERROR_INVALID_PARAMETER;
    }
    if (compression_mutex == NULL)
    {
        return COMPRESSION_ERROR_NOT_READY;
    }
    if (xSemaphoreTake(compression_mutex, 0U) != pdTRUE)
    {
        return COMPRESSION_ERROR_BUSY;
    }
    memset(&compression_work.report, 0, sizeof(compression_work.report));
    if (compression_file_open != 0U)
    {
        Compression_Result_t close_result = Compression_Close(COMPRESSION_OK);

        if (close_result != COMPRESSION_OK)
        {
            if (info != NULL)
            {
                *info = compression_work.report;
            }
            (void)xSemaphoreGive(compression_mutex);
            return close_result;
        }
    }
    memset(compression_work.directory, 0, sizeof(compression_work.directory));
    memcpy(compression_work.temporary_path, path, length);
    memcpy(compression_work.temporary_path + length, ".tmp", sizeof(".tmp"));
    return COMPRESSION_OK;
}

/**
 * @brief  复制结果，然后释放工作区锁
 * @note   调用前须关闭文件并结束 LA 数据保留或导入；本函数不会自动关闭文件
 */
static Compression_Result_t Compression_End(Compression_Result_t result,
                                            Compression_File_Info_t *info)
{
    if (info != NULL)
    {
        *info = compression_work.report;
    }
    (void)xSemaphoreGive(compression_mutex);
    return result;
}

/* ================================================== */

static void Compression_Stream_Init(Compression_Stream_t *stream,
                                     const Compression_Directory_t *directory)
{
    memset(stream, 0, sizeof(*stream));
    stream->crc = DTFH_CODEC_CRC32_INIT;
    stream->remaining = directory->bytes;
    stream->bit_limit = directory->bits;
    stream->result = COMPRESSION_OK;
}

static uint8_t Compression_Stream_Flush(Compression_Stream_t *stream)
{
    if (stream->result != COMPRESSION_OK)
    {
        return 0U;
    }
    if (stream->count != 0U)
    {
        stream->result = Compression_Write(compression_work.io, stream->count);
        if (stream->result != COMPRESSION_OK)
        {
            return 0U;
        }
        stream->crc = DTFH_Codec_CRC32_Update(stream->crc, compression_work.io,
                                             stream->count);
        stream->count = 0U;
    }
    return 1U;
}

static uint8_t Compression_Stream_Write_Byte(Compression_Stream_t *stream,
                                            uint8_t byte)
{
    if ((stream->result != COMPRESSION_OK) ||
        ((stream->count == COMPRESSION_IO_BYTES) &&
         (Compression_Stream_Flush(stream) == 0U)))
    {
        return 0U;
    }
    compression_work.io[stream->count++] = byte;
    return 1U;
}

static uint8_t Compression_Stream_Write_Bit(void *context, uint8_t bit)
{
    Compression_Stream_t *stream = context;

    stream->bit_byte = (uint8_t)((stream->bit_byte << 1U) | bit);
    stream->bit_count++;
    stream->bits++;
    if (stream->bit_count == 8U)
    {
        uint8_t byte = stream->bit_byte;

        stream->bit_count = 0U;
        stream->bit_byte = 0U;
        return Compression_Stream_Write_Byte(stream, byte);
    }
    return 1U;
}

static uint8_t Compression_Stream_Read_Byte(Compression_Stream_t *stream,
                                           uint8_t *byte)
{
    if (stream->result != COMPRESSION_OK)
    {
        return 0U;
    }
    if (stream->index == stream->count)
    {
        uint32_t count;

        if (stream->remaining == 0U)
        {
            stream->result = COMPRESSION_ERROR_FORMAT;
            return 0U;
        }
        count = (stream->remaining > COMPRESSION_IO_BYTES) ?
                COMPRESSION_IO_BYTES : (uint32_t)stream->remaining;
        stream->result = Compression_Read(compression_work.io, count);
        if (stream->result != COMPRESSION_OK)
        {
            return 0U;
        }
        stream->crc = DTFH_Codec_CRC32_Update(stream->crc, compression_work.io, count);
        stream->remaining -= count;
        stream->count = (uint16_t)count;
        stream->index = 0U;
    }
    *byte = compression_work.io[stream->index++];
    return 1U;
}

static uint8_t Compression_Stream_Read_Bit(void *context, uint8_t *bit)
{
    Compression_Stream_t *stream = context;

    if (stream->bits >= stream->bit_limit)
    {
        stream->result = COMPRESSION_ERROR_FORMAT;
        return 0U;
    }
    if (stream->bit_count == 0U)
    {
        if (Compression_Stream_Read_Byte(stream, &stream->bit_byte) == 0U)
        {
            return 0U;
        }
        stream->bit_count = 8U;
    }
    stream->bit_count--;
    *bit = (uint8_t)((stream->bit_byte >> stream->bit_count) & 1U);
    stream->bits++;
    return 1U;
}

/* ================================================== */

static Compression_Result_t Compression_Get_Data(uint8_t channel,
                                                 uint32_t index,
                                                 uint16_t requested)
{
    uint16_t received = 0U;
    LA_Service_Result_t result = LA_Service_Get_Data(
            (LA_Service_Channel_t)channel, index, compression_work.timestamps,
            requested, &received);

    if (result != LA_SERVICE_OK)
    {
        return Compression_LA_Error(result);
    }
    if (received != requested)
    {
        return Compression_LA_Error(LA_SERVICE_ERROR_STORAGE);
    }
    return COMPRESSION_OK;
}

/**
 * @brief  第一次读取已完成的采集，统计差值各字节位置的出现次数
 * @note   调用者已保留本次采集数据；每次短暂查询只复制时间戳
 */
static Compression_Result_t Compression_Scan(void)
{
    memset(compression_work.frequency, 0, sizeof(compression_work.frequency));
    for (uint8_t channel = 0U; channel < LA_SERVICE_CHANNEL_COUNT; channel++)
    {
        Compression_Directory_t *directory = &compression_work.directory[channel];
        uint32_t index = 0U;
        uint32_t previous = 0U;
        uint32_t crc = DTFH_CODEC_CRC32_INIT;

        directory->count = compression_work.report.capture.data_count[channel];
        compression_work.report.original_bytes += (uint64_t)directory->count * 4U;
        while (index < directory->count)
        {
            uint16_t count = (directory->count - index > COMPRESSION_READ_COUNT) ?
                    COMPRESSION_READ_COUNT : (uint16_t)(directory->count - index);
            Compression_Result_t result = Compression_Get_Data(channel, index, count);

            if (result != COMPRESSION_OK)
            {
                return result;
            }
            for (uint16_t i = 0U; i < count; i++)
            {
                uint8_t bytes[DTFH_CODEC_ULEB128_MAX_BYTES];
                uint8_t length;
                uint32_t timestamp = compression_work.timestamps[i];

                if (timestamp < previous)
                {
                    return COMPRESSION_ERROR_FORMAT;
                }
                if ((index == 0U) && (i == 0U))
                {
                    directory->first_tick = timestamp;
                }
                length = DTFH_Codec_ULEB128_Encode(timestamp - previous, bytes);
                for (uint8_t position = 0U; position < length; position++)
                {
                    compression_work.frequency[channel][position][bytes[position]]++;
                }
                previous = timestamp;
                crc = Compression_Timestamp_CRC(crc, timestamp);
            }
            index += count;
        }
        directory->last_tick = previous;
        directory->raw_crc = DTFH_Codec_CRC32_Finish(crc);
    }
    return COMPRESSION_OK;
}

static Compression_Mode_t Compression_Fallback_Mode(uint8_t channel,
                                                    uint64_t *bytes)
{
    uint64_t uleb_bytes = 0U;
    uint64_t raw_bytes = (uint64_t)compression_work.directory[channel].count * 4U;

    for (uint8_t position = 0U; position < DTFH_CODEC_TABLE_COUNT; position++)
    {
        for (uint16_t symbol = 0U; symbol < DTFH_CODEC_SYMBOL_COUNT; symbol++)
        {
            uleb_bytes += compression_work.frequency[channel][position][symbol];
        }
    }
    *bytes = (uleb_bytes < raw_bytes) ? uleb_bytes : raw_bytes;
    return (uleb_bytes < raw_bytes) ? COMPRESSION_MODE_DELTA_ULEB128 :
                                    COMPRESSION_MODE_RAW_U32;
}

/**
 * @brief  比较原始数据和压缩数据的实际大小，为每路选择更省空间的格式
 */
static Compression_Result_t Compression_Select_Modes(void)
{
    uint64_t saving = 0U;
    uint64_t offset;
    uint16_t codebook_bytes;
    uint8_t sparse;

    for (uint8_t position = 0U; position < DTFH_CODEC_TABLE_COUNT; position++)
    {
        memset(compression_work.combined, 0, sizeof(compression_work.combined));
        for (uint8_t channel = 0U; channel < LA_SERVICE_CHANNEL_COUNT; channel++)
        {
            for (uint16_t symbol = 0U; symbol < DTFH_CODEC_SYMBOL_COUNT; symbol++)
            {
                compression_work.combined[symbol] +=
                        compression_work.frequency[channel][position][symbol];
            }
        }
        if (DTFH_Codec_Huffman_Build(compression_work.combined,
                                     &compression_work.table[position],
                                     &compression_work.builder) != DTFH_CODEC_OK)
        {
            return COMPRESSION_ERROR_FORMAT;
        }
    }
    for (uint8_t channel = 0U; channel < LA_SERVICE_CHANNEL_COUNT; channel++)
    {
        Compression_Directory_t *directory = &compression_work.directory[channel];
        uint64_t bits = 0U;

        if (directory->count == 0U)
        {
            directory->mode = COMPRESSION_MODE_EMPTY;
            continue;
        }
        directory->mode = Compression_Fallback_Mode(channel, &directory->bytes);
        directory->bits = directory->bytes * 8U;
        for (uint8_t position = 0U; position < DTFH_CODEC_TABLE_COUNT; position++)
        {
            for (uint16_t symbol = 0U; symbol < DTFH_CODEC_SYMBOL_COUNT; symbol++)
            {
                bits += (uint64_t)compression_work.frequency[channel][position][symbol] *
                        compression_work.table[position].length[symbol];
            }
        }
        if ((bits + 7U) / 8U < directory->bytes)
        {
            saving += directory->bytes - (bits + 7U) / 8U;
            directory->mode = COMPRESSION_MODE_DELTA_HUFFMAN;
            directory->bits = bits;
            directory->bytes = (bits + 7U) / 8U;
        }
    }
    compression_work.flags = DTFH_FLAG_COMPLETE;
    compression_work.header_bytes = DTFH_BASE_HEADER_BYTES;
    codebook_bytes = DTFH_Codec_Codebook_Select(compression_work.table, &sparse);
    if (saving > codebook_bytes)
    {
        compression_work.flags |= DTFH_FLAG_HUFFMAN;
        if (sparse != 0U)
        {
            compression_work.flags |= DTFH_FLAG_SPARSE_CODEBOOK;
        }
        compression_work.header_bytes += codebook_bytes;
        compression_work.report.codebook = (sparse != 0U) ?
                    COMPRESSION_CODEBOOK_SPARSE : COMPRESSION_CODEBOOK_DENSE;
        compression_work.report.codebook_bytes = codebook_bytes;
    }
    else
    {
        for (uint8_t channel = 0U; channel < LA_SERVICE_CHANNEL_COUNT; channel++)
        {
            Compression_Directory_t *directory = &compression_work.directory[channel];

            if (directory->mode == COMPRESSION_MODE_DELTA_HUFFMAN)
            {
                directory->mode = Compression_Fallback_Mode(channel, &directory->bytes);
                directory->bits = directory->bytes * 8U;
            }
        }
    }
    offset = compression_work.header_bytes;
    for (uint8_t channel = 0U; channel < LA_SERVICE_CHANNEL_COUNT; channel++)
    {
        compression_work.directory[channel].offset = offset;
        offset += compression_work.directory[channel].bytes;
        compression_work.report.mode[channel] = compression_work.directory[channel].mode;
    }
    compression_work.report.file_bytes = offset + DTFH_FOOTER_BYTES;
    return COMPRESSION_OK;
}

static Compression_Result_t Compression_Encode_Header(uint8_t complete)
{
    uint8_t *header = compression_work.header;
    const LA_Service_Info_t *capture = &compression_work.report.capture;

    memset(header, 0, compression_work.header_bytes);
    memcpy(header, "DTFH", 4U);
    header[4] = DTFH_VERSION_MAJOR;
    header[6] = DTFH_VERSION_MINOR;
    Compression_Put_U32(header + 8U, compression_work.header_bytes);
    Compression_Put_U32(header + 12U, complete ? compression_work.flags :
                        (compression_work.flags & ~DTFH_FLAG_COMPLETE));
    header[16] = LA_SERVICE_CHANNEL_COUNT;
    header[17] = capture->initial_levels;
    header[18] = (uint8_t)capture->state;
    header[19] = capture->finalize_failed;
    Compression_Put_U32(header + 20U, (uint32_t)capture->stop_reason);
    Compression_Put_U32(header + 24U, capture->capture_id);
    Compression_Put_U32(header + 28U, capture->timestamp_hz);
    Compression_Put_U32(header + 32U, capture->deadline_tick);
    Compression_Put_U32(header + 36U, capture->cutoff_tick);
    Compression_Put_U32(header + 40U, capture->overcapture_mask);
    Compression_Put_U64(header + 48U, compression_work.report.file_bytes);
    for (uint8_t channel = 0U; channel < LA_SERVICE_CHANNEL_COUNT; channel++)
    {
        const Compression_Directory_t *directory = &compression_work.directory[channel];
        uint8_t *entry = header + DTFH_FIXED_HEADER_BYTES + channel * DTFH_DIRECTORY_BYTES;

        Compression_Put_U32(entry, (uint32_t)directory->mode);
        Compression_Put_U32(entry + 4U, directory->count);
        Compression_Put_U32(entry + 8U, directory->first_tick);
        Compression_Put_U32(entry + 12U, directory->last_tick);
        Compression_Put_U64(entry + 16U, directory->offset);
        Compression_Put_U64(entry + 24U, directory->bytes);
        Compression_Put_U64(entry + 32U, directory->bits);
        Compression_Put_U32(entry + 40U, directory->raw_crc);
        Compression_Put_U32(entry + 44U, directory->payload_crc);
    }
    if ((compression_work.flags & DTFH_FLAG_HUFFMAN) != 0U)
    {
        uint16_t written = 0U;
        uint8_t sparse = ((compression_work.flags & DTFH_FLAG_SPARSE_CODEBOOK) != 0U) ? 1U : 0U;

        if ((DTFH_Codec_Codebook_Encode(compression_work.table, sparse,
                    header + DTFH_BASE_HEADER_BYTES, DTFH_CODEBOOK_BYTES, &written) != DTFH_CODEC_OK) ||
            (written != compression_work.header_bytes - DTFH_BASE_HEADER_BYTES))
        {
            return COMPRESSION_ERROR_FORMAT;
        }
    }
    compression_work.metadata_crc = DTFH_Codec_CRC32_Finish(
            DTFH_Codec_CRC32_Update(DTFH_CODEC_CRC32_INIT, header,
                                    compression_work.header_bytes));
    Compression_Put_U32(header + DTFH_METADATA_CRC_OFFSET, compression_work.metadata_crc);
    return COMPRESSION_OK;
}

static Compression_Result_t Compression_Encode_Channel(uint8_t channel)
{
    Compression_Directory_t *directory = &compression_work.directory[channel];
    Compression_Stream_t stream;
    uint32_t previous = 0U;
    uint32_t index = 0U;
    uint32_t crc = DTFH_CODEC_CRC32_INIT;

    Compression_Stream_Init(&stream, directory);
    while (index < directory->count)
    {
        uint16_t count = (directory->count - index > COMPRESSION_READ_COUNT) ?
                COMPRESSION_READ_COUNT : (uint16_t)(directory->count - index);
        Compression_Result_t result = Compression_Get_Data(channel, index, count);

        if (result != COMPRESSION_OK)
        {
            return result;
        }
        for (uint16_t i = 0U; i < count; i++)
        {
            uint8_t bytes[DTFH_CODEC_ULEB128_MAX_BYTES];
            uint8_t length;
            uint32_t timestamp = compression_work.timestamps[i];

            if (timestamp < previous)
            {
                return COMPRESSION_ERROR_FORMAT;
            }
            crc = Compression_Timestamp_CRC(crc, timestamp);
            if (directory->mode == COMPRESSION_MODE_RAW_U32)
            {
                Compression_Put_U32(bytes, timestamp);
                length = 4U;
            }
            else
            {
                length = DTFH_Codec_ULEB128_Encode(timestamp - previous, bytes);
            }
            previous = timestamp;
            for (uint8_t position = 0U; position < length; position++)
            {
                if (directory->mode == COMPRESSION_MODE_DELTA_HUFFMAN)
                {
                    DTFH_Codec_Result_t codec_result = DTFH_Codec_Huffman_Write(
                            &compression_work.table[position], bytes[position],
                            Compression_Stream_Write_Bit, &stream);

                    if (codec_result != DTFH_CODEC_OK)
                    {
                        return (stream.result != COMPRESSION_OK) ? stream.result :
                               COMPRESSION_ERROR_FORMAT;
                    }
                }
                else if (Compression_Stream_Write_Byte(&stream, bytes[position]) == 0U)
                {
                    return stream.result;
                }
            }
        }
        index += count;
    }
    if ((directory->mode == COMPRESSION_MODE_DELTA_HUFFMAN) &&
        (stream.bits != directory->bits))
    {
        return COMPRESSION_ERROR_FORMAT;
    }
    if (stream.bit_count != 0U)
    {
        if (Compression_Stream_Write_Byte(&stream,
                (uint8_t)(stream.bit_byte << (8U - stream.bit_count))) == 0U)
        {
            return stream.result;
        }
    }
    if (Compression_Stream_Flush(&stream) == 0U)
    {
        return stream.result;
    }
    if (DTFH_Codec_CRC32_Finish(crc) != directory->raw_crc)
    {
        return COMPRESSION_ERROR_CHECKSUM;
    }
    directory->payload_crc = DTFH_Codec_CRC32_Finish(stream.crc);
    return ((uint64_t)f_tell(&compression_work.file) == directory->offset + directory->bytes) ?
           COMPRESSION_OK : COMPRESSION_ERROR_FORMAT;
}

/* ================================================== */

/**
 * @brief  验证1.0/2.0版本、目录边界、共享码表与完整文件尾
 */
static Compression_Result_t Compression_Decode_Header(void)
{
    uint8_t *header = compression_work.header;
    LA_Service_Info_t *capture = &compression_work.report.capture;
    uint8_t footer[DTFH_FOOTER_BYTES];
    uint32_t computed_crc;
    uint64_t offset;
    uint8_t has_huffman = 0U;
    Compression_Result_t result;

    if ((uint64_t)f_size(&compression_work.file) < DTFH_BASE_HEADER_BYTES + DTFH_FOOTER_BYTES)
    {
        return COMPRESSION_ERROR_FORMAT;
    }
    result = Compression_Read(header, DTFH_FIXED_HEADER_BYTES);
    if (result != COMPRESSION_OK)
    {
        return result;
    }
    compression_work.flags = Compression_Get_U32(header + 12U);
    compression_work.header_bytes = Compression_Get_U32(header + 8U);
    if ((memcmp(header, "DTFH", 4U) != 0) ||
        ((header[4] != DTFH_VERSION_MAJOR) && (header[4] != DTFH_LEGACY_VERSION_MAJOR)) ||
        (header[5] != 0U) ||
        (header[6] != DTFH_VERSION_MINOR) || (header[7] != 0U) ||
        (header[16] != LA_SERVICE_CHANNEL_COUNT) ||
        ((compression_work.flags != DTFH_FLAG_COMPLETE) &&
         (compression_work.flags != (DTFH_FLAG_COMPLETE | DTFH_FLAG_HUFFMAN)) &&
         (compression_work.flags != (DTFH_FLAG_COMPLETE | DTFH_FLAG_HUFFMAN | DTFH_FLAG_SPARSE_CODEBOOK))) ||
        /* 1.0 文件不支持只保存已用符号的编码表，不能把 2.0 文件按旧格式读取 */
        ((header[4] == DTFH_LEGACY_VERSION_MAJOR) &&
         ((compression_work.flags & DTFH_FLAG_SPARSE_CODEBOOK) != 0U)) ||
        (compression_work.header_bytes < DTFH_BASE_HEADER_BYTES) ||
        (compression_work.header_bytes > DTFH_MAX_HEADER_BYTES) ||
        (Compression_Get_U32(header + 44U) != 0U) ||
        (Compression_Get_U32(header + 60U) != 0U))
    {
        return COMPRESSION_ERROR_FORMAT;
    }
    if ((compression_work.flags & DTFH_FLAG_HUFFMAN) == 0U)
    {
        if (compression_work.header_bytes != DTFH_BASE_HEADER_BYTES)
        {
            return COMPRESSION_ERROR_FORMAT;
        }
    }
    else
    {
        compression_work.report.codebook_bytes = compression_work.header_bytes - DTFH_BASE_HEADER_BYTES;
        if ((compression_work.flags & DTFH_FLAG_SPARSE_CODEBOOK) != 0U)
        {
            if (compression_work.report.codebook_bytes < DTFH_MIN_SPARSE_CODEBOOK_BYTES)
            {
                return COMPRESSION_ERROR_FORMAT;
            }
            compression_work.report.codebook = COMPRESSION_CODEBOOK_SPARSE;
        }
        else
        {
            if (compression_work.report.codebook_bytes != DTFH_CODEBOOK_BYTES)
            {
                return COMPRESSION_ERROR_FORMAT;
            }
            compression_work.report.codebook = COMPRESSION_CODEBOOK_DENSE;
        }
    }
    compression_work.report.file_bytes = Compression_Get_U64(header + 48U);
    if ((compression_work.report.file_bytes != (uint64_t)f_size(&compression_work.file)) ||
        (compression_work.report.file_bytes < compression_work.header_bytes + DTFH_FOOTER_BYTES))
    {
        return COMPRESSION_ERROR_FORMAT;
    }
    result = Compression_Read(header + DTFH_FIXED_HEADER_BYTES,
                               compression_work.header_bytes - DTFH_FIXED_HEADER_BYTES);
    if (result != COMPRESSION_OK)
    {
        return result;
    }
    compression_work.metadata_crc = Compression_Get_U32(header + DTFH_METADATA_CRC_OFFSET);
    Compression_Put_U32(header + DTFH_METADATA_CRC_OFFSET, 0U);
    computed_crc = DTFH_Codec_CRC32_Finish(DTFH_Codec_CRC32_Update(
            DTFH_CODEC_CRC32_INIT, header, compression_work.header_bytes));
    Compression_Put_U32(header + DTFH_METADATA_CRC_OFFSET, compression_work.metadata_crc);
    if (computed_crc != compression_work.metadata_crc)
    {
        return COMPRESSION_ERROR_CHECKSUM;
    }
    capture->initial_levels = header[17];
    capture->state = (LA_Service_State_t)header[18];
    capture->finalize_failed = header[19];
    capture->stop_reason = (LA_Service_Stop_Reason_t)Compression_Get_U32(header + 20U);
    capture->capture_id = Compression_Get_U32(header + 24U);
    capture->timestamp_hz = Compression_Get_U32(header + 28U);
    capture->deadline_tick = Compression_Get_U32(header + 32U);
    capture->cutoff_tick = Compression_Get_U32(header + 36U);
    capture->overcapture_mask = Compression_Get_U32(header + 40U);
    offset = compression_work.header_bytes;
    for (uint8_t channel = 0U; channel < LA_SERVICE_CHANNEL_COUNT; channel++)
    {
        Compression_Directory_t *directory = &compression_work.directory[channel];
        const uint8_t *entry = header + DTFH_FIXED_HEADER_BYTES + channel * DTFH_DIRECTORY_BYTES;
        uint32_t mode = Compression_Get_U32(entry);

        if (mode > COMPRESSION_MODE_DELTA_HUFFMAN)
        {
            return COMPRESSION_ERROR_FORMAT;
        }
        directory->mode = (Compression_Mode_t)mode;
        directory->count = Compression_Get_U32(entry + 4U);
        directory->first_tick = Compression_Get_U32(entry + 8U);
        directory->last_tick = Compression_Get_U32(entry + 12U);
        directory->offset = Compression_Get_U64(entry + 16U);
        directory->bytes = Compression_Get_U64(entry + 24U);
        directory->bits = Compression_Get_U64(entry + 32U);
        directory->raw_crc = Compression_Get_U32(entry + 40U);
        directory->payload_crc = Compression_Get_U32(entry + 44U);
        capture->data_count[channel] = directory->count;
        compression_work.report.mode[channel] = directory->mode;
        compression_work.report.original_bytes += (uint64_t)directory->count * 4U;
        if ((directory->offset != offset) ||
            (directory->bytes > compression_work.report.file_bytes - DTFH_FOOTER_BYTES - offset))
        {
            return COMPRESSION_ERROR_FORMAT;
        }
        if (directory->mode == COMPRESSION_MODE_EMPTY)
        {
            if ((directory->count != 0U) || (directory->bytes != 0U) ||
                (directory->bits != 0U) || (directory->first_tick != 0U) ||
                (directory->last_tick != 0U) || (directory->raw_crc != 0U) ||
                (directory->payload_crc != 0U))
            {
                return COMPRESSION_ERROR_FORMAT;
            }
        }
        else
        {
            uint64_t maximum_bits = (uint64_t)directory->count *
                    DTFH_CODEC_ULEB128_MAX_BYTES * DTFH_CODEC_MAX_CODE_BITS;

            if ((directory->count == 0U) || (directory->first_tick > directory->last_tick) ||
                (directory->bytes == 0U) || (directory->bits == 0U) ||
                (directory->bits > maximum_bits) ||
                (directory->bytes != (directory->bits + 7U) / 8U))
            {
                return COMPRESSION_ERROR_FORMAT;
            }
            if (directory->mode == COMPRESSION_MODE_RAW_U32)
            {
                if ((directory->bytes != (uint64_t)directory->count * 4U) ||
                    (directory->bits != directory->bytes * 8U))
                {
                    return COMPRESSION_ERROR_FORMAT;
                }
            }
            else if (directory->mode == COMPRESSION_MODE_DELTA_ULEB128)
            {
                if ((directory->bytes < directory->count) ||
                    (directory->bytes > (uint64_t)directory->count * DTFH_CODEC_ULEB128_MAX_BYTES) ||
                    (directory->bits != directory->bytes * 8U))
                {
                    return COMPRESSION_ERROR_FORMAT;
                }
            }
            else
            {
                if ((compression_work.flags & DTFH_FLAG_HUFFMAN) == 0U)
                {
                    return COMPRESSION_ERROR_FORMAT;
                }
                has_huffman = 1U;
            }
        }
        offset += directory->bytes;
    }
    if ((offset != compression_work.report.file_bytes - DTFH_FOOTER_BYTES) ||
        (((compression_work.flags & DTFH_FLAG_HUFFMAN) != 0U) && (has_huffman == 0U)))
    {
        return COMPRESSION_ERROR_FORMAT;
    }
    result = Compression_Validate_Capture(capture);
    if (result != COMPRESSION_OK)
    {
        return result;
    }
    if (has_huffman != 0U)
    {
        uint8_t sparse = ((compression_work.flags & DTFH_FLAG_SPARSE_CODEBOOK) != 0U) ? 1U : 0U;

        if (DTFH_Codec_Codebook_Decode(header + DTFH_BASE_HEADER_BYTES,
                    (uint16_t)compression_work.report.codebook_bytes, sparse,
                    compression_work.table) != DTFH_CODEC_OK)
        {
            return COMPRESSION_ERROR_FORMAT;
        }
    }
    result = Compression_Seek(offset);
    if (result == COMPRESSION_OK)
    {
        result = Compression_Read(footer, sizeof(footer));
    }
    if (result != COMPRESSION_OK)
    {
        return result;
    }
    if ((memcmp(footer, "HFND", 4U) != 0) ||
        (Compression_Get_U64(footer + 8U) != compression_work.report.file_bytes))
    {
        return COMPRESSION_ERROR_FORMAT;
    }
    return (Compression_Get_U32(footer + 4U) == compression_work.metadata_crc) ?
           COMPRESSION_OK : COMPRESSION_ERROR_CHECKSUM;
}

/**
 * @brief  分段检查单通道数据，可选择只校验或同时导入
 */
static Compression_Result_t Compression_Decode_Channel(uint8_t channel,
                                                        LA_Service_Import_t *import)
{
    const Compression_Directory_t *directory = &compression_work.directory[channel];
    Compression_Stream_t stream;
    uint32_t previous = 0U;
    uint32_t first = 0U;
    uint32_t crc = DTFH_CODEC_CRC32_INIT;
    uint32_t index = 0U;
    Compression_Result_t result = Compression_Seek(directory->offset);

    if (result != COMPRESSION_OK)
    {
        return result;
    }
    Compression_Stream_Init(&stream, directory);
    while (index < directory->count)
    {
        uint16_t count = (directory->count - index > COMPRESSION_READ_COUNT) ?
                COMPRESSION_READ_COUNT : (uint16_t)(directory->count - index);

        for (uint16_t i = 0U; i < count; i++)
        {
            uint8_t bytes[DTFH_CODEC_ULEB128_MAX_BYTES];
            uint32_t timestamp;

            if (directory->mode == COMPRESSION_MODE_RAW_U32)
            {
                for (uint8_t position = 0U; position < 4U; position++)
                {
                    if (Compression_Stream_Read_Byte(&stream, &bytes[position]) == 0U)
                    {
                        return stream.result;
                    }
                }
                timestamp = Compression_Get_U32(bytes);
                if (timestamp < previous)
                {
                    return COMPRESSION_ERROR_FORMAT;
                }
            }
            else
            {
                uint8_t length = 0U;
                uint32_t delta;
                uint64_t absolute;

                do
                {
                    if (length == DTFH_CODEC_ULEB128_MAX_BYTES)
                    {
                        return COMPRESSION_ERROR_FORMAT;
                    }
                    if (directory->mode == COMPRESSION_MODE_DELTA_HUFFMAN)
                    {
                        if (DTFH_Codec_Huffman_Read(&compression_work.table[length],
                                Compression_Stream_Read_Bit, &stream, &bytes[length]) != DTFH_CODEC_OK)
                        {
                            return (stream.result != COMPRESSION_OK) ? stream.result :
                                   COMPRESSION_ERROR_FORMAT;
                        }
                    }
                    else if (Compression_Stream_Read_Byte(&stream, &bytes[length]) == 0U)
                    {
                        return stream.result;
                    }
                    length++;
                } while ((bytes[length - 1U] & 0x80U) != 0U);
                if (DTFH_Codec_ULEB128_Decode(bytes, length, &delta) != DTFH_CODEC_OK)
                {
                    return COMPRESSION_ERROR_FORMAT;
                }
                absolute = (uint64_t)previous + delta;
                if (absolute > UINT32_MAX)
                {
                    return COMPRESSION_ERROR_FORMAT;
                }
                timestamp = (uint32_t)absolute;
            }
            if ((index == 0U) && (i == 0U))
            {
                first = timestamp;
            }
            previous = timestamp;
            compression_work.timestamps[i] = timestamp;
            crc = Compression_Timestamp_CRC(crc, timestamp);
        }
        if (import != NULL)
        {
            LA_Service_Result_t la_result = LA_Service_Import_Append(import,
                    (LA_Service_Channel_t)channel, compression_work.timestamps, count);

            if (la_result != LA_SERVICE_OK)
            {
                return Compression_LA_Error(la_result);
            }
        }
        index += count;
    }
    if ((stream.remaining != 0U) || (stream.index != stream.count) ||
        (first != directory->first_tick) || (previous != directory->last_tick))
    {
        return COMPRESSION_ERROR_FORMAT;
    }
    if (directory->mode == COMPRESSION_MODE_DELTA_HUFFMAN)
    {
        if ((stream.bits != directory->bits) ||
            ((stream.bit_count != 0U) &&
             ((stream.bit_byte & ((1U << stream.bit_count) - 1U)) != 0U)))
        {
            return COMPRESSION_ERROR_FORMAT;
        }
    }
    if ((DTFH_Codec_CRC32_Finish(crc) != directory->raw_crc) ||
        (DTFH_Codec_CRC32_Finish(stream.crc) != directory->payload_crc))
    {
        return COMPRESSION_ERROR_CHECKSUM;
    }
    return COMPRESSION_OK;
}

static Compression_Result_t Compression_Decode_All(LA_Service_Import_t *import)
{
    for (uint8_t channel = 0U; channel < LA_SERVICE_CHANNEL_COUNT; channel++)
    {
        Compression_Result_t result = Compression_Decode_Channel(channel, import);

        if (result != COMPRESSION_OK)
        {
            return result;
        }
    }
    return COMPRESSION_OK;
}

/* ================================================== */

/**
 * @brief  创建工作区互斥锁，重复调用不会重复创建，也不会访问 TF 卡
 * @note   Init 允许启动期上下文，实际 Save/Inspect/Load 必须在可阻塞任务上下文调用
 */
uint8_t Compression_Service_Init(void)
{
    if (FileSystem_Port_Is_Init_Context() == 0U)
    {
        return 1U;
    }
    if (compression_mutex == NULL)
    {
        compression_mutex = xSemaphoreCreateMutex();
    }
    return (compression_mutex == NULL) ? 1U : 0U;
}

/**
 * @brief  保留采集数据期间，扫描数据、选择压缩模式并编码
 * @note   退出前释放采集数据和工作区锁；临时文件写入并关闭后才使用正式文件名
 */
Compression_Result_t Compression_Service_Save(const char *path,
                                               Compression_File_Info_t *info)
{
    LA_Service_Capture_Lease_t lease = {0U, 0U};
    Compression_Result_t result = Compression_Begin(path, info);
    LA_Service_Result_t la_result;
    FRESULT file_result;
    uint8_t footer[DTFH_FOOTER_BYTES];

    if (result != COMPRESSION_OK)
    {
        return result;
    }
    la_result = LA_Service_Capture_Acquire(&lease, &compression_work.report.capture);
    if (la_result != LA_SERVICE_OK)
    {
        return Compression_End(Compression_LA_Error(la_result), info);
    }
    result = Compression_Validate_Capture(&compression_work.report.capture);
    if (result == COMPRESSION_OK)
    {
        /* 不覆盖已有文件 */
        file_result = f_stat(path, NULL);
        if (file_result != FR_NO_FILE)
        {
            (void)Log_Printf("file: f_stat target failed fs=%u\r\n", (unsigned int)file_result);
            result = Compression_File_Error((file_result == FR_OK) ? FR_EXIST : file_result);
        }
    }
    if (result == COMPRESSION_OK)
    {
        result = Compression_Scan();
    }
    if (result == COMPRESSION_OK)
    {
        result = Compression_Select_Modes();
    }
    if (result != COMPRESSION_OK)
    {
        goto release;
    }
    file_result =
        f_open(&compression_work.file, compression_work.temporary_path, FA_WRITE | FA_CREATE_NEW);
    if (file_result != FR_OK)
    {
        (void)Log_Printf("file: f_open tmp failed fs=%u\r\n", (unsigned int)file_result);
        result = Compression_File_Error(file_result);
        goto release;
    }
    compression_file_open = 1U;
    result = Compression_Encode_Header(0U);
    if (result == COMPRESSION_OK)
    {
        result = Compression_Write(compression_work.header, compression_work.header_bytes);
    }
    for (uint8_t channel = 0U; (channel < LA_SERVICE_CHANNEL_COUNT) && (result == COMPRESSION_OK);
         channel++)
    {
        result = Compression_Encode_Channel(channel);
    }
    if (result == COMPRESSION_OK)
    {
        result = Compression_Encode_Header(1U);
    }
    if (result == COMPRESSION_OK)
    {
        memcpy(footer, "HFND", 4U);
        Compression_Put_U32(footer + 4U, compression_work.metadata_crc);
        Compression_Put_U64(footer + 8U, compression_work.report.file_bytes);
        result = Compression_Write(footer, sizeof(footer));
    }
    if (result == COMPRESSION_OK)
    {
        result = Compression_Seek(0U);
    }
    if (result == COMPRESSION_OK)
    {
        result = Compression_Write(compression_work.header, compression_work.header_bytes);
    }
    if (result == COMPRESSION_OK)
    {
        file_result = f_sync(&compression_work.file);
        if (file_result != FR_OK)
        {
            (void)Log_Printf("file: f_sync failed fs=%u\r\n", (unsigned int)file_result);
            result = Compression_File_Error(file_result);
        }
    }
    result = Compression_Close(result);
    if (result == COMPRESSION_OK)
    {
        file_result = f_rename(compression_work.temporary_path, path);
        if (file_result != FR_OK)
        {
            (void)Log_Printf("file: f_rename failed fs=%u\r\n", (unsigned int)file_result);
            result = Compression_File_Error(file_result);
        }
    }

release:
    la_result = LA_Service_Capture_Release(&lease);
    if ((result == COMPRESSION_OK) && (la_result != LA_SERVICE_OK))
    {
        result = Compression_LA_Error(la_result);
    }
    return Compression_End(result, info);
}

static Compression_Result_t Compression_Read_File(const char *path,
                                                   Compression_File_Info_t *info,
                                                   uint8_t load)
{
    Compression_Result_t result = Compression_Begin(path, info);
    LA_Service_Import_t import = {0U};
    FRESULT file_result;

    if (result != COMPRESSION_OK)
    {
        return result;
    }
    file_result = f_open(&compression_work.file, path, FA_READ | FA_OPEN_EXISTING);
    if (file_result != FR_OK)
    {
        return Compression_End(Compression_File_Error(file_result), info);
    }
    compression_file_open = 1U;
    result = Compression_Decode_Header();
    if (result == COMPRESSION_OK)
    {
        result = Compression_Decode_All(NULL);
    }
    if ((result == COMPRESSION_OK) && (load != 0U))
    {
        LA_Service_Result_t la_result = LA_Service_Import_Begin(
                &import, &compression_work.report.capture);

        if (la_result != LA_SERVICE_OK)
        {
            result = Compression_LA_Error(la_result);
        }
        else
        {
            result = Compression_Decode_All(&import);
        }
    }
    /* 读盘关闭失败也不得发布导入成功 */
    result = Compression_Close(result);
    if (import.token != 0U)
    {
        LA_Service_Result_t la_result;

        if (result == COMPRESSION_OK)
        {
            la_result = LA_Service_Import_Commit(&import);
            if (la_result != LA_SERVICE_OK)
            {
                result = Compression_LA_Error(la_result);
            }
        }
        if (import.token != 0U)
        {
            la_result = LA_Service_Import_Abort(&import);
            if (la_result != LA_SERVICE_OK)
            {
                /* 保留主错误，附加LA清理错误供调用者判断会话是否仍活动 */
                compression_work.report.la_result = la_result;
                if (result == COMPRESSION_OK)
                {
                    result = COMPRESSION_ERROR_LA_SERVICE;
                }
            }
        }
    }
    return Compression_End(result, info);
}

/**
 * @brief  通过共用加载路径完整试解码，不开始 LA 导入
 */
Compression_Result_t Compression_Service_Inspect(const char *path,
                                                  Compression_File_Info_t *info)
{
    return Compression_Read_File(path, info, 0U);
}

/**
 * @brief  先试解码通过，再在同一调用任务中流式导入并提交
 */
Compression_Result_t Compression_Service_Load(const char *path,
                                               Compression_File_Info_t *info)
{
    return Compression_Read_File(path, info, 1U);
}
