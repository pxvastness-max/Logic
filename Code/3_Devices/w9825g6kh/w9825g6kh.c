/**
 * @file   w9825g6kh.c
 * @brief  W9825G6KH-6 SDRAM设备初始化序列、刷新和自检
 */

/* ================================================== */

#include "w9825g6kh/w9825g6kh.h"
#include "memory/bsp_fmc_sdram.h"
#include "memory/platform_memory.h"

/* ================================================== */

#define W9825G6KH_HCLK3_HZ  240000000UL                                /* FMC内核时钟频率 */
#define W9825G6KH_SDCLK_DIV 3U                                         /* SDRAM时钟分频系数 */
#define W9825G6KH_SDCLK_HZ  (W9825G6KH_HCLK3_HZ / W9825G6KH_SDCLK_DIV) /* SDRAM工作时钟 */
/* 把数据手册中的纳秒时序向上取整换算为SDCLK周期 */
#define W9825G6KH_NS_TO_CYCLES(ns) \
    ((((uint32_t)(ns) * (W9825G6KH_SDCLK_HZ / 1000000UL)) + 999UL) / 1000UL)
#define W9825G6KH_T_MRD_CYCLES           2U                          /* 模式寄存器命令延时 */
#define W9825G6KH_T_XSR_CYCLES           W9825G6KH_NS_TO_CYCLES(72U) /* 退出自刷新延时 */
#define W9825G6KH_T_RAS_CYCLES           W9825G6KH_NS_TO_CYCLES(42U) /* 行有效保持时间 */
#define W9825G6KH_T_RC_CYCLES            W9825G6KH_NS_TO_CYCLES(60U) /* 行周期时间 */
#define W9825G6KH_T_WR_CYCLES            W9825G6KH_NS_TO_CYCLES(14U) /* 写恢复时间 */
#define W9825G6KH_T_RP_CYCLES            W9825G6KH_NS_TO_CYCLES(18U) /* 预充电时间 */
#define W9825G6KH_T_RCD_CYCLES           W9825G6KH_NS_TO_CYCLES(18U) /* 行到列延时 */
#define W9825G6KH_CAS_LATENCY            FMC_SDRAM_CAS_LATENCY_2     /* CAS延迟为2个SDCLK */
#define W9825G6KH_MODEREG_CAS_LATENCY    ((uint32_t)W9825G6KH_CAS_LATENCY >> 3) /* MRS的CAS位域 */
#define W9825G6KH_MODEREG_BURST_LENGTH_1 ((uint32_t)0x0000U)                    /* MRS突发长度1 */
#define W9825G6KH_MODEREG_BURST_TYPE_SEQUENTIAL   ((uint32_t)0x0000U)           /* MRS顺序突发 */
#define W9825G6KH_MODEREG_OPERATING_MODE_STANDARD ((uint32_t)0x0000U)           /* MRS标准模式 */
#define W9825G6KH_MODEREG_WRITEBURST_MODE_SINGLE  ((uint32_t)0x0200U)           /* MRS单次写突发 */
#define W9825G6KH_CLOCK_PERIOD                    FMC_SDRAM_CLOCK_PERIOD_3 /* FMC三分频输出SDCLK */
#define W9825G6KH_READ_PIPE_DELAY                 FMC_SDRAM_RPIPE_DELAY_1  /* FMC读取管道延迟 */
#define W9825G6KH_ROWS                            8192U                    /* SDRAM刷新行数 */
#define W9825G6KH_REFRESH_PERIOD_MS               64U                      /* 全部行最长刷新周期 */
/* FMC刷新计数器值 */
#define W9825G6KH_REFRESH_COUNT \
    ((uint32_t)((((uint64_t)W9825G6KH_REFRESH_PERIOD_MS * (uint64_t)W9825G6KH_SDCLK_HZ) / \
                 1000ULL) / \
                (uint64_t)W9825G6KH_ROWS) - \
     20ULL)
#define W9825G6KH_SELFTEST_STRIDE 65536UL /* 地址扫描步长 */
#define W9825G6KH_SELFTEST_BLOCK  65536UL /* 连续棋盘格测试长度 */

/* ================================================== */

static uint32_t w9825g6kh_error_addr; /* 最近一次自检首先出错的绝对地址 */

/* ================================================== */

/**
 * @brief  按器件要求执行时钟使能、预充电、自动刷新和模式寄存器装载
 */
