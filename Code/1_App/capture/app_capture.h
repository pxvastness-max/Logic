/**
 * @file   app_capture.h
 * @brief  采集流程与状态管理
 */

#ifndef APP_CAPTURE_H
#define APP_CAPTURE_H

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================== */

/* 在 GUI 任务创建界面前绑定采集按钮请求 */
void App_Capture_Init(void);

/* 每次 GUI 更新时，检查启动等待、采集进度和结束状态 */
void App_Capture_Process(void);

/* ================================================== */

#ifdef __cplusplus
}
#endif

#endif /* APP_CAPTURE_H */
