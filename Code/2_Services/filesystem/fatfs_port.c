/**
 * @file   fatfs_port.c
 * @brief  FatFs R0.16的FreeRTOS内存与互斥锁适配
 */

/* ================================================== */

#include "filesystem/fatfs_port.h"
#include "rtos/platform_runtime.h"
#include "ff.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

/* ================================================== */

#if FF_FS_REENTRANT

static SemaphoreHandle_t fatfs_mutex[FF_VOLUMES + 1]; /* 末项为FatFs系统互斥锁 */
#endif

/* ================================================== */

/**
 * @brief  检查允许建立 FatFs 互斥锁和分配工作区的调用上下文
 */
uint8_t FileSystem_Port_Is_Init_Context(void)
{
    return Platform_Runtime_Is_Init_Context() ? 1U : 0U;
}

/* ================================================== */

/**
 * @brief  检查文件访问及互斥等待所需的运行期任务上下文
 */
uint8_t FileSystem_Port_Is_Task_Context(void)
{
    return Platform_Runtime_Is_Task_Context() ? 1U : 0U;
}

/* ================================================== */

#if FF_USE_LFN == 3

/**
 * @brief  用 FreeRTOS 堆分配 FatFs 的动态长文件名工作区
 * @note   允许安全启动期上下文，不进行磁盘访问，不缓存已分配地址
 */
void *ff_memalloc(UINT msize)
{
    if (FileSystem_Port_Is_Init_Context() == 0U)
    {
        return NULL;
    }

    return pvPortMalloc((size_t)msize);
}

/* ================================================== */

/**
 * @brief  配对释放 FatFs 动态长文件名工作区
 * @note   必须在允许初始化资源的上下文调用，无效上下文不执行任何操作
 */
void ff_memfree(void *mblock)
{
    if ((mblock != NULL) && (FileSystem_Port_Is_Init_Context() != 0U))
    {
        vPortFree(mblock);
    }
}

#endif /* FF_USE_LFN == 3 */

/* ================================================== */

#if FF_FS_REENTRANT

/**
 * @brief  检查卷互斥锁和系统互斥锁编号
 */
static uint8_t FileSystem_Port_Is_Mutex_Index(int vol)
{
    return ((vol >= 0) && (vol <= FF_VOLUMES)) ? 1U : 0U;
}

/* ================================================== */

/**
 * @brief  创建 FatFs 卷锁或系统锁，重复调用不会重复创建
 * @note   由 FatFs 挂载管理调用，上层须独占挂载和卸载生命周期
 */
int ff_mutex_create(int vol)
{
    if ((FileSystem_Port_Is_Mutex_Index(vol) == 0U) || (FileSystem_Port_Is_Init_Context() == 0U))
    {
        return 0;
    }

    if (fatfs_mutex[vol] == NULL)
    {
        fatfs_mutex[vol] = xSemaphoreCreateMutex();
    }

    return (fatfs_mutex[vol] != NULL) ? 1 : 0;
}

/* ================================================== */

/**
 * @brief  删除 FatFs 已不再使用的卷锁或系统锁
 * @note   上层必须先停止文件访问，确认无拥有者及等待者，再由串行生命周期管理调用
 */
void ff_mutex_delete(int vol)
{
    if ((FileSystem_Port_Is_Mutex_Index(vol) == 0U) || (FileSystem_Port_Is_Init_Context() == 0U))
    {
        return;
    }

    if (fatfs_mutex[vol] != NULL)
    {
        vSemaphoreDelete(fatfs_mutex[vol]);
        fatfs_mutex[vol] = NULL;
    }
}

/* ================================================== */

/**
 * @brief  在有效任务上下文中有界等待非递归 FatFs 互斥锁
 */
int ff_mutex_take(int vol)
{
    if ((FileSystem_Port_Is_Mutex_Index(vol) == 0U) || (FileSystem_Port_Is_Task_Context() == 0U) ||
        (fatfs_mutex[vol] == NULL))
    {
        return 0;
    }

    return (xSemaphoreTake(fatfs_mutex[vol], (TickType_t)FF_FS_TIMEOUT) == pdTRUE) ? 1 : 0;
}

/* ================================================== */

/**
 * @brief  释放当前任务拥有的 FatFs 卷锁或系统锁
 * @note   必须由拥有任务配对释放，不能从 ISR 或其他任务替代拥有者释放
 */
void ff_mutex_give(int vol)
{
    if ((FileSystem_Port_Is_Mutex_Index(vol) == 0U) || (FileSystem_Port_Is_Task_Context() == 0U) ||
        (fatfs_mutex[vol] == NULL))
    {
        return;
    }

    (void)xSemaphoreGive(fatfs_mutex[vol]);
}

#endif /* FF_FS_REENTRANT */
