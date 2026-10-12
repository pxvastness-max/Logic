/**
 * @file   log_service.h
 * @brief  任务上下文使用的异步日志接口
 */

#ifndef LOG_SERVICE_H
#define LOG_SERVICE_H

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================== */

#include <stdbool.h>
#include <stdint.h>

/* ================================================== */

/**
 * @brief  创建 UART 运行期同步资源、日志队列与发送任务
 * @retval true 创建成功或日志任务此前已存在
 * @note   启动时串行调用，BSP UART 硬件与全部设备 HAL_Delay 初始化必须先完成
 */
bool Log_Service_Init(void);

/**
 * @brief  不等待复制原始日志字节到消息队列
 * @param  data 非 NULL 的调用者缓冲，至少包含 length 个可读字节
 * @retval true 队列已经保存字节副本
 * @note   只在任务上下文调用，不使用 ISR 版队列 API
 */
bool Log_Write(const void *data, uint16_t length);

/**
 * @brief  格式化一条日志并不等待提交其有效字节
 * @retval true 格式化后非空消息已入队
 * @note   只在任务上下文调用，使用栈上临时缓冲，不保留格式字符串或变参地址
 */
bool Log_Printf(const char *format, ...);

/* ================================================== */

#ifdef __cplusplus
}
#endif

#endif /* LOG_SERVICE_H */
