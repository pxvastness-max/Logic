/**
 * @file   bsp_init.h
 * @brief  BSP 层统一初始化入口
 */

#ifndef BSP_INIT_H
#define BSP_INIT_H

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================== */

#include <stdbool.h>

/* ================================================== */

/**
 * @brief  初始化六路 LA 捕获硬件、TIM7、DMA2D 和调试串口
 * @note   调度器启动前调用一次
 */
bool BSP_Init(void);

/* ================================================== */

#ifdef __cplusplus
}
#endif

#endif /* BSP_INIT_H */
