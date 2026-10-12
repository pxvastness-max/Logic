/**
 * @file   app_gui.h
 * @brief  GUI 应用控制器调度
 */

#ifndef APP_GUI_H
#define APP_GUI_H

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================== */

/* GUI 就绪后、创建页面前，绑定各应用流程 */
void App_GUI_Init(void);

/* 在唯一 GUI 任务中调度文件、协议、页面与采集流程 */
void App_GUI_Process(void);

/* ================================================== */

#ifdef __cplusplus
}
#endif

#endif /* APP_GUI_H */
