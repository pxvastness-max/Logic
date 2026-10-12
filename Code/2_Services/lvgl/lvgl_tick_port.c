/**
 * @file   lvgl_tick_port.c
 * @brief  LVGL 毫秒时基适配
 */

/* ================================================== */

#include "lvgl/lvgl_tick_port.h"
#include "timer/bsp_timer.h"
#include "lvgl.h"

/* ================================================== */

static void LVGL_Tick_Port_Elapsed(void *context);

/* ================================================== */

static void LVGL_Tick_Port_Elapsed(void *context)
{
    (void)context;
    lv_tick_inc(1U);
}

/* ================================================== */

bool LVGL_Tick_Port_Init(void)
{
    BSP_Timer_Tick_Set_Callback(LVGL_Tick_Port_Elapsed, NULL);
    return BSP_Timer_Tick_Start();
}
