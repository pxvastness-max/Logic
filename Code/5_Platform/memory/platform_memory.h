/**
 * @file   platform_memory.h
 * @brief  Cortex-M7 MPU 与 L1 Cache 配置
 */

#ifndef PLATFORM_MEMORY_H
#define PLATFORM_MEMORY_H

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================== */

#include <stdint.h>

/* ================================================== */

/**
 * @brief  按链接布局配置片内存储器 MPU 属性和外部地址保护
 */
void Platform_Memory_Bootstrap(void);

/**
 * @brief  在片内 MPU 区域配置完成后开启 I-Cache 和 D-Cache
 */
void Platform_Memory_Enable_Cache(void);

/**
 * @brief  配置SDRAM内存属性
 * @note   只在SDRAM硬件初始化成功后调用
 */
void Platform_SDRAM_MPU_Config(uint32_t base_address);

/**
 * @brief  等待内存访问完成
 */
void Platform_Memory_Barrier(void);

/**
 * @brief  将脏缓存行提交到主存
 * @note   维护范围扩展到32字节缓存行
 */
void Platform_Memory_Clean_Cache(const void *address, uint32_t byte_count);

/**
 * @brief  丢弃缓冲对应的缓存行
 * @note   DMA完成后调用，完整缓存行不得含其他脏数据
 */
void Platform_Memory_Invalidate_Cache(void *address, uint32_t byte_count);

/* ================================================== */

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_MEMORY_H */
