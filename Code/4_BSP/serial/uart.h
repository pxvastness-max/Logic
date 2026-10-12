/**
 * @file   uart.h
 * @brief  板载 CH340 调试串口 USART1 的只发送接口
 */

#ifndef UART_H
#define UART_H

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================== */

#include "stm32h7xx_hal.h"
#include <stdbool.h>
#include <stdint.h>

/* ========================= 配置 ========================= */

#define UART_INSTANCE           USART1
#define UART_BAUDRATE           115200U
#define UART_TX_GPIO_PORT       GPIOA
#define UART_TX_GPIO_PIN        GPIO_PIN_9
#define UART_TX_DMA_INSTANCE    DMA2_Stream7
#define UART_TX_DMA_REQUEST     DMA_REQUEST_USART1_TX
#define UART_TX_DMA_IRQn        DMA2_Stream7_IRQn
#define UART_TX_DMA_BUFFER_SIZE 128U
/* 数值不得小于 configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY */
#define UART_IRQ_PRIORITY 5U

/* ================================================== */

/**
 * @brief  初始化 PA9、USART1 115200 8N1 和 TX DMA
 * @note   在调度器启动前调用一次
 */
bool UART_Init(void);

/**
 * @brief  创建驱动同步对象，已创建时直接返回成功
 */
bool UART_Runtime_Init(void);

/**
 * @brief  复制并通过 DMA 发送一条 1..128 字节消息
 * @param  length 消息字节数
 * @note   仅可在调度器运行且中断未屏蔽的任务上下文调用；驱动串行处理多个调用者
 */
uint16_t UART_Write(const uint8_t *data, uint16_t length);

/* ================================================== */

/* 仅供平台中断分发与板级诊断使用，服务层无需访问 HAL 句柄 */
extern UART_HandleTypeDef bsp_uart_handle;
extern DMA_HandleTypeDef bsp_uart_tx_dma_handle;

/* ================================================== */

#ifdef __cplusplus
}
#endif

#endif /* UART_H */
