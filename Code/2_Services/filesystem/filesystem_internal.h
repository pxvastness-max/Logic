/**
 * @file   filesystem_internal.h
 * @brief  文件系统内部磁盘适配生命周期接口
 */

#ifndef FILESYSTEM_INTERNAL_H
#define FILESYSTEM_INTERNAL_H

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================== */

#include <stdint.h>
#include "ff.h"
#include "diskio.h"

/* ================================================== */

/**
 * @brief  创建互斥锁，防止多个任务同时使用 SD 卡读写缓冲区
 * @retval 0 已就绪或此前已创建
 * @note   由 FileSystem_Init 串行调用，可在调度器启动前执行，不初始化 TF 卡
 */
uint8_t FatFs_Disk_Init(void);

/**
 * @brief  在磁盘锁内反初始化 SD 并使软件磁盘状态失效
 * @retval RES_OK SD 资源已停止
 * @note   由卷生命周期拥有者在关闭全部文件对象后调用，不释放可复用的磁盘互斥锁
 */
DRESULT FatFs_Disk_DeInit(void);

/* ================================================== */

#ifdef __cplusplus
}
#endif

#endif /* FILESYSTEM_INTERNAL_H */
