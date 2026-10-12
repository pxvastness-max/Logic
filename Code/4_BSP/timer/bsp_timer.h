/**
 * @file   bsp_timer.h
 * @brief  硬件同步捕获定时器和独立毫秒周期事件
 */

#ifndef BSP_TIMER_H
#define BSP_TIMER_H

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================== */

#include "stm32h7xx_hal.h"
#include <stdbool.h>

/* ================================================== */

/* TIM7 每 1 ms 在中断上下文调用一次；回调只能执行非阻塞操作 */
typedef void (*BSP_Timer_Tick_Callback_t)(void *context);

/* ================================================== */

/* 采集 BSP 的寄存器操作需要这些句柄，上层使用 la_ic.h */
extern TIM_HandleTypeDef htim1;
extern TIM_HandleTypeDef htim2;
extern TIM_HandleTypeDef htim5;

/* ================================================== */

/**
 * @brief  初始化 TIM1 同步触发和 TIM2/TIM5 六路双边沿捕获，不启动采集
 */
bool BSP_Timer_Capture_Init(void);

/**
 * @brief  设置六路捕获滤波
 * @param  input_filter ICxF档位0~15
 * @note   只在计数、捕获和DMA请求关闭时更新
 */
bool BSP_Timer_Capture_Set_Filter(uint8_t input_filter);

/**
 * @brief  初始化 TIM7 为 1 ms 周期，暂不启动，失败返回 false
 */
bool BSP_Timer_Tick_Init(void);

/**
 * @brief  绑定毫秒周期回调
 * @note   回调在TIM7中断中执行，禁止阻塞
 */
void BSP_Timer_Tick_Set_Callback(BSP_Timer_Tick_Callback_t callback, void *context);

/**
 * @brief  启动 TIM7 周期中断，失败返回 false
 */
bool BSP_Timer_Tick_Start(void);

/**
 * @brief  服务 TIM2 采集截止比较中断
 */
void BSP_Timer_Capture_IRQHandler(void);

/**
 * @brief  处理 TIM7 中断，并通知已绑定的回调
 */
void BSP_Timer_Tick_IRQHandler(void);

/* ================================================== */

#ifdef __cplusplus
}
#endif

#endif /* BSP_TIMER_H */
