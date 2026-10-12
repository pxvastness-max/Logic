/**
 * @file   bsp_dma.h
 * @brief  六路 TIM 输入捕获的 DMA1/DMA2 资源管理
 */

#ifndef BSP_DMA_H
#define BSP_DMA_H

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================== */

#include "stm32h7xx_hal.h"
#include <stdbool.h>
#include <stdint.h>

/* ================================================== */

/* 仅供采集 BSP 共享；服务和应用应使用 la_ic.h 的采集接口 */
extern DMA_HandleTypeDef hdma_tim2_ch1;
extern DMA_HandleTypeDef hdma_tim2_ch3;
extern DMA_HandleTypeDef hdma_tim2_ch4;
extern DMA_HandleTypeDef hdma_tim5_ch1;
extern DMA_HandleTypeDef hdma_tim5_ch3;
extern DMA_HandleTypeDef hdma_tim5_ch4;

/* ================================================== */

/**
 * @brief  配置 DMA1 Stream0..2 和 DMA2 Stream0..2 为循环、32 位外设到内存传输
 * @param  timer2 已选择 TIM2 实例的非空 HAL 句柄
 * @param  timer5 已选择 TIM5 实例的非空 HAL 句柄
 * @note   只在捕获定时器初始化时调用
 */
bool BSP_DMA_Capture_Init(TIM_HandleTypeDef *timer2, TIM_HandleTypeDef *timer5);

/**
 * @brief  按采集通道序号分发 DMA1/DMA2 捕获中断
 */
void BSP_DMA_Capture_IRQHandler(uint32_t channel_index);

/* ================================================== */

#ifdef __cplusplus
}
#endif

#endif /* BSP_DMA_H */
