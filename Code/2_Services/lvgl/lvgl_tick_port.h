/**
 * @file   lvgl_tick_port.h
 * @brief  LVGL 毫秒时基适配
 */

#ifndef LVGL_TICK_PORT_H
#define LVGL_TICK_PORT_H

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================== */

#include <stdbool.h>

/* ================================================== */

/**
 * @brief  将板级毫秒周期事件接入 LVGL 并启动时基
 * @note   BSP 和 lv_init 就绪后调用，TIM7 中断只更新 tick 计数
 */
bool LVGL_Tick_Port_Init(void);

/* ================================================== */

#ifdef __cplusplus
}
#endif

#endif /* LVGL_TICK_PORT_H */
