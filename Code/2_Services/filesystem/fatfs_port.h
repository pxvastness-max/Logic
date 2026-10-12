/**
 * @file   fatfs_port.h
 * @brief  FatFs平台运行上下文接口
 */

#ifndef FATFS_PORT_H
#define FATFS_PORT_H

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================== */

#include <stdint.h>

/* ================================================== */

/**
 * @brief  检查是否允许初始化FatFs平台资源
 * @retval 1-满足初始化条件
 * @note   支持调度器启动前和正常任务阶段
 */
uint8_t FileSystem_Port_Is_Init_Context(void);

/**
 * @brief  检查是否处于允许阻塞的FatFs任务上下文
 * @retval 1-满足运行条件
 * @note   要求调度器运行、非中断上下文且未通过PRIMASK或BASEPRI屏蔽中断
 */
uint8_t FileSystem_Port_Is_Task_Context(void);

/* ================================================== */

#ifdef __cplusplus
}
#endif

#endif /* FATFS_PORT_H */
