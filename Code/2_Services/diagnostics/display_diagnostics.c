/**
 * @file   display_diagnostics.c
 * @brief  显示统计与 LTDC 诊断
 */

/* ================================================== */

#include "diagnostics/display_diagnostics.h"
#include "gui_config.h"
#include "FreeRTOS.h"
#include "task.h"

/* ================================================== */

#if DISPLAY_DIAGNOSTICS_ENABLE
#include "display/bsp_dma2d.h"
#include "display/bsp_ltdc_lcd.h"
#include "log/log_service.h"
#endif

/* ========================= 配置 ========================= */

#if DISPLAY_DIAGNOSTICS_ENABLE
    #define DISPLAY_DIAGNOSTICS_STACK_DEPTH 512U
    #define DISPLAY_DIAGNOSTICS_PRIORITY    1U
    #define DISPLAY_DIAGNOSTICS_PERIOD      pdMS_TO_TICKS(5000U)
#endif

/* ================================================== */

static uint32_t display_diagnostics_flush_count;
static Display_Render_Stats_t display_diagnostics_render_stats;
#if DISPLAY_DIAGNOSTICS_ENABLE
static TaskHandle_t display_diagnostics_task;
#endif

/* ================================================== */

#if DISPLAY_DIAGNOSTICS_ENABLE
static void Display_Diagnostics_Task(void *parameter);
#endif

/* ================================================== */

uint32_t Display_Diagnostics_Get_Flush_Count(void)
{
    uint32_t flush_count;

    taskENTER_CRITICAL();
    flush_count = display_diagnostics_flush_count;
    taskEXIT_CRITICAL();
    return flush_count;
}

void Display_Diagnostics_Record_LVGL_Handler(uint32_t cycles, bool rendered)
{
    taskENTER_CRITICAL();
    display_diagnostics_render_stats.lvgl_handler_cycles += cycles;
    display_diagnostics_render_stats.lvgl_handler_calls++;
    if (rendered)
    {
        display_diagnostics_render_stats.rendered_handler_cycles += cycles;
        display_diagnostics_render_stats.rendered_handler_calls++;
    }
    taskEXIT_CRITICAL();
}

void Display_Diagnostics_Record_Flush(uint32_t pixels)
{
    taskENTER_CRITICAL();
    display_diagnostics_flush_count++;
    display_diagnostics_render_stats.flush_pixels += pixels;
    display_diagnostics_render_stats.flush_calls++;
    taskEXIT_CRITICAL();
}

void Display_Diagnostics_Get_And_Reset_Render_Stats(Display_Render_Stats_t *stats)
{
    if (stats == NULL)
    {
        return;
    }
    taskENTER_CRITICAL();
    *stats = display_diagnostics_render_stats;
    display_diagnostics_render_stats = (Display_Render_Stats_t){0};
    taskEXIT_CRITICAL();
}

#if DISPLAY_DIAGNOSTICS_ENABLE

/* ================================================== */

/**
 * @brief  周期清理渲染统计窗口，并在 LTDC 错误计数变化后打印故障现场
 */
static void Display_Diagnostics_Task(void *parameter)
{
    uint32_t last_fifo_underrun_count = 0U;
    uint32_t last_transfer_error_count = 0U;
    TickType_t last_wake_time = xTaskGetTickCount();

    (void)parameter;
    for (;;)
    {
        Display_Render_Stats_t render_stats;
        BSP_LTDC_Error_Diagnostic_t diagnostic;

        xTaskDelayUntil(&last_wake_time, DISPLAY_DIAGNOSTICS_PERIOD);
        Display_Diagnostics_Get_And_Reset_Render_Stats(&render_stats);

        if (!BSP_LTDC_LCD_Get_Error_Diagnostic(&diagnostic))
        {
            continue;
        }
        /* 中断只锁存故障现场；变化后的错误在低优先级任务中输出 */
        if (diagnostic.fifo_underrun_count != last_fifo_underrun_count ||
            diagnostic.transfer_error_count != last_transfer_error_count)
        {
            (void)Log_Printf(
                "ltdc: fu=%lu te=%lu hal=%08lX dma=%08lX flags=%02lX pos=%08lX frame=%lu\r\n",
                (unsigned long)diagnostic.fifo_underrun_count,
                (unsigned long)diagnostic.transfer_error_count,
                (unsigned long)diagnostic.hal_error_code,
                (unsigned long)BSP_DMA2D_Get_Error_Code(),
                (unsigned long)diagnostic.last_error_flags,
                (unsigned long)diagnostic.last_error_position,
                (unsigned long)diagnostic.last_error_frame_count);
        }
        last_fifo_underrun_count = diagnostic.fifo_underrun_count;
        last_transfer_error_count = diagnostic.transfer_error_count;
    }
}

#endif

/* ================================================== */

bool Display_Diagnostics_Init(void)
{
#if DISPLAY_DIAGNOSTICS_ENABLE
    if (display_diagnostics_task != NULL)
    {
        return true;
    }
    return xTaskCreate(Display_Diagnostics_Task,
                       "display_diag",
                       DISPLAY_DIAGNOSTICS_STACK_DEPTH,
                       NULL,
                       DISPLAY_DIAGNOSTICS_PRIORITY,
                       &display_diagnostics_task) == pdPASS;
#else
    return true;
#endif
}
