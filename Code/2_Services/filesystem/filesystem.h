/**
 * @file   filesystem.h
 * @brief  TF卡文件系统生命周期与容量查询
 */

#ifndef FILESYSTEM_H
#define FILESYSTEM_H

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================== */

#include <stdint.h>
#include "ff.h"

/* ================================================== */

#define FILESYSTEM_DRIVE_PATH "0:"
#define FILESYSTEM_ROOT_PATH  "0:/"

/* ================================================== */

typedef struct
{
    uint32_t sector_count;        /* TF卡物理容量，单位512字节扇区 */
    uint32_t sectors_per_cluster; /* 当前卷簇大小，单位 512 字节扇区 */
    uint64_t total_bytes;         /* 卷内数据区容量，不含FAT等元数据 */
    uint64_t free_bytes;          /* 当前可用数据簇字节数，不表示文件写入的事务保证 */
} FileSystem_Info_t;

/* ================================================== */

/**
 * @brief  注册单卷 FatFs 并准备磁盘串行访问资源
 * @retval FR_OK 注册成功，或此前已经完成初始化
 * @note   启动时串行调用，允许调度器启动前，采用延迟挂载不访问 TF 卡
 */
FRESULT FileSystem_Init(void);

/**
 * @brief  访问根目录确认 TF 卷可用，必要时重新注册已卸载的卷
 * @retval FR_OK 根目录已成功打开并关闭
 * @note   只在调度器运行的任务中调用，拒绝 ISR、PRIMASK/BASEPRI 屏蔽和暂停调度器
 */
FRESULT FileSystem_Mount(void);

/**
 * @brief  查询当前卷的数据区容量、空闲量及 TF 物理扇区数量
 * @param  info 非 NULL 的容量输出，全部查询成功后才写入
 * @retval FR_OK 已读取容量信息
 * @note   只在调度器运行的任务中调用，其他卷访问错误沿用 FatFs 返回值
 */
FRESULT FileSystem_Get_Info(FileSystem_Info_t *info);

/**
 * @brief  注销当前 FatFs 卷并停止 SD 硬件资源
 * @retval FR_OK 卷已注销且磁盘反初始化成功
 * @note   应用须独占卷生命周期，先关闭全部 FIL/DIR 并停止并发访问和等待者
 */
FRESULT FileSystem_Unmount(void);

/* ================================================== */

/* f_open/f_read/f_write/f_sync/f_close等直接使用ff.h */

#ifdef __cplusplus
}
#endif

#endif /* FILESYSTEM_H */
