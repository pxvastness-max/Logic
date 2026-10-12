/**
 * @file   fatfs_diskio.c
 * @brief  FatFs物理磁盘0到板载SDMMC1的适配
 */

/* ================================================== */

#include "filesystem/filesystem_internal.h"
#include "sd/sd_card.h"
#include "filesystem/fatfs_port.h"
#include "log/log_service.h"
#include "memory/memory_placement.h"
#include "memory/platform_memory.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include <string.h>

/* ========================= 配置 ========================= */

#define FATFS_DISK_DRIVE            0U
#define FATFS_DISK_SECTOR_SIZE      512U
#define FATFS_DISK_TRANSFER_SECTORS 8U

#if (FF_VOLUMES != 1) || (FF_MIN_SS != 512) || (FF_MAX_SS != 512) || FF_LBA64
    #error "SD disk port requires one volume, 512-byte sectors and 32-bit LBA"
#endif

/* ================================================== */

PLATFORM_AXI_SRAM_ZI
PLATFORM_CACHE_ALIGNMENT_ATTRIBUTE static uint8_t
    fatfs_disk_buffer[FATFS_DISK_TRANSFER_SECTORS * FATFS_DISK_SECTOR_SIZE];
static SemaphoreHandle_t fatfs_disk_mutex = NULL;
static uint8_t fatfs_disk_initialized = 0U;

/* ================================================== */

static void FatFs_Disk_Clean_Buffer(uint32_t byte_count)
{
    Platform_Memory_Clean_Cache(fatfs_disk_buffer, byte_count);
}

static void FatFs_Disk_Invalidate_Buffer(uint32_t byte_count)
{
    Platform_Memory_Invalidate_Cache(fatfs_disk_buffer, byte_count);
}

/* ================================================== */

static uint8_t FatFs_Disk_Lock(void)
{
    if ((FileSystem_Port_Is_Task_Context() == 0U) || (fatfs_disk_mutex == NULL))
    {
        return 0U;
    }
    return (uint8_t)(xSemaphoreTake(fatfs_disk_mutex, (TickType_t)FF_FS_TIMEOUT) == pdTRUE);
}

/**
 * @brief  将 SD 操作结果映射为 FatFs 磁盘错误
 */
static DRESULT FatFs_Disk_Map_Result(Device_SD_Result_t result)
{
    switch (result)
    {
        case DEVICE_SD_OK:
            return RES_OK;
        case DEVICE_SD_ERROR_PARAMETER:
            return RES_PARERR;
        case DEVICE_SD_ERROR_NOT_READY:
        case DEVICE_SD_ERROR_INIT:
            fatfs_disk_initialized = 0U;
            return RES_NOTRDY;
        case DEVICE_SD_ERROR_CONTEXT:
            return RES_NOTRDY;
        default:
            fatfs_disk_initialized = 0U;
            return RES_ERROR;
    }
}

static DRESULT FatFs_Disk_Check_Range(LBA_t sector, UINT count)
{
    Device_SD_Card_Info_t info;
    DRESULT result;

    if ((fatfs_disk_initialized == 0U) || (Device_SD_Is_Ready() == 0U))
    {
        return RES_NOTRDY;
    }
    result = FatFs_Disk_Map_Result(Device_SD_Get_Card_Info(&info));
    if (result != RES_OK)
    {
        return result;
    }
    if ((info.sector_size != FATFS_DISK_SECTOR_SIZE) || (sector >= info.sector_count) ||
        (count > info.sector_count - sector))
    {
        return RES_PARERR;
    }
    return RES_OK;
}

/* ================================================== */

uint8_t FatFs_Disk_Init(void)
{
    if (FileSystem_Port_Is_Init_Context() == 0U)
    {
        return 1U;
    }
    if (fatfs_disk_mutex == NULL)
    {
        fatfs_disk_mutex = xSemaphoreCreateMutex();
    }
    return (uint8_t)(fatfs_disk_mutex == NULL);
}

DRESULT FatFs_Disk_DeInit(void)
{
    DRESULT result;

    if (FatFs_Disk_Lock() == 0U)
    {
        return RES_NOTRDY;
    }
    result = FatFs_Disk_Map_Result(Device_SD_DeInit());
    fatfs_disk_initialized = 0U;
    (void)xSemaphoreGive(fatfs_disk_mutex);
    return result;
}

