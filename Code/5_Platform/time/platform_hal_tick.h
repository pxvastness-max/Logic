/**
 * @file   platform_hal_tick.h
 * @brief  TIM6 HAL 毫秒时基的中断适配
 */

#ifndef PLATFORM_HAL_TICK_H
#define PLATFORM_HAL_TICK_H

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================== */

/**
 * @brief  处理 TIM6 更新中断；仅由 TIM6_DAC_IRQHandler 调用
 */
void Platform_HAL_Tick_IRQHandler(void);

/* ================================================== */

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_HAL_TICK_H */
