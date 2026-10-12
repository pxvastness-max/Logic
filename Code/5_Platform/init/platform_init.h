/**
 * @file   platform_init.h
 * @brief  STM32H743 基础启动接口
 */

#ifndef PLATFORM_INIT_H
#define PLATFORM_INIT_H

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================== */

#include <stdbool.h>

/* ================================================== */

/**
 * @brief  配置启动 MPU、Cache、HAL、系统时钟和 DWT 软件时间源
 */
bool Platform_Init(void);

/* ================================================== */

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_INIT_H */
