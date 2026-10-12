/**
 * @file   app_gui.c
 * @brief  GUI 应用控制器调度
 */

/* ================================================== */

#include "gui/app_gui.h"
#include "capture/app_capture.h"
#include "gui/app_file.h"
#include "gui/app_protocol.h"
#include "gui/pages/gui_panel.h"

/* ================================================== */

/**
 * @brief  初始化应用控制器并绑定界面事件
 */
void App_GUI_Init(void)
{
    App_Protocol_Init();
    App_File_Init();
    App_Capture_Init();
}

/**
 * @brief  处理后台结果并更新应用状态
 * @note   先处理文件结果，再处理协议和采集状态
 */
void App_GUI_Process(void)
{
    App_File_Process();
    App_Protocol_Process();
    GUI_Panel_Process();
    App_Capture_Process();
}
