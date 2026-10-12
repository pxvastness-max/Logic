/**
 * @file   stm32h7xx_it.c
 * @brief  处理器异常与外设中断的统一向量入口
 */

#include "stm32h7xx_it.h"
#include "stm32h7xx_hal.h"
#include "system/platform_system.h"
#include "time/platform_hal_tick.h"
#include "timer/bsp_timer.h"
#include "display/bsp_dma2d.h"
#include "display/bsp_ltdc_lcd.h"
#include "sd/bsp_sdmmc.h"
#include "serial/uart.h"
#include "dma/bsp_dma.h"

/* SVC、PendSV 和 SysTick 由 FreeRTOS 端口直接提供。 */

void NMI_Handler(void)
{
    Platform_Fatal_Error();
}

void HardFault_Handler(void)
{
    Platform_Fatal_Error();
}

void MemManage_Handler(void)
{
    Platform_Fatal_Error();
}

void BusFault_Handler(void)
{
    Platform_Fatal_Error();
}

void UsageFault_Handler(void)
{
    Platform_Fatal_Error();
}

void DebugMon_Handler(void)
{
}

/* TIM6 负责 HAL 毫秒时基，SysTick 只由 FreeRTOS 使用。 */
void TIM6_DAC_IRQHandler(void)
{
    Platform_HAL_Tick_IRQHandler();
}

/* TIM2 使用 DMA1 Stream0..2，TIM5 使用 DMA2 Stream0..2。 */
void DMA1_Stream0_IRQHandler(void)
{
    BSP_DMA_Capture_IRQHandler(0U);
}

void DMA1_Stream1_IRQHandler(void)
{
    BSP_DMA_Capture_IRQHandler(1U);
}

void DMA1_Stream2_IRQHandler(void)
{
    BSP_DMA_Capture_IRQHandler(2U);
}

void DMA2_Stream0_IRQHandler(void)
{
    BSP_DMA_Capture_IRQHandler(3U);
}

void DMA2_Stream1_IRQHandler(void)
{
    BSP_DMA_Capture_IRQHandler(4U);
}

void DMA2_Stream2_IRQHandler(void)
{
    BSP_DMA_Capture_IRQHandler(5U);
}

void TIM2_IRQHandler(void)
{
    BSP_Timer_Capture_IRQHandler();
}

void SDMMC1_IRQHandler(void)
{
    BSP_SDMMC_IRQHandler();
}

void TIM7_IRQHandler(void)
{
    BSP_Timer_Tick_IRQHandler();
}

/* UART TX DMA 完成后仍由 HAL UART 等待最后一位发送结束。 */
void DMA2_Stream7_IRQHandler(void)
{
    HAL_DMA_IRQHandler(&bsp_uart_tx_dma_handle);
}

void USART1_IRQHandler(void)
{
    HAL_UART_IRQHandler(&bsp_uart_handle);
}

void DMA2D_IRQHandler(void)
{
    HAL_DMA2D_IRQHandler(&bsp_dma2d_handle);
}

void LTDC_IRQHandler(void)
{
    HAL_LTDC_IRQHandler(&bsp_ltdc_handle);
}

void LTDC_ER_IRQHandler(void)
{
    BSP_LTDC_LCD_Capture_Error_IRQ();
    HAL_LTDC_IRQHandler(&bsp_ltdc_handle);
}
