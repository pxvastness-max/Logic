/**
 * @file   bsp_timer.c
 * @brief  TIM1 同步启动 TIM2/TIM5；TIM7 提供独立的毫秒周期事件
 */

/* ================================================== */

#include "timer/bsp_timer.h"
#include "dma/bsp_dma.h"
#include "clock/platform_clock.h"

/* ========================= 配置 ========================= */

#define BSP_TIMER_IC_FILTER       8U
#define BSP_TIMER_TICK_COUNTER_HZ 1000000U

/* ================================================== */

TIM_HandleTypeDef htim1;
TIM_HandleTypeDef htim2;
TIM_HandleTypeDef htim5;
static TIM_HandleTypeDef bsp_timer_tick_handle;
static BSP_Timer_Tick_Callback_t bsp_timer_tick_callback;
static void *bsp_timer_tick_context;

/* ================================================== */

/**
 * @brief  配置采集引脚，未接入外部信号时由下拉电阻保持低电平
 */
static void BSP_Timer_Capture_GPIO_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    gpio.Pin = GPIO_PIN_5;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_PULLDOWN;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    gpio.Alternate = GPIO_AF1_TIM2;
    HAL_GPIO_Init(GPIOA, &gpio);
    gpio.Pin = GPIO_PIN_10 | GPIO_PIN_11;
    HAL_GPIO_Init(GPIOB, &gpio);
    gpio.Pin = GPIO_PIN_0 | GPIO_PIN_2 | GPIO_PIN_3;
    gpio.Alternate = GPIO_AF2_TIM5;
    HAL_GPIO_Init(GPIOA, &gpio);
}

/**
 * @brief  初始化一个不分频、向上计数且关闭 ARR 预装载的定时器
 */
static bool BSP_Timer_Base_Init(TIM_HandleTypeDef *timer, TIM_TypeDef *instance, uint32_t period)
{
    timer->Instance = instance;
    timer->Init.Prescaler = 0U;
    timer->Init.CounterMode = TIM_COUNTERMODE_UP;
    timer->Init.Period = period;
    timer->Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    timer->Init.RepetitionCounter = 0U;
    timer->Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    return HAL_TIM_Base_Init(timer) == HAL_OK;
}

/**
 * @brief  统一配置 TIM2/TIM5 为由 TIM1 ITR0 触发的三通道双边沿捕获
 */
static bool BSP_Timer_Input_Capture_Init(TIM_HandleTypeDef *timer)
{
    TIM_ClockConfigTypeDef clock = {0};
    TIM_SlaveConfigTypeDef slave = {0};
    TIM_MasterConfigTypeDef master = {0};
    TIM_IC_InitTypeDef capture = {0};
    static const uint32_t channels[] = {TIM_CHANNEL_1, TIM_CHANNEL_3, TIM_CHANNEL_4};
    uint32_t channel_index;

    clock.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
    slave.SlaveMode = TIM_SLAVEMODE_TRIGGER;
    slave.InputTrigger = TIM_TS_ITR0;
    master.MasterOutputTrigger = TIM_TRGO_RESET;
    master.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
    if ((HAL_TIM_ConfigClockSource(timer, &clock) != HAL_OK) ||
        (HAL_TIM_IC_Init(timer) != HAL_OK) ||
        (HAL_TIM_SlaveConfigSynchro(timer, &slave) != HAL_OK) ||
        (HAL_TIMEx_MasterConfigSynchronization(timer, &master) != HAL_OK))
    {
        return false;
    }
    capture.ICPolarity = TIM_INPUTCHANNELPOLARITY_BOTHEDGE;
    capture.ICSelection = TIM_ICSELECTION_DIRECTTI;
    capture.ICPrescaler = TIM_ICPSC_DIV1;
    /* fDTS/8 下连续 6 次采样确认边沿，六路保持相同最小脉宽约束 */
    capture.ICFilter = BSP_TIMER_IC_FILTER;
    for (channel_index = 0U; channel_index < 3U; ++channel_index)
    {
        if (HAL_TIM_IC_ConfigChannel(timer, &capture, channels[channel_index]) != HAL_OK)
        {
            return false;
        }
    }
    return true;
}

bool BSP_Timer_Capture_Init(void)
{
    TIM_ClockConfigTypeDef clock = {0};
    TIM_MasterConfigTypeDef master = {0};
    TIM_OC_InitTypeDef deadline = {0};

    __HAL_RCC_TIM1_CLK_ENABLE();
    __HAL_RCC_TIM2_CLK_ENABLE();
    __HAL_RCC_TIM5_CLK_ENABLE();
    BSP_Timer_Capture_GPIO_Init();
    if (!BSP_Timer_Base_Init(&htim1, TIM1, UINT16_MAX) ||
        !BSP_Timer_Base_Init(&htim2, TIM2, UINT32_MAX) ||
        !BSP_Timer_Base_Init(&htim5, TIM5, UINT32_MAX) || !BSP_DMA_Capture_Init(&htim2, &htim5))
    {
        return false;
    }
    clock.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
    master.MasterOutputTrigger = TIM_TRGO_UPDATE;
    master.MasterOutputTrigger2 = TIM_TRGO2_RESET;
    master.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
    if ((HAL_TIM_ConfigClockSource(&htim1, &clock) != HAL_OK) ||
        (HAL_TIMEx_MasterConfigSynchronization(&htim1, &master) != HAL_OK) ||
        !BSP_Timer_Input_Capture_Init(&htim2) || !BSP_Timer_Input_Capture_Init(&htim5) ||
        (HAL_TIM_OC_Init(&htim2) != HAL_OK))
    {
        return false;
    }

    /* TIM2_CH2 不接引脚，专用于统一截止时间比较；计数器宽度为 32 位 */
    deadline.OCMode = TIM_OCMODE_TIMING;
    deadline.Pulse = 0U;
    deadline.OCPolarity = TIM_OCPOLARITY_HIGH;
    deadline.OCFastMode = TIM_OCFAST_DISABLE;
    if (HAL_TIM_OC_ConfigChannel(&htim2, &deadline, TIM_CHANNEL_2) != HAL_OK)
    {
        return false;
    }
    HAL_NVIC_SetPriority(TIM2_IRQn, 5U, 0U);
    HAL_NVIC_EnableIRQ(TIM2_IRQn);
    return true;
}

