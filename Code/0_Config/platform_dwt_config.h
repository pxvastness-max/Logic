/* platform_dwt_config.h：DWT 周期计数器的芯片与频率适配 */

#ifndef PLATFORM_DWT_CONFIG_H
#define PLATFORM_DWT_CONFIG_H

#include "platform_config.h"

/* ========================= 配置 ========================= */

/* 提供 CoreDebug、DWT、PRIMASK 以及内核屏障等 CMSIS 定义 */
#define PLATFORM_DWT_CMSIS_HEADER "stm32h7xx.h"

/* 单位为 Hz，与 CYCCNT 的实际内核时钟保持一致，用于周期到微秒和毫秒的换算 */
#define PLATFORM_DWT_CLOCK_HZ CPU_CYCLES_HZ

#endif /* PLATFORM_DWT_CONFIG_H */