/**
 * @brief  持有磁盘锁时初始化 SD 卡，并确认扇区大小为 512 字节
 * @note   只在调度器运行的任务中调用，不提供物理卡座插拔检测
 */
DSTATUS disk_initialize(BYTE pdrv)
{
    Device_SD_Card_Info_t info = {0U, 0U};
    Device_SD_Result_t init_result;
    Device_SD_Result_t info_result = DEVICE_SD_ERROR_NOT_READY;
    DSTATUS status = STA_NOINIT;

    if ((pdrv != FATFS_DISK_DRIVE) || (FatFs_Disk_Lock() == 0U))
    {
        return STA_NOINIT;
    }
    fatfs_disk_initialized = 0U;
    init_result = Device_SD_Init();
    if (init_result == DEVICE_SD_OK)
    {
        info_result = Device_SD_Get_Card_Info(&info);
    }
    if ((init_result == DEVICE_SD_OK) && (info_result == DEVICE_SD_OK) &&
        (info.sector_size == FATFS_DISK_SECTOR_SIZE) && (info.sector_count != 0U))
    {
        fatfs_disk_initialized = 1U;
        status = 0U;
    }
    if (status != 0U)
    {
        (void)Log_Printf("file: SD init failed init=%u info=%u sectors=%lu hal=%08lx\r\n",
                         (unsigned int)init_result,
                         (unsigned int)info_result,
                         (unsigned long)info.sector_count,
                         (unsigned long)Device_SD_Get_Last_HAL_Error());
    }
    (void)xSemaphoreGive(fatfs_disk_mutex);
    return status;
}

/**
 * @brief  检查磁盘和 SD 驱动是否已经初始化
 * @note   任务上下文调用，不发送卡命令，不能据此判断物理拔卡
 */
DSTATUS disk_status(BYTE pdrv)
{
    DSTATUS status;

    if ((pdrv != FATFS_DISK_DRIVE) || (FatFs_Disk_Lock() == 0U))
    {
        return STA_NOINIT;
    }
    status = ((fatfs_disk_initialized != 0U) && (Device_SD_Is_Ready() != 0U)) ? 0U : STA_NOINIT;
    (void)xSemaphoreGive(fatfs_disk_mutex);
    return status;
}

/**
 * @brief  通过固定的 AXI 缓冲区分批读取扇区
 * @note   读卡时独占共用缓冲区，每次最多读 8 个扇区；读前和读后都处理数据缓存
 */
DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    DRESULT result;

    if ((pdrv != FATFS_DISK_DRIVE) || (buff == NULL) || (count == 0U))
    {
        return RES_PARERR;
    }
    if (FatFs_Disk_Lock() == 0U)
    {
        return RES_NOTRDY;
    }
    result = FatFs_Disk_Check_Range(sector, count);
    while ((result == RES_OK) && (count != 0U))
    {
        UINT transfer = (count > FATFS_DISK_TRANSFER_SECTORS) ? FATFS_DISK_TRANSFER_SECTORS : count;
        Device_SD_Result_t bsp_result;
        uint32_t transfer_bytes = transfer * FATFS_DISK_SECTOR_SIZE;

        /* 丢弃旧缓存行，防止其稍后回写覆盖IDMA收到的新数据 */
        FatFs_Disk_Invalidate_Buffer(transfer_bytes);
        bsp_result = Device_SD_Read_Blocks(fatfs_disk_buffer, (uint32_t)sector, transfer);
        result = FatFs_Disk_Map_Result(bsp_result);
        if (result != RES_OK)
        {
            (void)Log_Printf("file: disk read sector=%lu count=%u bsp=%u hal=%08lx\r\n",
                             (unsigned long)sector,
                             (unsigned int)transfer,
                             (unsigned int)bsp_result,
                             (unsigned long)Device_SD_Get_Last_HAL_Error());
        }
        if (result == RES_OK)
        {
            /* IDMA写入内存后，使CPU下一次读取绕过传输前的缓存内容 */
            FatFs_Disk_Invalidate_Buffer(transfer_bytes);
            memcpy(buff, fatfs_disk_buffer, transfer_bytes);
            buff += transfer_bytes;
            sector += transfer;
            count -= transfer;
        }
    }
    (void)xSemaphoreGive(fatfs_disk_mutex);
    return result;
}

