/**
 * @file   platform_system.h
 * @brief  芯片公共 HAL 支持与不可恢复错误处理
 */

#ifndef PLATFORM_SYSTEM_H
#define PLATFORM_SYSTEM_H

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================== */

/**
 * @brief  关闭全部可屏蔽中断并停机，保留现场供调试器检查
 */
void Platform_Fatal_Error(void);

/* ================================================== */

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_SYSTEM_H */
