/**
 * @file   app_init.h
 * @brief  应用初始化与任务调度
 */

#ifndef APP_INIT_H
#define APP_INIT_H

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================== */

#include <stdbool.h>

/* ================================================== */

/**
 * @brief  初始化平台、外设和各项服务，再创建应用任务
 * @note   调度器启动前调用，显示对象在 GUI 任务中创建
 */
bool App_Init(void);

/**
 * @brief  启动调度器，异常返回时进入统一故障停机
 * @note   只在 App_Init 成功后调用，无正常返回
 */
void App_Run(void);

/* ================================================== */

#ifdef __cplusplus
}
#endif

#endif /* APP_INIT_H */