#if !FF_FS_READONLY

/**
 * @brief  先把数据复制到 AXI 缓冲区，再分批写入 SD 卡
 * @note   写卡期间独占缓冲区；每批复制完成后先清理数据缓存，再启动 IDMA
 */
DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    DRESULT result;

    if ((pdrv != FATFS_DISK_DRIVE) || (buff == NULL) || (count == 0U))
    {
        return RES_PARERR;
    }
    if (FatFs_Disk_Lock() == 0U)
    {
        return RES_NOTRDY;
    }
    result = FatFs_Disk_Check_Range(sector, count);
    while ((result == RES_OK) && (count != 0U))
    {
        UINT transfer = (count > FATFS_DISK_TRANSFER_SECTORS) ? FATFS_DISK_TRANSFER_SECTORS : count;
        Device_SD_Result_t bsp_result;
        uint32_t transfer_bytes = transfer * FATFS_DISK_SECTOR_SIZE;

        memcpy(fatfs_disk_buffer, buff, transfer_bytes);
        /* 将CPU写入的数据推到AXI SRAM，随后IDMA才能读取到最新内容 */
        FatFs_Disk_Clean_Buffer(transfer_bytes);
        bsp_result = Device_SD_Write_Blocks(fatfs_disk_buffer, (uint32_t)sector, transfer);
        result = FatFs_Disk_Map_Result(bsp_result);
        if (result != RES_OK)
        {
            (void)Log_Printf("file: disk write sector=%lu count=%u bsp=%u hal=%08lx\r\n",
                             (unsigned long)sector,
                             (unsigned int)transfer,
                             (unsigned int)bsp_result,
                             (unsigned long)Device_SD_Get_Last_HAL_Error());
        }
        if (result == RES_OK)
        {
            buff += transfer_bytes;
            sector += transfer;
            count -= transfer;
        }
    }
    (void)xSemaphoreGive(fatfs_disk_mutex);
    return result;
}
#endif

/**
 * @brief  持有磁盘锁时检查卡状态，或读取文件系统需要的容量信息
 */
DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    Device_SD_Card_Info_t info;
    DRESULT result;

    if ((pdrv != FATFS_DISK_DRIVE) ||
        ((cmd != CTRL_SYNC) && (cmd != GET_SECTOR_COUNT) && (cmd != GET_SECTOR_SIZE) &&
         (cmd != GET_BLOCK_SIZE)) ||
        ((cmd != CTRL_SYNC) && (buff == NULL)))
    {
        return RES_PARERR;
    }
    if (FatFs_Disk_Lock() == 0U)
    {
        return RES_NOTRDY;
    }
    if ((fatfs_disk_initialized == 0U) || (Device_SD_Is_Ready() == 0U))
    {
        result = RES_NOTRDY;
    }
    else if (cmd == CTRL_SYNC)
    {
        Device_SD_Result_t bsp_result = Device_SD_Sync();

        result = FatFs_Disk_Map_Result(bsp_result);
        if (result != RES_OK)
        {
            (void)Log_Printf("file: disk sync bsp=%u hal=%08lx\r\n",
                             (unsigned int)bsp_result,
                             (unsigned long)Device_SD_Get_Last_HAL_Error());
        }
    }
    else
    {
        result = FatFs_Disk_Map_Result(Device_SD_Get_Card_Info(&info));
        if (result == RES_OK)
        {
            if (cmd == GET_SECTOR_COUNT)
            {
                LBA_t value = info.sector_count;
                memcpy(buff, &value, sizeof(value));
            }
            else if (cmd == GET_SECTOR_SIZE)
            {
                WORD value = (WORD)info.sector_size;
                memcpy(buff, &value, sizeof(value));
            }
            else
            {
                /* 未公开SD擦除组粒度，FatFs允许返回1作为未知时的保守值 */
                DWORD value = 1U;
                memcpy(buff, &value, sizeof(value));
            }
        }
    }
    (void)xSemaphoreGive(fatfs_disk_mutex);
    return result;
}
