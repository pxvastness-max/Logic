/**
 * @file   lvgl_display_port.h
 * @brief  LVGL 显示适配
 */

#ifndef LVGL_DISPLAY_PORT_H
#define LVGL_DISPLAY_PORT_H

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================== */

#include "lvgl.h"
#include <stdbool.h>

/* ================================================== */

/**
 * @brief  创建 RGB565 局部渲染显示对象并注册绘制缓冲和刷新回调
 * @note   GUI 任务在 lv_init 和 LCD 初始化后调用
 */
bool LVGL_Display_Port_Init(void);

/**
 * @brief  允许显示刷新回调将 LVGL 像素写入 LCD 帧缓冲
 */
void LVGL_Display_Port_Enable_Update(void);

/**
 * @brief  暂停 LVGL 像素向 LCD 帧缓冲的写入
 * @note   仍确认刷新请求，防止绘制缓冲一直占用
 */
void LVGL_Display_Port_Disable_Update(void);

/* ================================================== */

#ifdef __cplusplus
}
#endif

#endif /* LVGL_DISPLAY_PORT_H */
