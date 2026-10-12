/**
 * @file   platform_runtime.h
 * @brief  FreeRTOS 平台周期维护接口
 */

#ifndef PLATFORM_RUNTIME_H
#define PLATFORM_RUNTIME_H

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================== */

#include <stdbool.h>

/* ================================================== */

/**
 * @brief  创建并启动每秒维护 DWT 64 位累计值的软件定时器
 * @note   在平台时钟及 DWT 初始化后、调度器启动前调用
 */
bool Platform_Runtime_Init(void);

/**
 * @brief  检查初始化调用上下文
 * @note   调度器启动前允许BASEPRI非零
 */
bool Platform_Runtime_Is_Init_Context(void);

/**
 * @brief  检查普通任务调用上下文
 */
bool Platform_Runtime_Is_Task_Context(void);

/* ================================================== */

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_RUNTIME_H */
