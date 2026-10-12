/**
 * @file   platform_dwt.h
 * @brief  DWT 周期计数和时间转换
 */

#ifndef PLATFORM_DWT_H
#define PLATFORM_DWT_H

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================== */

#include <stdbool.h>
#include <stdint.h>

/* ================================================== */

/**
 * @brief  初始化 Cortex-M DWT 的 CYCCNT 时基
 */
bool Platform_DWT_Init(void);

/**
 * @brief  维护64位周期累计值
 * @note   相邻维护间隔须小于32位计数回绕周期
 */
void Platform_DWT_Maintain(void);

/**
 * @brief  DWT 周期计数器是否已经启用并完成软件时基初始化
 */
bool Platform_DWT_Is_Enabled(void);

/**
 * @brief  获取当前 32 位 CPU 周期计数值
 */
uint32_t Platform_DWT_Cycles(void);

/**
 * @brief  计算自 start_cycle 起经过的周期数
 */
uint32_t Platform_DWT_Elapsed(uint32_t start_cycle);

/**
 * @brief  读取自 Platform_DWT_Init() 起累计的 64 位 CPU 周期数
 */
uint64_t Platform_DWT_Cycles64(void);

/**
 * @brief  获取自 Platform_DWT_Init() 起的微秒时间
 */
uint64_t Platform_DWT_Time_Us(void);

/**
 * @brief  获取自 Platform_DWT_Init() 起的毫秒时间
 */
uint64_t Platform_DWT_Time_Ms(void);

/**
 * @brief  将周期数换算为微秒，使用 Platform 配置中的 DWT 时钟频率
 */
uint32_t Platform_DWT_Cycles_To_Us(uint32_t cycles);

/**
 * @brief  返回在当前配置时钟下，32 位计数器回绕一次所需的最长维护间隔
 */
uint32_t Platform_DWT_Max_Maintain_Interval_Us(void);

/* ================================================== */

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_DWT_H */
