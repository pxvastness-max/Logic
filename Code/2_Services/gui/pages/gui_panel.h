/**
 * @file   gui_panel.h
 * @brief  导航与采集状态显示
 */

#ifndef GUI_PANEL_H
#define GUI_PANEL_H

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================== */

#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>

/* ================================================== */

typedef enum
{
    GUI_PANEL_CAPTURE_IDLE = 0,
    GUI_PANEL_CAPTURE_STARTING,
    GUI_PANEL_CAPTURE_RUNNING
} GUI_Panel_Capture_State_t;

typedef void (*GUI_Panel_Capture_Callback_t)(void *context);

/* ================================================== */

/* 创建右侧导航、页面容器和采集控件，全部对象由 parent 管理 */
bool GUI_Panel_Create(lv_obj_t *parent);

/* 应用在创建界面前绑定采集按钮请求，NULL 关闭请求通知 */
void GUI_Panel_Bind_Capture(GUI_Panel_Capture_Callback_t callback, void *context);

/* GUI 任务用应用传来的状态更新采集进度，数值限制在 0～100 */
void GUI_Panel_Set_Capture_Status(GUI_Panel_Capture_State_t state, uint8_t progress_percent);

/* GUI 任务更新语言和当前配置页，不处理采集状态 */
void GUI_Panel_Process(void);

/* ================================================== */

#ifdef __cplusplus
}
#endif

#endif /* GUI_PANEL_H */
