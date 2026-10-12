/**
 * @file   hardware_service.h
 * @brief  内存、显示与触摸设备初始化
 */

#ifndef HARDWARE_SERVICE_H
#define HARDWARE_SERVICE_H

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================== */

#include <stdbool.h>

/* ================================================== */

/**
 * @brief  初始化并自检 SDRAM，再初始化 LCD 和可选触摸控制器
 * @note   SDRAM 自检破坏数据，须先于上层缓冲访问
 */
bool Hardware_Service_Init(void);

/* ================================================== */

#ifdef __cplusplus
}
#endif

#endif /* HARDWARE_SERVICE_H */
