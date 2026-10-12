/**
 * @file   bsp_sdmmc.c
 * @brief  板载 SDMMC1 的 PLL1Q 时钟、六线 GPIO 与可重复启停的资源管理
 */

/* ================================================== */

#include "sd/bsp_sdmmc.h"

/* ================================================== */

SD_HandleTypeDef hsd1;

/* ================================================== */

bool BSP_SDMMC_Hardware_Init(void)
{
    RCC_PeriphCLKInitTypeDef clock = {0};
    GPIO_InitTypeDef gpio = {0};

    clock.PeriphClockSelection = RCC_PERIPHCLK_SDMMC;
    clock.SdmmcClockSelection = RCC_SDMMCCLKSOURCE_PLL;
    if (HAL_RCCEx_PeriphCLKConfig(&clock) != HAL_OK)
    {
        return false;
    }
    HAL_NVIC_DisableIRQ(SDMMC1_IRQn);
    __HAL_RCC_SDMMC1_CLK_ENABLE();
    /* 释放失败收尾留下的复位，并保证每次重试从干净的 IDMA/DPSM 状态开始 */
    __HAL_RCC_SDMMC1_FORCE_RESET();
    __DSB();
    __HAL_RCC_SDMMC1_RELEASE_RESET();
    __DSB();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();

    /* PC8..PC11 = D0..D3 */
    gpio.Pin = GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF12_SDIO1;
    HAL_GPIO_Init(GPIOC, &gpio);
    gpio.Pin = GPIO_PIN_2;
    HAL_GPIO_Init(GPIOD, &gpio);

    HAL_NVIC_SetPriority(SDMMC1_IRQn, 6U, 0U);
    HAL_NVIC_ClearPendingIRQ(SDMMC1_IRQn);
    HAL_NVIC_EnableIRQ(SDMMC1_IRQn);
    return true;
}

void BSP_SDMMC_Hardware_Stop(void)
{
    HAL_NVIC_DisableIRQ(SDMMC1_IRQn);
    __HAL_RCC_SDMMC1_CLK_ENABLE();
    /* Abort 的 CMD12/超时早返回不能证明 IDMA 已关闭 */
    SDMMC1->MASK = 0U;
    SDMMC1->IDMACTRL = SDMMC_DISABLE_IDMA;
    SDMMC1->DCTRL = 0U;
    SDMMC1->CMD = 0U;
    __DSB();
    /* 持续复位使 IDMA 与命令/数据状态机无法再次发出总线请求 */
    __HAL_RCC_SDMMC1_FORCE_RESET();
    __DSB();
    HAL_NVIC_ClearPendingIRQ(SDMMC1_IRQn);
    __HAL_RCC_SDMMC1_CLK_DISABLE();
    HAL_GPIO_DeInit(GPIOC, GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12);
    HAL_GPIO_DeInit(GPIOD, GPIO_PIN_2);
    hsd1.State = HAL_SD_STATE_RESET;
    hsd1.Context = SD_CONTEXT_NONE;
    hsd1.Lock = HAL_UNLOCKED;
    hsd1.pTxBuffPtr = NULL;
    hsd1.pRxBuffPtr = NULL;
    hsd1.TxXferSize = 0U;
    hsd1.RxXferSize = 0U;
    __DSB();
}

void HAL_SD_MspDeInit(SD_HandleTypeDef *sd)
{
    if (sd->Instance == SDMMC1)
    {
        BSP_SDMMC_Hardware_Stop();
    }
}

void BSP_SDMMC_IRQHandler(void)
{
    HAL_SD_IRQHandler(&hsd1);
}
