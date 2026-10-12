/**
 * @file   platform_clock.h
 * @brief  STM32H743 系统时钟与片上定时器时钟查询
 */

#ifndef PLATFORM_CLOCK_H
#define PLATFORM_CLOCK_H

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================== */

#include <stdbool.h>
#include <stdint.h>

/* ================================================== */

/**
 * @brief  使用板载 25 MHz HSE 配置 480 MHz CPU、240 MHz AHB 和 120 MHz APB
 */
bool Platform_Clock_Init(void);

/**
 * @brief  返回 APB1 上 TIM2/TIM5/TIM6/TIM7 的内核时钟（Hz）
 */
uint32_t Platform_Clock_Apb1_Timer_Hz(void);

/* ================================================== */

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_CLOCK_H */
