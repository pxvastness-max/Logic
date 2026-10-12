/**
 * @file   platform_clock.c
 * @brief  固定板级时钟契约，保留原工程经过验证的 PLL1 与总线分频
 */

/* ================================================== */

#include "clock/platform_clock.h"
#include "platform_config.h"
#include "stm32h7xx_hal.h"

/* ========================= 配置 ========================= */

#define PLATFORM_VOLTAGE_READY_TIMEOUT_MS 100U

/* ================================================== */

/**
 * @brief  按板卡供电和 HSE 契约建立 PLL1 及各总线时钟
 */
bool Platform_Clock_Init(void)
{
    RCC_OscInitTypeDef oscillator = {0};
    RCC_ClkInitTypeDef clock = {0};
    uint32_t start_tick;

    if (HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY) != HAL_OK)
    {
        return false;
    }
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);
    start_tick = HAL_GetTick();
    while (!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY))
    {
        if ((HAL_GetTick() - start_tick) >= PLATFORM_VOLTAGE_READY_TIMEOUT_MS)
        {
            return false;
        }
    }

    /* 25 MHz / 5 * 192 = 960 MHz VCO */
    oscillator.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    oscillator.HSEState = RCC_HSE_ON;
    oscillator.PLL.PLLState = RCC_PLL_ON;
    oscillator.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    oscillator.PLL.PLLM = 5U;
    oscillator.PLL.PLLN = 192U;
    oscillator.PLL.PLLP = 2U;
    oscillator.PLL.PLLQ = 4U;
    oscillator.PLL.PLLR = 2U;
    oscillator.PLL.PLLRGE = RCC_PLL1VCIRANGE_2;
    oscillator.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
    oscillator.PLL.PLLFRACN = 0U;
    if (HAL_RCC_OscConfig(&oscillator) != HAL_OK)
    {
        return false;
    }

    clock.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 |
                      RCC_CLOCKTYPE_PCLK2 | RCC_CLOCKTYPE_D3PCLK1 | RCC_CLOCKTYPE_D1PCLK1;
    clock.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clock.SYSCLKDivider = RCC_SYSCLK_DIV1;
    clock.AHBCLKDivider = RCC_HCLK_DIV2;
    clock.APB3CLKDivider = RCC_APB3_DIV2;
    clock.APB1CLKDivider = RCC_APB1_DIV2;
    clock.APB2CLKDivider = RCC_APB2_DIV2;
    clock.APB4CLKDivider = RCC_APB4_DIV2;
    if (HAL_RCC_ClockConfig(&clock, FLASH_LATENCY_4) != HAL_OK)
    {
        return false;
    }
    return SystemCoreClock == CPU_CYCLES_HZ;
}

/**
 * @brief  根据当前 APB1 分频计算 TIM2/TIM5/TIM6/TIM7 的实际内核时钟
 */
uint32_t Platform_Clock_Apb1_Timer_Hz(void)
{
    RCC_ClkInitTypeDef clock = {0};
    uint32_t flash_latency;
    uint32_t timer_clock;

    /* 本工程使用 TIMPRE = 0；拒绝未支持的四倍频模式，避免静默改变采集时基 */
    if ((RCC->CFGR & RCC_CFGR_TIMPRE) != 0U)
    {
        return 0U;
    }
    HAL_RCC_GetClockConfig(&clock, &flash_latency);
    timer_clock = HAL_RCC_GetPCLK1Freq();
    if (clock.APB1CLKDivider != RCC_APB1_DIV1)
    {
        timer_clock *= 2U;
    }
    return timer_clock;
}
