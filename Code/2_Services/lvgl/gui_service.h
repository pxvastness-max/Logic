/**
 * @file   gui_service.h
 * @brief  LVGL 与波形界面组合服务
 */

#ifndef GUI_SERVICE_H
#define GUI_SERVICE_H

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================== */

#include <stdbool.h>
#include <stdint.h>

/* ================================================== */

/**
 * @brief  初始化 LVGL 显示、触摸、时基和波形视图
 * @note   GUI 任务调用，成功后绑定应用请求处理器
 */
bool GUI_Service_Init(void);

/**
 * @brief  创建控制页面，应用波形配置并启动帧准备
 * @note   应用请求处理器绑定后调用
 */
bool GUI_Service_Start(void);

/**
 * @brief  用波形任务送来的最新数据更新界面
 */
void GUI_Service_Process_Updates(void);

/**
 * @brief  执行 LVGL 定时处理并记录渲染耗时
 * @note   返回建议等待时间，单位毫秒
 */
uint32_t GUI_Service_Process_Timers(void);

/* ================================================== */

#ifdef __cplusplus
}
#endif

#endif /* GUI_SERVICE_H */
