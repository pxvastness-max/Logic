/**
 * @file   w9825g6kh.h
 * @brief  W9825G6KH-6 SDRAM设备参数、初始化序列和自检接口
 */

#ifndef W9825G6KH_H
#define W9825G6KH_H

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================== */

#include <stdbool.h>
#include <stdint.h>

/* ================================================== */

#define W9825G6KH_BANK_ADDR  0xC0000000UL             /* SDRAM映射起始地址 */
#define W9825G6KH_SIZE_BYTES (32UL * 1024UL * 1024UL) /* W9825G6KH容量：32MiB */

/* ================================================== */

/**
 * @brief  初始化SDRAM器件并开放内存映射
 */
bool W9825G6KH_Init(void);

/**
 * @brief  执行SDRAM上电自检
 * @note   破坏内存内容，须先于显示和采集缓冲访问
 */
bool W9825G6KH_Selftest(void);

/**
 * @brief  取得自检首个错误地址
 */
uint32_t W9825G6KH_Get_Error_Addr(void);

/* ================================================== */

#ifdef __cplusplus
}
#endif

#endif /* W9825G6KH_H */
