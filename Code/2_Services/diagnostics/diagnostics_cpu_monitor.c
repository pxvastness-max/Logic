/**
 * @file   diagnostics_cpu_monitor.c
 * @brief  CPU 占用率统计
 */

/* ================================================== */

#include "diagnostics/diagnostics_cpu_monitor.h"
#include "gui_config.h"

/* ================================================== */

#if CPU_DIAGNOSTICS_ENABLE
#include "FreeRTOS.h"
#include "task.h"
#include "log/log_service.h"
#include <string.h>

/* ========================= 配置 ========================= */

    #define TASK_CPU_MONITOR_MAX_TASKS    12U
    #define TASK_CPU_MONITOR_PERIOD_TICKS pdMS_TO_TICKS(1000U)
    #define TASK_CPU_MONITOR_TASK_STACK   512U
    #define TASK_CPU_MONITOR_TASK_PRIO    1U

/* ================================================== */

typedef struct
{
    TaskHandle_t idle_handle;                  /* 对齐空闲任务，避免不同任务的累计计数相减 */
    configRUN_TIME_COUNTER_TYPE idle_runtime;  /* 空闲任务累计运行周期 */
    configRUN_TIME_COUNTER_TYPE total_runtime; /* 平台累计运行周期 */
} Diagnostics_CPU_Monitor_Sample_t;

/* ================================================== */

static TaskStatus_t cpu_monitor_current[TASK_CPU_MONITOR_MAX_TASKS];
static TaskHandle_t cpu_monitor_task_handle;

/* ================================================== */

static void Task_CPU_Monitor_Task(void *parameter);
static bool Task_CPU_Monitor_Read_Sample(Diagnostics_CPU_Monitor_Sample_t *sample);

/* ================================================== */

bool Diagnostics_CPU_Monitor_Init(void)
{
    if (cpu_monitor_task_handle != NULL)
    {
        return true;
    }
    return xTaskCreate(Task_CPU_Monitor_Task,
                       "cpu_mon",
                       TASK_CPU_MONITOR_TASK_STACK,
                       NULL,
                       TASK_CPU_MONITOR_TASK_PRIO,
                       &cpu_monitor_task_handle) == pdPASS;
}

/* ================================================== */

/**
 * @brief  同时读取总运行周期和空闲任务的运行周期
 */
static bool Task_CPU_Monitor_Read_Sample(Diagnostics_CPU_Monitor_Sample_t *sample)
{
    configRUN_TIME_COUNTER_TYPE total_runtime;
    UBaseType_t task_count;
    UBaseType_t index;

    task_count =
        uxTaskGetSystemState(cpu_monitor_current, TASK_CPU_MONITOR_MAX_TASKS, &total_runtime);
    if (task_count == 0U)
    {
        (void)Log_Printf("cpu monitor: runtime snapshot unavailable\r\n");
        return false;
    }
    for (index = 0U; index < task_count; index++)
    {
        if (strcmp(cpu_monitor_current[index].pcTaskName, "IDLE") == 0)
        {
            sample->idle_handle = cpu_monitor_current[index].xHandle;
            sample->idle_runtime = cpu_monitor_current[index].ulRunTimeCounter;
            sample->total_runtime = total_runtime;
            return true;
        }
    }
    (void)Log_Printf("cpu monitor: idle task missing\r\n");
    return false;
}

/**
 * @brief  按相邻采样的非空闲周期计算 CPU 总占用率
 * @note   读取失败时重新记录起点；计数器溢出后仍可计算差值
 */
static void Task_CPU_Monitor_Task(void *parameter)
{
    TickType_t last_wake_time = xTaskGetTickCount();
    Diagnostics_CPU_Monitor_Sample_t previous;
    bool has_previous;

    (void)parameter;
    has_previous = Task_CPU_Monitor_Read_Sample(&previous);
    for (;;)
    {
        Diagnostics_CPU_Monitor_Sample_t current;
        configRUN_TIME_COUNTER_TYPE total_delta;
        configRUN_TIME_COUNTER_TYPE idle_delta;
        uint32_t busy_percent_x100;

        if (xTaskDelayUntil(&last_wake_time, TASK_CPU_MONITOR_PERIOD_TICKS) == pdFALSE)
        {
            last_wake_time = xTaskGetTickCount();
        }
        if (!Task_CPU_Monitor_Read_Sample(&current))
        {
            has_previous = false;
            continue;
        }
        if (!has_previous || current.idle_handle != previous.idle_handle)
        {
            previous = current;
            has_previous = true;
            continue;
        }

        total_delta = (configRUN_TIME_COUNTER_TYPE)(current.total_runtime - previous.total_runtime);
        idle_delta = (configRUN_TIME_COUNTER_TYPE)(current.idle_runtime - previous.idle_runtime);
        previous = current;
        if (total_delta == 0U || idle_delta > total_delta)
        {
            (void)Log_Printf("cpu monitor: invalid runtime interval\r\n");
            continue;
        }
        /* 正常一秒区间约 4.8 亿周期，64 位乘法保留百分比两位小数 */
        busy_percent_x100 =
            (uint32_t)(((uint64_t)(total_delta - idle_delta) * 10000ULL) / total_delta);
        (void)Log_Printf("CPU: %lu.%02lu%%\r\n",
                         (unsigned long)(busy_percent_x100 / 100U),
                         (unsigned long)(busy_percent_x100 % 100U));
    }
}

#else

/* ================================================== */

bool Diagnostics_CPU_Monitor_Init(void)
{
    return true;
}

#endif /* CPU_DIAGNOSTICS_ENABLE */
