/**
 * @file   memory_placement.h
 * @brief  Arm Compiler 6 静态对象内存放置属性
 */

#ifndef MEMORY_PLACEMENT_H
#define MEMORY_PLACEMENT_H

/* ================================================== */

/* Cortex-M7 一级数据缓存的缓存行大小 */
#define PLATFORM_CACHE_LINE_SIZE_BYTES 32U

/* Cortex-M7 保守对齐，兼容u64、double的8字节对齐 */
#define PLATFORM_CONSERVATIVE_ALIGNMEN_BYTES 8U

/* ================================================== */

#if defined(__ARMCC_VERSION) && (__ARMCC_VERSION >= 6010050)

    /* 零初始化段，不允许非零初值 */
    #define PLATFORM_MEMORY_ZI_ATTRIBUTE(section_name) \
        __attribute__((section(".bss." section_name)))

    /* 带初值的数据段 */
    #define PLATFORM_MEMORY_RW_ATTRIBUTE(section_name) \
        __attribute__((section(".data." section_name)))

    /* 类型对齐 */
    #define PLATFORM_TYPE_ALIGNMENT_ATTRIBUTE(type) __attribute__((aligned(__alignof__(type))))

    /* 缓存行对齐，用于缓存维护边界或 DMA 共用缓冲 */
    #define PLATFORM_CACHE_ALIGNMENT_ATTRIBUTE \
        __attribute__((aligned(PLATFORM_CACHE_LINE_SIZE_BYTES)))

    /* 保守对齐 */
    #define PLATFORM_CONSERVATIVE_ALIGNMENT_ATTRIBUTE \
        __attribute__((aligned(PLATFORM_CONSERVATIVE_ALIGNMEN_BYTES)))

    /* 自定义对齐 */
    #define PLATFORM_CUSTOM_ALIGNMENT_ATTRIBUTE(alignment) __attribute__((aligned(alignment)))

#else
    #error "memory_placement.h requires Arm Compiler 6"
#endif

/* ================================================== */

/* DTCM：DMA不可访问 */
#define PLATFORM_DTCM_ZI PLATFORM_MEMORY_ZI_ATTRIBUTE("DTCM_DATA")
#define PLATFORM_DTCM_RW PLATFORM_MEMORY_RW_ATTRIBUTE("DTCM_DATA")

/* AXI SRAM：CPU数据区 */
#define PLATFORM_AXI_SRAM_ZI PLATFORM_MEMORY_ZI_ATTRIBUTE("AXI_SRAM_DATA")
#define PLATFORM_AXI_SRAM_RW PLATFORM_MEMORY_RW_ATTRIBUTE("AXI_SRAM_DATA")

/* DMA区：80 KiB，非缓存且不启动清零 */
#define PLATFORM_DMA_NOCACHE_ZI PLATFORM_MEMORY_ZI_ATTRIBUTE("DMA_NOCACHE")

/* SRAM1数据区：48 KiB，非缓存 */
#define PLATFORM_SRAM1_ZI PLATFORM_MEMORY_ZI_ATTRIBUTE("SRAM1_DATA")
#define PLATFORM_SRAM1_RW PLATFORM_MEMORY_RW_ATTRIBUTE("SRAM1_DATA")

/* SRAM2：DMA使用时须维护缓存 */
#define PLATFORM_SRAM2_ZI PLATFORM_MEMORY_ZI_ATTRIBUTE("SRAM2_DATA")
#define PLATFORM_SRAM2_RW PLATFORM_MEMORY_RW_ATTRIBUTE("SRAM2_DATA")

/* SRAM3：缓存规则同SRAM2 */
#define PLATFORM_SRAM3_ZI PLATFORM_MEMORY_ZI_ATTRIBUTE("SRAM3_DATA")
#define PLATFORM_SRAM3_RW PLATFORM_MEMORY_RW_ATTRIBUTE("SRAM3_DATA")

/* SRAM4保留区：4 KiB，不启动清零 */
#define PLATFORM_SRAM4_RETAINED_ZI PLATFORM_MEMORY_ZI_ATTRIBUTE("SRAM4_NOINIT")

/* SRAM4数据区：60 KiB，可缓存 */
#define PLATFORM_SRAM4_ZI PLATFORM_MEMORY_ZI_ATTRIBUTE("SRAM4_DATA")
#define PLATFORM_SRAM4_RW PLATFORM_MEMORY_RW_ATTRIBUTE("SRAM4_DATA")

/* UI区：8 MiB，非缓存且不启动清零 */
#define PLATFORM_SDRAM_UI_ZI PLATFORM_MEMORY_ZI_ATTRIBUTE("UI_DATA")

/* 协议区：2 MiB，非缓存且不启动清零 */
#define PLATFORM_SDRAM_PROTOCOL_ZI PLATFORM_MEMORY_ZI_ATTRIBUTE("PROTOCOL_DATA")

/* 采集区：22 MiB，非缓存且不启动清零 */
#define PLATFORM_SDRAM_LA_ZI PLATFORM_MEMORY_ZI_ATTRIBUTE("LA_DATA")

/* ================================================== */

/* 定义自然对齐的非缓存DMA数组 */
#define PLATFORM_DEFINE_DMA_NOCACHE_ARRAY(name, type, count) \
    PLATFORM_DMA_NOCACHE_ZI PLATFORM_TYPE_ALIGNMENT_ATTRIBUTE(type) static type name[(count)]

/* ================================================== */

#endif /* MEMORY_PLACEMENT_H */
