/**
 * @file   gui_service.c
 * @brief  LVGL 与波形界面组合服务
 */

/* ================================================== */

#include "lvgl/gui_service.h"
#include "lvgl/lvgl_display_port.h"
#include "lvgl/lvgl_input_port.h"
#include "lvgl/lvgl_tick_port.h"
#include "gui/gesture/gui_wave_gesture.h"
#include "gui/pages/gui_panel.h"
#include "waveform/wave_service.h"
#include "diagnostics/display_diagnostics.h"
#include "log/log_service.h"
#include "time/platform_dwt.h"
#include "lvgl.h"

/* ================================================== */

bool GUI_Service_Init(void)
{
    lv_init();
    if (!LVGL_Display_Port_Init())
    {
        return false;
    }
    if (!LVGL_Input_Port_Init())
    {
        (void)Log_Printf("ui: touch input unavailable\r\n");
    }
    if (!LVGL_Tick_Port_Init() || !Wave_Init(lv_screen_active()))
    {
        return false;
    }

    LVGL_Input_Port_Set_Gesture_Callback(GUI_Wave_Gesture_Handle, NULL);
    return true;
}

bool GUI_Service_Start(void)
{
    if (!GUI_Panel_Create(lv_screen_active()) || !Wave_Apply_Configuration())
    {
        return false;
    }

    Wave_Display_Task_Start();
    return true;
}

void GUI_Service_Process_Updates(void)
{
    Wave_Process_GUI_Updates();
}

uint32_t GUI_Service_Process_Timers(void)
{
    uint32_t cycle_start = Platform_DWT_Cycles();
    uint32_t flush_count = Display_Diagnostics_Get_Flush_Count();
    uint32_t delay_ms = lv_timer_handler();

    Display_Diagnostics_Record_LVGL_Handler(Platform_DWT_Elapsed(cycle_start),
                                            Display_Diagnostics_Get_Flush_Count() != flush_count);
    return delay_ms;
}
