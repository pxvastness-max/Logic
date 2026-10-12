/**
 * @file   bsp_fmc_sdram.h
 * @brief  阿波罗 H743板级FMC SDRAM总线接口
 */

#ifndef BSP_FMC_SDRAM_H
#define BSP_FMC_SDRAM_H

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================== */

#include "stm32h7xx_hal.h"
#include <stdbool.h>
#include <stdint.h>

/* ========================= 配置 ========================= */

#define BSP_FMC_SDRAM_BANK_ADDR 0xC0000000UL /* FMC SDRAM Bank1默认映射起始地址 */

/* ================================================== */

typedef struct
{
    uint32_t column_bits;     /* 列地址位数，取 HAL 的 FMC_SDRAM_COLUMN_BITS_NUM_* 常量 */
    uint32_t row_bits;        /* 行地址位数，取 HAL 的 FMC_SDRAM_ROW_BITS_NUM_* 常量 */
    uint32_t data_width;      /* 数据总线宽度，必须与 PCB 布线一致 */
    uint32_t internal_banks;  /* SDRAM 芯片内部 Bank 数量 */
    uint32_t cas_latency;     /* 读命令到有效数据的 CAS 等待周期 */
    uint32_t clock_period;    /* SDRAM 时钟相对 FMC 内核时钟的分频常量 */
    uint32_t read_pipe_delay; /* FMC 读数据流水线延迟常量 */
} BSP_FMC_SDRAM_Config_t;

/* ================================================== */

extern SDRAM_HandleTypeDef bsp_fmc_sdram_handle;

/* ================================================== */

/**
 * @brief  初始化FMC SDRAM控制器
 * @param  config 非空器件配置
 * @param  timing 非空时序配置
 * @note   不执行器件上电序列
 */
bool BSP_FMC_SDRAM_Init(const BSP_FMC_SDRAM_Config_t *config,
                        const FMC_SDRAM_TimingTypeDef *timing);

/**
 * @brief  发送SDRAM命令
 */
bool BSP_FMC_SDRAM_Send_Command(FMC_SDRAM_CommandTypeDef *command);

/**
 * @brief  装载 SDRAM 自动刷新计数值
 */
bool BSP_FMC_SDRAM_Set_Refresh(uint32_t refresh_count);

/* ================================================== */

#ifdef __cplusplus
}
#endif

#endif /* BSP_FMC_SDRAM_H */
