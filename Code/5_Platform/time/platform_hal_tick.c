/**
 * @file   platform_hal_tick.c
 * @brief  TIM6 提供 HAL 1 ms 时基，SysTick 完全交给 FreeRTOS
 */

/* ================================================== */

#include "time/platform_hal_tick.h"
#include "clock/platform_clock.h"
#include "stm32h7xx_hal.h"

/* ========================= 配置 ========================= */

#define PLATFORM_HAL_TICK_COUNTER_HZ 1000000U
#define PLATFORM_HAL_TICK_PERIOD_HZ  1000U

/* ================================================== */

static TIM_HandleTypeDef platform_hal_tick_timer;

/* ================================================== */

/**
 * @brief  重建HAL毫秒时基
 * @note   支持初始化和系统时钟切换后的重复定标
 */
HAL_StatusTypeDef HAL_InitTick(uint32_t tick_priority)
{
    uint32_t timer_clock = Platform_Clock_Apb1_Timer_Hz();
    uint32_t divider;

    if ((tick_priority >= (1UL << __NVIC_PRIO_BITS)) ||
        (timer_clock < PLATFORM_HAL_TICK_COUNTER_HZ) ||
        ((timer_clock % PLATFORM_HAL_TICK_COUNTER_HZ) != 0U))
    {
        return HAL_ERROR;
    }
    divider = timer_clock / PLATFORM_HAL_TICK_COUNTER_HZ;
    if (divider > 65536U)
    {
        return HAL_ERROR;
    }

    __HAL_RCC_TIM6_CLK_ENABLE();
    HAL_NVIC_DisableIRQ(TIM6_DAC_IRQn);
    platform_hal_tick_timer.Instance = TIM6;
    /* 暂停更新请求后重新装载 PSC/ARR，清除初始化生成的更新标志 */
    __HAL_TIM_DISABLE_IT(&platform_hal_tick_timer, TIM_IT_UPDATE);
    __HAL_TIM_DISABLE(&platform_hal_tick_timer);
    platform_hal_tick_timer.Init.Prescaler = divider - 1U;
    platform_hal_tick_timer.Init.CounterMode = TIM_COUNTERMODE_UP;
    platform_hal_tick_timer.Init.Period =
        (PLATFORM_HAL_TICK_COUNTER_HZ / PLATFORM_HAL_TICK_PERIOD_HZ) - 1U;
    platform_hal_tick_timer.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    platform_hal_tick_timer.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    if (HAL_TIM_Base_Init(&platform_hal_tick_timer) != HAL_OK)
    {
        return HAL_ERROR;
    }
    __HAL_TIM_SET_COUNTER(&platform_hal_tick_timer, 0U);
    __HAL_TIM_CLEAR_FLAG(&platform_hal_tick_timer, TIM_FLAG_UPDATE);
    HAL_NVIC_ClearPendingIRQ(TIM6_DAC_IRQn);
    HAL_NVIC_SetPriority(TIM6_DAC_IRQn, tick_priority, 0U);
    uwTickPrio = tick_priority;
    HAL_NVIC_EnableIRQ(TIM6_DAC_IRQn);
    return HAL_TIM_Base_Start_IT(&platform_hal_tick_timer);
}

/**
 * @brief  暂停 TIM6 更新中断，HAL 毫秒计数停止但定时器本身继续运行
 */
void HAL_SuspendTick(void)
{
    __HAL_TIM_DISABLE_IT(&platform_hal_tick_timer, TIM_IT_UPDATE);
}

/**
 * @brief  恢复 TIM6 更新中断，不补偿暂停期间遗漏的毫秒计数
 */
void HAL_ResumeTick(void)
{
    __HAL_TIM_ENABLE_IT(&platform_hal_tick_timer, TIM_IT_UPDATE);
}

void Platform_HAL_Tick_IRQHandler(void)
{
    /* TIM6 没有其他事件源，直接服务更新事件，避免与 BSP TIM 回调共用分发器 */
    if ((__HAL_TIM_GET_FLAG(&platform_hal_tick_timer, TIM_FLAG_UPDATE) != RESET) &&
        (__HAL_TIM_GET_IT_SOURCE(&platform_hal_tick_timer, TIM_IT_UPDATE) != RESET))
    {
        __HAL_TIM_CLEAR_IT(&platform_hal_tick_timer, TIM_IT_UPDATE);
        HAL_IncTick();
    }
}
