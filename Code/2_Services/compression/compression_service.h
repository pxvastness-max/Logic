/**
 * @file   compression_service.h
 * @brief  六通道边沿采集的DTFH文件压缩与加载接口
 */

#ifndef COMPRESSION_SERVICE_H
#define COMPRESSION_SERVICE_H

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================== */

#include "stdint.h"
#include "la/la_service.h"
#include "ff.h"

/* ================================================== */

#define COMPRESSION_PATH_CAPACITY 768U /* UTF-8完整路径及临时后缀的缓冲容量 */

/* ================================================== */

typedef enum
{
    COMPRESSION_MODE_EMPTY = 0,
    COMPRESSION_MODE_RAW_U32,
    COMPRESSION_MODE_DELTA_ULEB128,
    COMPRESSION_MODE_DELTA_HUFFMAN
} Compression_Mode_t;

typedef enum
{
    COMPRESSION_CODEBOOK_NONE = 0,
    COMPRESSION_CODEBOOK_DENSE,
    COMPRESSION_CODEBOOK_SPARSE
} Compression_Codebook_t;

typedef enum
{
    COMPRESSION_OK = 0,
    COMPRESSION_ERROR_BUSY,
    COMPRESSION_ERROR_INVALID_PARAMETER,
    COMPRESSION_ERROR_NOT_READY,
    COMPRESSION_ERROR_FILESYSTEM,
    COMPRESSION_ERROR_FORMAT,
    COMPRESSION_ERROR_CHECKSUM,
    COMPRESSION_ERROR_CAPACITY,
    COMPRESSION_ERROR_LA_SERVICE
} Compression_Result_t;

typedef struct
{
    LA_Service_Info_t capture; /* 文件来源信息 */
    uint64_t file_bytes;       /* 文件总字节数，包含固定头、目录、码表、载荷和尾部 */
    uint64_t original_bytes;   /* 仅计算原始时间戳，不含采集元数据 */
    /* 六路分别选择的空、原始、差值或 Huffman 模式 */
    Compression_Mode_t mode[LA_SERVICE_CHANNEL_COUNT];
    Compression_Codebook_t codebook;    /* 五张共享码表整体使用的文件存储形式 */
    uint32_t codebook_bytes;            /* NONE时为0，不含固定头和通道目录 */
    FRESULT filesystem_result;          /* 文件API的具体错误，短读写为FR_DISK_ERR */
    LA_Service_Result_t la_result;      /* 保留、读取或导入采集数据时的 LA 返回值 */
} Compression_File_Info_t;

/* ================================================== */

/**
 * @brief  创建压缩服务固定工作区的互斥访问锁
 * @retval 0 资源就绪或此前已创建
 * @note   启动时在 LA 与 FileSystem 就绪后串行调用，支持调度器启动前
 */
uint8_t Compression_Service_Init(void);

/**
 * @brief  将终态六通道采集无损保存为 DTFH 2.0 文件
 * @param  path NUL 结尾的 UTF-8 完整路径
 * @retval COMPRESSION_OK 临时文件已同步、关闭并重命名为正式文件
 * @note   同步任务接口，要求调度器运行、未屏蔽中断且卷已挂载
 */
Compression_Result_t Compression_Service_Save(const char *path,
                                               Compression_File_Info_t *info);

/**
 * @brief  完整解码并校验 DTFH 文件，返回来源信息，不改动采集数据
 * @param  info 可选元数据与模式/码表报告输出，失败时返回已知诊断
 * @retval COMPRESSION_OK 文件头、目录、编码表、通道数据、时间戳和 CRC 均已通过检查
 * @note   只在调度器运行且中断未屏蔽的任务中同步调用，卷须先挂载
 */
Compression_Result_t Compression_Service_Inspect(const char *path,
                                                  Compression_File_Info_t *info);

/**
 * @brief  校验文件并流式恢复到 LA 共享存储块池
 * @param  info 可选报告输出
 * @retval COMPRESSION_OK 正式导入完成，可使用 LA 查询与协议解析
 * @note   同步任务接口，卷已挂载且 LA 为 IDLE，上层须显式 Reset 旧数据
 */
Compression_Result_t Compression_Service_Load(const char *path,
                                               Compression_File_Info_t *info);

/* ================================================== */

#ifdef __cplusplus
}
#endif

#endif /* COMPRESSION_SERVICE_H */
