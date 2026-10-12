/**
 * @file   bsp_fmc_sdram.c
 * @brief  阿波罗 H743板载SDRAM的FMC时钟、GPIO和控制器实现
 */

/* ================================================== */

#include "memory/bsp_fmc_sdram.h"

/* ================================================== */

/* 同一GPIO端口上的一组FMC复用引脚 */
typedef struct
{
    GPIO_TypeDef *port;
    uint16_t pins;
} BSP_FMC_Pin_Group_t;

/* ================================================== */

SDRAM_HandleTypeDef bsp_fmc_sdram_handle; /* HAL FMC SDRAM外设句柄 */

/* 当前PCB上SDRAM连接使用的全部FMC引脚 */
static const BSP_FMC_Pin_Group_t bsp_fmc_sdram_pins[] = {
    {GPIOC, GPIO_PIN_0 | GPIO_PIN_2 | GPIO_PIN_3},
    {GPIOD,
     GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_14 | GPIO_PIN_15},
    {GPIOE,
     GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11 |
         GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15},
    {GPIOF,
     GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_11 |
         GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15},
    {GPIOG,
     GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_8 | GPIO_PIN_15},
    {NULL, 0U}};

/* ================================================== */

/**
 * @brief  开启FMC和GPIO时钟并把SDRAM引脚配置为AF12
 */
static void BSP_FMC_SDRAM_GPIO_Init(void)
{
    GPIO_InitTypeDef gpio = {0};
    uint32_t i;

    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();
    __HAL_RCC_FMC_CLK_ENABLE();

    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF12_FMC;
    for (i = 0U; bsp_fmc_sdram_pins[i].port != NULL; ++i)
    {
        gpio.Pin = bsp_fmc_sdram_pins[i].pins;
        HAL_GPIO_Init(bsp_fmc_sdram_pins[i].port, &gpio);
    }
}

/**
 * @brief  初始化FMC SDRAM控制器
 * @note   不执行器件上电序列
 */
bool BSP_FMC_SDRAM_Init(const BSP_FMC_SDRAM_Config_t *config, const FMC_SDRAM_TimingTypeDef *timing)
{
    if (config == NULL || timing == NULL)
    {
        return false;
    }
    BSP_FMC_SDRAM_GPIO_Init();

    bsp_fmc_sdram_handle.Instance = FMC_Bank5_6_R;
    bsp_fmc_sdram_handle.Init.SDBank = FMC_SDRAM_BANK1;
    bsp_fmc_sdram_handle.Init.ColumnBitsNumber = config->column_bits;
    bsp_fmc_sdram_handle.Init.RowBitsNumber = config->row_bits;
    bsp_fmc_sdram_handle.Init.MemoryDataWidth = config->data_width;
    bsp_fmc_sdram_handle.Init.InternalBankNumber = config->internal_banks;
    bsp_fmc_sdram_handle.Init.CASLatency = config->cas_latency;
    bsp_fmc_sdram_handle.Init.WriteProtection = FMC_SDRAM_WRITE_PROTECTION_DISABLE;
    bsp_fmc_sdram_handle.Init.SDClockPeriod = config->clock_period;
    bsp_fmc_sdram_handle.Init.ReadBurst = FMC_SDRAM_RBURST_ENABLE;
    bsp_fmc_sdram_handle.Init.ReadPipeDelay = config->read_pipe_delay;
    return (HAL_SDRAM_Init(&bsp_fmc_sdram_handle, (FMC_SDRAM_TimingTypeDef *)timing) == HAL_OK);
}

/**
 * @brief  发送SDRAM命令
 */
bool BSP_FMC_SDRAM_Send_Command(FMC_SDRAM_CommandTypeDef *command)
{
    return (command != NULL) &&
           (HAL_SDRAM_SendCommand(&bsp_fmc_sdram_handle, command, 0x1000U) == HAL_OK);
}

/**
 * @brief  设置FMC SDRAM自动刷新计数值
 */
bool BSP_FMC_SDRAM_Set_Refresh(uint32_t refresh_count)
{
    return (HAL_SDRAM_ProgramRefreshRate(&bsp_fmc_sdram_handle, refresh_count) == HAL_OK);
}
