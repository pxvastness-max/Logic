/**
 * @file   filesystem.c
 * @brief  TF卡卷注册、挂载、卸载和容量查询
 */

/* ================================================== */

#include "filesystem/filesystem.h"
#include "filesystem/filesystem_internal.h"
#include "filesystem/fatfs_port.h"
#include <stddef.h>

/* ================================================== */

static FATFS filesystem_volume;
static uint8_t filesystem_initialized = 0U;
static uint8_t filesystem_registered = 0U;

/* ================================================== */

/**
 * @brief  延迟注册卷并保留可重试的初始化状态
 * @note   仅注册成功后发布 initialized，避免前置锁成功但卷注册失败时跳过重试
 */
FRESULT FileSystem_Init(void)
{
    FRESULT result;

    if (FileSystem_Port_Is_Init_Context() == 0U)
    {
        return FR_INVALID_PARAMETER;
    }
    if (filesystem_initialized != 0U)
    {
        return FR_OK;
    }
    if (FatFs_Disk_Init() != 0U)
    {
        return FR_INT_ERR;
    }
    /* 延迟挂载：调度器启动前不访问卡、不等待卷锁 */
    result = f_mount(&filesystem_volume, FILESYSTEM_DRIVE_PATH, 0U);
    if (result == FR_OK)
    {
        filesystem_registered = 1U;
        filesystem_initialized = 1U;
    }
    return result;
}

/**
 * @brief  通过一次根目录打开/关闭触发延迟挂载
 * @note   普通重复挂载不替换卷对象，只有卸载后的重新注册需要独占生命周期
 */
FRESULT FileSystem_Mount(void)
{
    DIR directory;
    FRESULT result;

    if (FileSystem_Port_Is_Task_Context() == 0U)
    {
        return FR_INVALID_PARAMETER;
    }
    if (filesystem_initialized == 0U)
    {
        return FR_NOT_ENABLED;
    }
    if (filesystem_registered == 0U)
    {
        /* 卸载后的首次挂载须由应用独占，普通重复Mount不替换卷或互斥锁 */
        result = f_mount(&filesystem_volume, FILESYSTEM_DRIVE_PATH, 0U);
        if (result != FR_OK)
        {
            return result;
        }
        filesystem_registered = 1U;
    }
    result = f_opendir(&directory, FILESYSTEM_ROOT_PATH);
    if (result == FR_OK)
    {
        result = f_closedir(&directory);
    }
    return result;
}

/**
 * @brief  先注销卷，再停止磁盘硬件
 */
FRESULT FileSystem_Unmount(void)
{
    FRESULT result;

    if (FileSystem_Port_Is_Task_Context() == 0U)
    {
        return FR_INVALID_PARAMETER;
    }
    if (filesystem_initialized == 0U)
    {
        return FR_NOT_ENABLED;
    }
    if (filesystem_registered != 0U)
    {
        result = f_mount(NULL, FILESYSTEM_DRIVE_PATH, 0U);
        if (result != FR_OK)
        {
            return result;
        }
        filesystem_registered = 0U;
    }
    return (FatFs_Disk_DeInit() == RES_OK) ? FR_OK : FR_DISK_ERR;
}

/**
 * @brief  完成空闲簇和物理容量查询后一次性发布输出
 */
FRESULT FileSystem_Get_Info(FileSystem_Info_t *info)
{
    FATFS *volume;
    DWORD free_clusters;
    LBA_t sector_count;
    FRESULT result;
    FileSystem_Info_t value;

    if ((info == NULL) || (FileSystem_Port_Is_Task_Context() == 0U))
    {
        return FR_INVALID_PARAMETER;
    }
    if ((filesystem_initialized == 0U) || (filesystem_registered == 0U))
    {
        return FR_NOT_ENABLED;
    }
    result = f_getfree(FILESYSTEM_DRIVE_PATH, &free_clusters, &volume);
    if (result != FR_OK)
    {
        return result;
    }
    if (disk_ioctl(0U, GET_SECTOR_COUNT, &sector_count) != RES_OK)
    {
        return FR_DISK_ERR;
    }
    value.sector_count = (uint32_t)sector_count;
    value.sectors_per_cluster = volume->csize;
    value.total_bytes = (uint64_t)(volume->n_fatent - 2U) * volume->csize * FF_MAX_SS;
    value.free_bytes = (uint64_t)free_clusters * volume->csize * FF_MAX_SS;
    *info = value;
    return FR_OK;
}
