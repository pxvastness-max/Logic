/**
 * @file   diagnostics_cpu_monitor.h
 * @brief  CPU 占用率统计
 */

#ifndef DIAGNOSTICS_CPU_MONITOR_H
#define DIAGNOSTICS_CPU_MONITOR_H

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================== */

#include <stdbool.h>

/* ================================================== */

/**
 * @brief  按配置创建每秒输出总占用率的低优先级任务
 * @note   调度器启动前调用，关闭统计时直接成功
 */
bool Diagnostics_CPU_Monitor_Init(void);

/* ================================================== */

#ifdef __cplusplus
}
#endif

#endif /* DIAGNOSTICS_CPU_MONITOR_H */
