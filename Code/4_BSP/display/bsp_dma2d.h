/**
 * @file   bsp_dma2d.h
 * @brief  STM32H7 DMA2D 异步 RGB565 搬运和填充接口
 */

#ifndef BSP_DMA2D_H
#define BSP_DMA2D_H

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================== */

#include "stm32h7xx_hal.h"
#include <stdbool.h>
#include <stdint.h>

/* ================================================== */

/* 必须不高于FreeRTOS允许调用FromISR API的最高逻辑优先级 */

#ifndef BSP_DMA2D_IRQ_PRIORITY
    #define BSP_DMA2D_IRQ_PRIORITY 5U
#endif

/* DMA2D 总线访问节流：每次突发访问之间至少留出若干总线时钟 */

#ifndef BSP_DMA2D_AXI_DEAD_TIME
    #define BSP_DMA2D_AXI_DEAD_TIME 0U
#endif

/* ================================================== */

/**
 * @brief  DMA2D 传输结束回调
 * @note   回调在 DMA2D 中断上下文执行
 */
typedef void (*BSP_DMA2D_Callback_t)(void *context, bool success);

/* ================================================== */

extern DMA2D_HandleTypeDef bsp_dma2d_handle; /* HAL DMA2D句柄，供中断入口使用 */

/* ================================================== */

/**
 * @brief  初始化 RGB565 DMA2D 外设、前景格式及完成/错误中断回调
 */
bool BSP_DMA2D_Init(void);

/**
 * @brief  HAL 锁存的 DMA2D 错误码
 */
uint32_t BSP_DMA2D_Get_Error_Code(void);

/**
 * @brief  查询D-Cache启用状态
 */
bool BSP_DMA2D_Is_Data_Cache_Enabled(void);

/**
 * @brief  异步搬运RGB565像素块
 * @param  destination_stride 不小于width
 * @note   回调前不得修改源缓冲或访问目标缓冲
 */
bool BSP_DMA2D_Copy_RGB565_Async(const uint16_t *source,
                                 uint16_t *destination,
                                 uint16_t width,
                                 uint16_t height,
                                 uint16_t destination_stride,
                                 BSP_DMA2D_Callback_t callback,
                                 void *context);

/**
 * @brief  异步填充RGB565像素块
 * @param  destination_stride 不小于width
 * @note   调用者串行提交，回调前不得访问目标缓冲
 */
bool BSP_DMA2D_Fill_RGB565_Async(uint16_t *destination,
                                 uint16_t width,
                                 uint16_t height,
                                 uint16_t destination_stride,
                                 uint16_t color,
                                 BSP_DMA2D_Callback_t callback,
                                 void *context);

/* ================================================== */

#ifdef __cplusplus
}
#endif

#endif /* BSP_DMA2D_H */
