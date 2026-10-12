/**
 * @file   platform_init.c
 * @brief  内核、时钟和平台时基初始化
 */

/* ================================================== */

#include "init/platform_init.h"
#include "clock/platform_clock.h"
#include "time/platform_dwt.h"
#include "memory/platform_memory.h"
#include "stm32h7xx_hal.h"

/* ================================================== */

/**
 * @brief  配置 MPU 和 Cache，再初始化 HAL 与系统时钟
 */
bool Platform_Init(void)
{
    Platform_Memory_Bootstrap();
    Platform_Memory_Enable_Cache();
    if (HAL_Init() != HAL_OK)
    {
        return false;
    }

    if (!Platform_Clock_Init())
    {
        return false;
    }
    return Platform_DWT_Init();
}
