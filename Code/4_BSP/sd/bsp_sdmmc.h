/**
 * @file   bsp_sdmmc.h
 * @brief  SDMMC1 外设资源与中断适配，供 SD BSP 内部使用
 */

#ifndef BSP_SDMMC_H
#define BSP_SDMMC_H

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================== */

#include "stm32h7xx_hal.h"
#include <stdbool.h>

/* ================================================== */

extern SD_HandleTypeDef hsd1;

/* ================================================== */

/**
 * @brief  准备 SDMMC1 时钟、引脚和 NVIC；失败返回 false，调用者可稍后重试
 */
bool BSP_SDMMC_Hardware_Init(void);

/**
 * @brief  强制关闭 IDMA、数据通道及中断，并保持 SDMMC1 复位直到下次初始化
 */
void BSP_SDMMC_Hardware_Stop(void);

/**
 * @brief  分发 SDMMC1 全局中断到 HAL；完成/错误回调由 sd_card.c 处理
 */
void BSP_SDMMC_IRQHandler(void);

/* ================================================== */

#ifdef __cplusplus
}
#endif

#endif /* BSP_SDMMC_H */