static bool W9825G6KH_Power_Up_Sequence(void)
{
    FMC_SDRAM_CommandTypeDef command = {0};
    uint32_t mode;

    command.CommandTarget = FMC_SDRAM_CMD_TARGET_BANK1;
    command.AutoRefreshNumber = 1U;
    command.ModeRegisterDefinition = 0U;

    command.CommandMode = FMC_SDRAM_CMD_CLK_ENABLE;
    if (!BSP_FMC_SDRAM_Send_Command(&command))
    {
        return false;
    }
    HAL_Delay(1U);

    command.CommandMode = FMC_SDRAM_CMD_PALL;
    if (!BSP_FMC_SDRAM_Send_Command(&command))
    {
        return false;
    }

    command.CommandMode = FMC_SDRAM_CMD_AUTOREFRESH_MODE;
    command.AutoRefreshNumber = 8U;
    if (!BSP_FMC_SDRAM_Send_Command(&command))
    {
        return false;
    }

    mode = W9825G6KH_MODEREG_BURST_LENGTH_1 | W9825G6KH_MODEREG_BURST_TYPE_SEQUENTIAL |
           W9825G6KH_MODEREG_CAS_LATENCY | W9825G6KH_MODEREG_OPERATING_MODE_STANDARD |
           W9825G6KH_MODEREG_WRITEBURST_MODE_SINGLE;
    command.CommandMode = FMC_SDRAM_CMD_LOAD_MODE;
    command.AutoRefreshNumber = 1U;
    command.ModeRegisterDefinition = mode;
    return BSP_FMC_SDRAM_Send_Command(&command);
}

/**
 * @brief  初始化SDRAM器件并开放内存映射
 */
bool W9825G6KH_Init(void)
{
    BSP_FMC_SDRAM_Config_t config = {0};
    FMC_SDRAM_TimingTypeDef timing = {0};

    config.column_bits = FMC_SDRAM_COLUMN_BITS_NUM_9;
    config.row_bits = FMC_SDRAM_ROW_BITS_NUM_13;
    config.data_width = FMC_SDRAM_MEM_BUS_WIDTH_16;
    config.internal_banks = FMC_SDRAM_INTERN_BANKS_NUM_4;
    config.cas_latency = W9825G6KH_CAS_LATENCY;
    config.clock_period = W9825G6KH_CLOCK_PERIOD;
    config.read_pipe_delay = W9825G6KH_READ_PIPE_DELAY;

    timing.LoadToActiveDelay = W9825G6KH_T_MRD_CYCLES;
    timing.ExitSelfRefreshDelay = W9825G6KH_T_XSR_CYCLES;
    timing.SelfRefreshTime = W9825G6KH_T_RAS_CYCLES;
    timing.RowCycleDelay = W9825G6KH_T_RC_CYCLES;
    timing.WriteRecoveryTime = W9825G6KH_T_WR_CYCLES;
    timing.RPDelay = W9825G6KH_T_RP_CYCLES;
    timing.RCDDelay = W9825G6KH_T_RCD_CYCLES;

    if (!BSP_FMC_SDRAM_Init(&config, &timing))
    {
        return false;
    }
    if (!W9825G6KH_Power_Up_Sequence())
    {
        return false;
    }
    if (!BSP_FMC_SDRAM_Set_Refresh(W9825G6KH_REFRESH_COUNT))
    {
        return false;
    }

    /* 先完成 FMC 和 SDRAM 上电，随后才开放可读写的 MPU 区域 */
    Platform_SDRAM_MPU_Config(W9825G6KH_BANK_ADDR);
    return true;
}

/**
 * @brief  执行SDRAM上电自检
 * @note   破坏内存内容，须先于显示和采集缓冲访问
 */
bool W9825G6KH_Selftest(void)
{
    volatile uint32_t *base = (volatile uint32_t *)W9825G6KH_BANK_ADDR;
    uint32_t words = W9825G6KH_SIZE_BYTES / 4UL;
    uint32_t step = W9825G6KH_SELFTEST_STRIDE / 4UL;
    uint32_t blocks = W9825G6KH_SELFTEST_BLOCK / 4UL;
    uint32_t i;
    uint32_t expect;
    static const uint32_t patterns[] =
        {0x00000000UL, 0xFFFFFFFFUL, 0xAAAAAAAAUL, 0x55555555UL, 0xFFFF0000UL, 0x0000FFFFUL};

    w9825g6kh_error_addr = 0U;
    for (i = 0U; i < words; i += step)
    {
        base[i] = i * 2654435761UL;
    }
    for (i = 0U; i < words; i += step)
    {
        expect = i * 2654435761UL;
        if (base[i] != expect)
        {
            w9825g6kh_error_addr = W9825G6KH_BANK_ADDR + i * 4UL;
            return false;
        }
    }

    for (i = 0U; i < blocks; ++i)
    {
        base[i] = ((i & 1U) != 0U) ? 0xAAAAAAAAUL : 0x55555555UL;
    }
    for (i = 0U; i < blocks; ++i)
    {
        expect = ((i & 1U) != 0U) ? 0xAAAAAAAAUL : 0x55555555UL;
        if (base[i] != expect)
        {
            w9825g6kh_error_addr = W9825G6KH_BANK_ADDR + i * 4UL;
            return false;
        }
    }

    for (i = 0U; i < (sizeof(patterns) / sizeof(patterns[0])); ++i)
    {
        base[i] = patterns[i];
    }
    for (i = 0U; i < (sizeof(patterns) / sizeof(patterns[0])); ++i)
    {
        if (base[i] != patterns[i])
        {
            w9825g6kh_error_addr = W9825G6KH_BANK_ADDR + i * 4UL;
            return false;
        }
    }
    return true;
}

/**
 * @brief  取得自检首个错误地址
 */
uint32_t W9825G6KH_Get_Error_Addr(void)
{
    return w9825g6kh_error_addr;
}
