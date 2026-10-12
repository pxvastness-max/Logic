/**
 * @file   freertos_hooks.c
 * @brief  FreeRTOS 内核要求的钩子函数实现（平台层，Code/5_Platform）
 */

/* ================================================== */

#include "FreeRTOS.h"
#include "task.h"
#include "system/platform_system.h"

/* ================================================== */

/**
 * @brief  任务栈溢出时进入这里
 */
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    (void)pcTaskName; /* 断点停在这里时，把 pcTaskName 加入 Watch 窗口 */

    Platform_Fatal_Error();
}

/**
 * @brief  堆内存分配失败时进入这里
 */
void vApplicationMallocFailedHook(void)
{
    Platform_Fatal_Error();
}
