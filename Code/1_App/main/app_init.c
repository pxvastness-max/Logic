/**
 * @file   app_init.c
 * @brief  应用初始化与任务调度
 */

/* ================================================== */

#include "main/app_init.h"
#include "gui/app_gui.h"
#include "hardware/hardware_service.h"
#include "lvgl/gui_service.h"
#include "log/log_service.h"
#include "touch/touch_service.h"
#include "waveform/wave_service.h"
#include "diagnostics/display_diagnostics.h"
#include "diagnostics/diagnostics_cpu_monitor.h"
#include "la/la_service.h"
#include "protocol/protocol_service.h"
#include "filesystem/file_manager.h"
#include "init/bsp_init.h"
#include "init/platform_init.h"
#include "rtos/platform_runtime.h"
#include "system/platform_system.h"
#include "FreeRTOS.h"
#include "task.h"

/* ========================= 配置 ========================= */

#define APP_GUI_TASK_STACK_DEPTH 2048U
#define APP_GUI_TASK_PRIORITY    2U
#define APP_GUI_MAX_WAIT_MS      10U

/* ================================================== */

static void App_GUI_Task(void *parameter);

/* ================================================== */

bool App_Init(void)
{
    TaskHandle_t gui_task = NULL;

    if (!Platform_Init() || !BSP_Init() || !Hardware_Service_Init() || !Platform_Runtime_Init())
    {
        return false;
    }
    if (!Log_Service_Init() || (LA_Service_Init() != 0U) || (Protocol_Service_Init() != 0U) ||
        !File_Manager_Init() || !Touch_Service_Init() || !Wave_Display_Task_Create() ||
        !Display_Diagnostics_Init() || !Diagnostics_CPU_Monitor_Init())
    {
        return false;
    }
    if (xTaskCreate(App_GUI_Task,
                    "lvgl",
                    APP_GUI_TASK_STACK_DEPTH,
                    NULL,
                    APP_GUI_TASK_PRIORITY,
                    &gui_task) != pdPASS)
    {
        return false;
    }

    Touch_Service_Bind_GUI_Task(gui_task);
    return true;
}

void App_Run(void)
{
    vTaskStartScheduler();
    Platform_Fatal_Error();
}

/* ================================================== */

/**
 * @brief  初始化 GUI，随后处理采集、文件和协议任务
 * @note   唯一 LVGL 调用任务，触摸通知可提前唤醒
 */
static void App_GUI_Task(void *parameter)
{
    (void)parameter;

    if (!GUI_Service_Init())
    {
        Platform_Fatal_Error();
    }
    App_GUI_Init();
    if (!GUI_Service_Start())
    {
        Platform_Fatal_Error();
    }

    for (;;)
    {
        uint32_t delay_ms;
        TickType_t wait_ticks;

        GUI_Service_Process_Updates();
        App_GUI_Process();
        delay_ms = GUI_Service_Process_Timers();
        if (delay_ms < 1U)
        {
            delay_ms = 1U;
        }
        else if (delay_ms > APP_GUI_MAX_WAIT_MS)
        {
            delay_ms = APP_GUI_MAX_WAIT_MS;
        }

        /* 毫秒换算后至少等待一个 RTOS tick，避免低 tick 频率下忙轮询 */
        wait_ticks = pdMS_TO_TICKS(delay_ms);
        (void)ulTaskNotifyTake(pdTRUE, wait_ticks == 0U ? 1U : wait_ticks);
    }
}
