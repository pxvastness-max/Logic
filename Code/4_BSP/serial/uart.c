/**
 * @file   uart.c
 * @brief  USART1 TX DMA 驱动，独立管理传输缓冲、串行访问和完成等待
 */

/* ================================================== */

#include "serial/uart.h"
#include "memory/memory_placement.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"
#include <string.h>

/* ========================= 配置 ========================= */

#define UART_TX_TIMEOUT_MS 100U

/* ================================================== */

UART_HandleTypeDef bsp_uart_handle;
DMA_HandleTypeDef bsp_uart_tx_dma_handle;

/* SRAM1 DMA_NOCACHE 区由 MPU 配为不可缓存，发送前只需内存屏障 */
PLATFORM_DMA_NOCACHE_ZI
PLATFORM_CACHE_ALIGNMENT_ATTRIBUTE static uint8_t uart_dma_tx_buffer[UART_TX_DMA_BUFFER_SIZE];

static SemaphoreHandle_t uart_tx_lock;
static SemaphoreHandle_t uart_tx_done;
static volatile bool uart_tx_active;
static volatile bool uart_tx_failed;

/* ================================================== */

bool UART_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_USART1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    gpio.Pin = UART_TX_GPIO_PIN;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF7_USART1;
    HAL_GPIO_Init(UART_TX_GPIO_PORT, &gpio);

    bsp_uart_handle.Instance = UART_INSTANCE;
    bsp_uart_handle.Init.BaudRate = UART_BAUDRATE;
    bsp_uart_handle.Init.WordLength = UART_WORDLENGTH_8B;
    bsp_uart_handle.Init.StopBits = UART_STOPBITS_1;
    bsp_uart_handle.Init.Parity = UART_PARITY_NONE;
    bsp_uart_handle.Init.Mode = UART_MODE_TX;
    bsp_uart_handle.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    bsp_uart_handle.Init.OverSampling = UART_OVERSAMPLING_16;
    bsp_uart_handle.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
    bsp_uart_handle.Init.ClockPrescaler = UART_PRESCALER_DIV1;
    bsp_uart_handle.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
    bsp_uart_handle.FifoMode = UART_FIFOMODE_DISABLE;
    if (HAL_UART_Init(&bsp_uart_handle) != HAL_OK)
    {
        return false;
    }

    __HAL_RCC_DMA2_CLK_ENABLE();
    bsp_uart_tx_dma_handle.Instance = UART_TX_DMA_INSTANCE;
    bsp_uart_tx_dma_handle.Init.Request = UART_TX_DMA_REQUEST;
    bsp_uart_tx_dma_handle.Init.Direction = DMA_MEMORY_TO_PERIPH;
    bsp_uart_tx_dma_handle.Init.PeriphInc = DMA_PINC_DISABLE;
    bsp_uart_tx_dma_handle.Init.MemInc = DMA_MINC_ENABLE;
    bsp_uart_tx_dma_handle.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    bsp_uart_tx_dma_handle.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    bsp_uart_tx_dma_handle.Init.Mode = DMA_NORMAL;
    bsp_uart_tx_dma_handle.Init.Priority = DMA_PRIORITY_LOW;
    bsp_uart_tx_dma_handle.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
    bsp_uart_tx_dma_handle.Init.FIFOThreshold = DMA_FIFO_THRESHOLD_FULL;
    bsp_uart_tx_dma_handle.Init.MemBurst = DMA_MBURST_SINGLE;
    bsp_uart_tx_dma_handle.Init.PeriphBurst = DMA_PBURST_SINGLE;
    if (HAL_DMA_Init(&bsp_uart_tx_dma_handle) != HAL_OK)
    {
        return false;
    }
    __HAL_LINKDMA(&bsp_uart_handle, hdmatx, bsp_uart_tx_dma_handle);
    HAL_NVIC_SetPriority(UART_TX_DMA_IRQn, UART_IRQ_PRIORITY, 0U);
    HAL_NVIC_EnableIRQ(UART_TX_DMA_IRQn);
    HAL_NVIC_SetPriority(USART1_IRQn, UART_IRQ_PRIORITY, 0U);
    HAL_NVIC_EnableIRQ(USART1_IRQn);
    return true;
}

bool UART_Runtime_Init(void)
{
    /* 调度器启动前创建队列可能提高 BASEPRI */
    if (uart_tx_lock == NULL)
    {
        uart_tx_lock = xSemaphoreCreateMutex();
    }
    if (uart_tx_done == NULL)
    {
        uart_tx_done = xSemaphoreCreateBinary();
    }
    return (uart_tx_lock != NULL) && (uart_tx_done != NULL);
}

uint16_t UART_Write(const uint8_t *data, uint16_t length)
{
    uint16_t transmitted = 0U;

    if ((data == NULL) || (length == 0U) || (length > UART_TX_DMA_BUFFER_SIZE) ||
        (uart_tx_lock == NULL) || (uart_tx_done == NULL) || (__get_IPSR() != 0U) ||
        (__get_PRIMASK() != 0U) || (__get_BASEPRI() != 0U) ||
        (xTaskGetSchedulerState() != taskSCHEDULER_RUNNING))
    {
        return 0U;
    }
    if (xSemaphoreTake(uart_tx_lock, pdMS_TO_TICKS(UART_TX_TIMEOUT_MS)) != pdTRUE)
    {
        return 0U;
    }

    /* 前次异常未停稳时不覆盖 DMA 源缓冲，避免向线上发送一半旧数据、一半新数据 */
    if ((bsp_uart_handle.gState == HAL_UART_STATE_READY) &&
        (bsp_uart_tx_dma_handle.State == HAL_DMA_STATE_READY))
    {
        while (xSemaphoreTake(uart_tx_done, 0U) == pdTRUE)
        {
            /* 清理前次完成信号，不消费调用任务的其他通知 */
        }
        memcpy(uart_dma_tx_buffer, data, length);
        __DMB();
        uart_tx_failed = false;
        uart_tx_active = true;
        if (HAL_UART_Transmit_DMA(&bsp_uart_handle, uart_dma_tx_buffer, length) == HAL_OK)
        {
            if ((xSemaphoreTake(uart_tx_done, pdMS_TO_TICKS(UART_TX_TIMEOUT_MS)) == pdTRUE) &&
                !uart_tx_failed && (bsp_uart_handle.ErrorCode == HAL_UART_ERROR_NONE))
            {
                transmitted = length;
            }
            else
            {
                /* 在任务上下文等待硬件停止，避免错误或丢失中断永久阻塞日志发送 */
                (void)HAL_UART_AbortTransmit(&bsp_uart_handle);
            }
        }
        uart_tx_active = false;
    }
    (void)xSemaphoreGive(uart_tx_lock);
    return transmitted;
}

/**
 * @brief  在 USART1/DMA ISR 中唤醒本次同步发送等待者
 */
static void UART_Notify_From_ISR(bool failed)
{
    BaseType_t higher_priority_task_woken = pdFALSE;

    if (uart_tx_active && (uart_tx_done != NULL))
    {
        if (failed)
        {
            uart_tx_failed = true;
        }
        (void)xSemaphoreGiveFromISR(uart_tx_done, &higher_priority_task_woken);
        portYIELD_FROM_ISR(higher_priority_task_woken);
    }
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *uart)
{
    if (uart == &bsp_uart_handle)
    {
        UART_Notify_From_ISR(false);
    }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *uart)
{
    if (uart == &bsp_uart_handle)
    {
        UART_Notify_From_ISR(true);
    }
}
