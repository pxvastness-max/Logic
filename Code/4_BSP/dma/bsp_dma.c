/**
 * @file   bsp_dma.c
 * @brief  DMA1/DMA2 捕获资源表驱动配置，统一六路 DMA 的数据宽度、循环模式与优先级
 */

/* ================================================== */

#include "dma/bsp_dma.h"

/* ================================================== */

typedef struct
{
    DMA_HandleTypeDef *handle;
    DMA_Stream_TypeDef *stream;
    uint32_t request;
    IRQn_Type irq;
    uint32_t timer_dma_id;
} BSP_Capture_DMA_Config_t;

/* ================================================== */

DMA_HandleTypeDef hdma_tim2_ch1;
DMA_HandleTypeDef hdma_tim2_ch3;
DMA_HandleTypeDef hdma_tim2_ch4;
DMA_HandleTypeDef hdma_tim5_ch1;
DMA_HandleTypeDef hdma_tim5_ch3;
DMA_HandleTypeDef hdma_tim5_ch4;

static const BSP_Capture_DMA_Config_t bsp_dma_capture_dma[] = {
    {&hdma_tim2_ch1, DMA1_Stream0, DMA_REQUEST_TIM2_CH1, DMA1_Stream0_IRQn, TIM_DMA_ID_CC1},
    {&hdma_tim2_ch3, DMA1_Stream1, DMA_REQUEST_TIM2_CH3, DMA1_Stream1_IRQn, TIM_DMA_ID_CC3},
    {&hdma_tim2_ch4, DMA1_Stream2, DMA_REQUEST_TIM2_CH4, DMA1_Stream2_IRQn, TIM_DMA_ID_CC4},
    {&hdma_tim5_ch1, DMA2_Stream0, DMA_REQUEST_TIM5_CH1, DMA2_Stream0_IRQn, TIM_DMA_ID_CC1},
    {&hdma_tim5_ch3, DMA2_Stream1, DMA_REQUEST_TIM5_CH3, DMA2_Stream1_IRQn, TIM_DMA_ID_CC3},
    {&hdma_tim5_ch4, DMA2_Stream2, DMA_REQUEST_TIM5_CH4, DMA2_Stream2_IRQn, TIM_DMA_ID_CC4}};

/* ================================================== */

bool BSP_DMA_Capture_Init(TIM_HandleTypeDef *timer2, TIM_HandleTypeDef *timer5)
{
    uint32_t channel_index;

    if ((timer2 == NULL) || (timer5 == NULL))
    {
        return false;
    }
    __HAL_RCC_DMA1_CLK_ENABLE();
    __HAL_RCC_DMA2_CLK_ENABLE();
    for (channel_index = 0U;
         channel_index < (sizeof(bsp_dma_capture_dma) / sizeof(bsp_dma_capture_dma[0]));
         ++channel_index)
    {
        const BSP_Capture_DMA_Config_t *config = &bsp_dma_capture_dma[channel_index];
        DMA_HandleTypeDef *dma = config->handle;
        TIM_HandleTypeDef *timer = (channel_index < 3U) ? timer2 : timer5;

        dma->Instance = config->stream;
        dma->Init.Request = config->request;
        dma->Init.Direction = DMA_PERIPH_TO_MEMORY;
        dma->Init.PeriphInc = DMA_PINC_DISABLE;
        dma->Init.MemInc = DMA_MINC_ENABLE;
        dma->Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
        dma->Init.MemDataAlignment = DMA_MDATAALIGN_WORD;
        dma->Init.Mode = DMA_CIRCULAR;
        dma->Init.Priority = DMA_PRIORITY_VERY_HIGH;
        dma->Init.FIFOMode = DMA_FIFOMODE_DISABLE;
        if (HAL_DMA_Init(dma) != HAL_OK)
        {
            return false;
        }
        timer->hdma[config->timer_dma_id] = dma;
        dma->Parent = timer;
        HAL_NVIC_SetPriority(config->irq, 6U, 0U);
        HAL_NVIC_EnableIRQ(config->irq);
    }
    return true;
}

void BSP_DMA_Capture_IRQHandler(uint32_t channel_index)
{
    if (channel_index < (sizeof(bsp_dma_capture_dma) / sizeof(bsp_dma_capture_dma[0])))
    {
        HAL_DMA_IRQHandler(bsp_dma_capture_dma[channel_index].handle);
    }
}