bool BSP_Timer_Capture_Set_Filter(uint8_t input_filter)
{
    const uint32_t capture_mask = TIM_CCER_CC1E | TIM_CCER_CC3E | TIM_CCER_CC4E;
    const uint32_t dma_mask = TIM_DIER_CC1DE | TIM_DIER_CC3DE | TIM_DIER_CC4DE;
    const uint32_t filter_1 = (uint32_t)input_filter << TIM_CCMR1_IC1F_Pos;
    const uint32_t filter_3_4 = ((uint32_t)input_filter << TIM_CCMR2_IC3F_Pos) |
                                ((uint32_t)input_filter << TIM_CCMR2_IC4F_Pos);

    /* 在写任何一路前检查全部前提，失败时保持六路原滤波配置 */
    if ((input_filter > 15U) || (htim2.Instance != TIM2) || (htim5.Instance != TIM5) ||
        ((TIM2->CR1 & TIM_CR1_CEN) != 0U) || ((TIM5->CR1 & TIM_CR1_CEN) != 0U) ||
        ((TIM2->CCER & (capture_mask | TIM_CCER_CC2E)) != 0U) ||
        ((TIM5->CCER & capture_mask) != 0U) || ((TIM2->DIER & (dma_mask | TIM_DIER_CC2IE)) != 0U) ||
        ((TIM5->DIER & dma_mask) != 0U))
    {
        return false;
    }

    /* 捕获关闭后再更新确认时间，避免采集中途引入不同滤波延迟 */
    MODIFY_REG(TIM2->CCMR1, TIM_CCMR1_IC1F, filter_1);
    MODIFY_REG(TIM5->CCMR1, TIM_CCMR1_IC1F, filter_1);
    MODIFY_REG(TIM2->CCMR2, TIM_CCMR2_IC3F | TIM_CCMR2_IC4F, filter_3_4);
    MODIFY_REG(TIM5->CCMR2, TIM_CCMR2_IC3F | TIM_CCMR2_IC4F, filter_3_4);
    __DSB();
    return true;
}

bool BSP_Timer_Tick_Init(void)
{
    TIM_MasterConfigTypeDef master = {0};
    uint32_t timer_clock = Platform_Clock_Apb1_Timer_Hz();
    uint32_t divider;

    if ((timer_clock < BSP_TIMER_TICK_COUNTER_HZ) ||
        ((timer_clock % BSP_TIMER_TICK_COUNTER_HZ) != 0U))
    {
        return false;
    }
    divider = timer_clock / BSP_TIMER_TICK_COUNTER_HZ;
    if (divider > 65536U)
    {
        return false;
    }
    __HAL_RCC_TIM7_CLK_ENABLE();
    bsp_timer_tick_handle.Instance = TIM7;
    bsp_timer_tick_handle.Init.Prescaler = divider - 1U;
    bsp_timer_tick_handle.Init.CounterMode = TIM_COUNTERMODE_UP;
    bsp_timer_tick_handle.Init.Period = 999U;
    bsp_timer_tick_handle.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    master.MasterOutputTrigger = TIM_TRGO_RESET;
    master.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
    if ((HAL_TIM_Base_Init(&bsp_timer_tick_handle) != HAL_OK) ||
        (HAL_TIMEx_MasterConfigSynchronization(&bsp_timer_tick_handle, &master) != HAL_OK))
    {
        return false;
    }
    HAL_NVIC_SetPriority(TIM7_IRQn, 15U, 0U);
    HAL_NVIC_EnableIRQ(TIM7_IRQn);
    return true;
}

void BSP_Timer_Tick_Set_Callback(BSP_Timer_Tick_Callback_t callback, void *context)
{
    uint32_t primask = __get_PRIMASK();

    /* 回调和上下文必须成对发布，防止运行中的 TIM7 观察到一半更新的绑定 */
    __disable_irq();
    bsp_timer_tick_callback = callback;
    bsp_timer_tick_context = context;
    __DMB();
    __set_PRIMASK(primask);
}

bool BSP_Timer_Tick_Start(void)
{
    __HAL_TIM_CLEAR_FLAG(&bsp_timer_tick_handle, TIM_FLAG_UPDATE);
    return HAL_TIM_Base_Start_IT(&bsp_timer_tick_handle) == HAL_OK;
}

void BSP_Timer_Capture_IRQHandler(void)
{
    HAL_TIM_IRQHandler(&htim2);
}

void BSP_Timer_Tick_IRQHandler(void)
{
    if ((__HAL_TIM_GET_FLAG(&bsp_timer_tick_handle, TIM_FLAG_UPDATE) != RESET) &&
        (__HAL_TIM_GET_IT_SOURCE(&bsp_timer_tick_handle, TIM_IT_UPDATE) != RESET))
    {
        __HAL_TIM_CLEAR_IT(&bsp_timer_tick_handle, TIM_IT_UPDATE);
        if (bsp_timer_tick_callback != NULL)
        {
            bsp_timer_tick_callback(bsp_timer_tick_context);
        }
    }
}
