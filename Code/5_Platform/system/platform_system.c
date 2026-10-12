/**
 * @file   platform_system.c
 * @brief  全局 HAL 支持与统一故障终止点
 */

/* ================================================== */

#include "system/platform_system.h"
#include "stm32h7xx_hal.h"

/* ================================================== */

/**
 * @brief  HAL 全局 MSP 只开启系统配置控制器；外设资源由各 BSP 驱动管理
 */
void HAL_MspInit(void)
{
    __HAL_RCC_SYSCFG_CLK_ENABLE();
}

void Platform_Fatal_Error(void)
{
    __disable_irq();
    for (;;)
    {
        __NOP();
    }
}

#ifdef USE_FULL_ASSERT
/**
 * @brief  HAL 参数断言失败时保留调用位置并进入统一故障终止点
 */
void assert_failed(uint8_t *file, uint32_t line)
{
    (void)file;
    (void)line;
    Platform_Fatal_Error();
}
#endif
