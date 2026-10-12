/**
 * @file   platform_runtime.c
 * @brief  通过 FreeRTOS 软件定时器维护平台 DWT 64 位时间
 */

/* ================================================== */

#include "rtos/platform_runtime.h"
#include "time/platform_dwt.h"
#include "FreeRTOS.h"
#include "timers.h"
#include "task.h"
#include "stm32h7xx.h"

/* ========================= 配置 ========================= */

#define PLATFORM_DWT_MAINTAIN_PERIOD_MS 1000U

/* ================================================== */

static TimerHandle_t platform_dwt_maintain_timer;
static bool platform_dwt_maintain_started;

/* ================================================== */

/**
 * @brief  在 Timer Service 任务中更新 64 位 DWT 累计值
 */
static void Platform_DWT_Maintain_Callback(TimerHandle_t timer)
{
    (void)timer;
    Platform_DWT_Maintain();
}

/* ================================================== */

/**
 * @brief  初始化用于维护 DWT 周期计数的定时器
 * @note   定时器以 1 s 为周期
 */
bool Platform_Runtime_Init(void)
{
    TickType_t period = pdMS_TO_TICKS(PLATFORM_DWT_MAINTAIN_PERIOD_MS);

    if (platform_dwt_maintain_started)
    {
        return true;
    }
    if (!Platform_DWT_Is_Enabled() || period == 0U ||
        Platform_DWT_Max_Maintain_Interval_Us() <= (PLATFORM_DWT_MAINTAIN_PERIOD_MS * 1000U))
    {
        return false;
    }

    if (platform_dwt_maintain_timer == NULL)
    {
        platform_dwt_maintain_timer = xTimerCreate(
            "PlatformTick",
            period,
            pdTRUE,
            NULL,
            Platform_DWT_Maintain_Callback);
        if (platform_dwt_maintain_timer == NULL)
        {
            return false;
        }
    }

    if (xTimerStart(platform_dwt_maintain_timer, 0U) != pdPASS)
    {
        return false;
    }

    platform_dwt_maintain_started = true;
    return true;
}

bool Platform_Runtime_Is_Init_Context(void)
{
    BaseType_t scheduler_state;

    if ((__get_IPSR() != 0U) || (__get_PRIMASK() != 0U))
    {
        return false;
    }
    scheduler_state = xTaskGetSchedulerState();
    if (scheduler_state == taskSCHEDULER_NOT_STARTED)
    {
        return true;
    }
    return (scheduler_state == taskSCHEDULER_RUNNING) && (__get_BASEPRI() == 0U);
}

bool Platform_Runtime_Is_Task_Context(void)
{
    if ((__get_IPSR() != 0U) || (__get_PRIMASK() != 0U) || (__get_BASEPRI() != 0U))
    {
        return false;
    }
    return xTaskGetSchedulerState() == taskSCHEDULER_RUNNING;
}
