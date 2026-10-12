/**
 * @file   log_service.c
 * @brief  通过消息队列把任务日志交给独立 UART 发送任务
 */

/* ================================================== */

#include "log/log_service.h"
#include "serial/uart.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ========================= 配置 ========================= */

#define LOG_MESSAGE_CAPACITY 96U
#define LOG_QUEUE_LENGTH     32U
#define LOG_TASK_STACK_DEPTH 512U
#define LOG_TASK_PRIORITY    4U

/* ================================================== */

typedef struct
{
    uint16_t length;                    /* 本条有效字节数，不以 NUL 作为结束标志 */
    uint8_t data[LOG_MESSAGE_CAPACITY]; /* 由队列拥有的日志字节副本 */
} Log_Message_t;

/* ================================================== */

static QueueHandle_t log_queue;
static TaskHandle_t log_task_handle;

/* ================================================== */

/**
 * @brief  串行发送队列中的消息
 */
static void Log_Service_Task(void *argument)
{
    Log_Message_t message;

    (void)argument;
    for (;;)
    {
        if (xQueueReceive(log_queue, &message, portMAX_DELAY) == pdTRUE)
        {
            (void)UART_Write(message.data, message.length);
        }
    }
}

/* ================================================== */

/**
 * @brief  在全部设备延时初始化之后建立日志运行期资源
 * @note   BSP runtime 资源与日志队列分属各自拥有者，任务创建失败仅删除本模块队列
 */
bool Log_Service_Init(void)
{
    if (log_task_handle != NULL)
    {
        return true;
    }
    /* BSP 阶段只准备 UART 硬件 */
    if (!UART_Runtime_Init())
    {
        return false;
    }

    log_queue = xQueueCreate(LOG_QUEUE_LENGTH, sizeof(Log_Message_t));
    if (log_queue == NULL)
    {
        return false;
    }
    if (xTaskCreate(Log_Service_Task,
                    "log",
                    LOG_TASK_STACK_DEPTH,
                    NULL,
                    LOG_TASK_PRIORITY,
                    &log_task_handle) != pdPASS)
    {
        vQueueDelete(log_queue);
        log_queue = NULL;
        return false;
    }
    return true;
}

/**
 * @brief  同步复制调用者字节并立即尝试入队
 * @note   队列保存日志副本，发送任务不占用写入方的缓冲区；队列满时立即返回
 */
bool Log_Write(const void *data, uint16_t length)
{
    Log_Message_t message;

    if ((log_queue == NULL) || (data == NULL) || (length == 0U) || (length > LOG_MESSAGE_CAPACITY))
    {
        return false;
    }
    message.length = length;
    memcpy(message.data, data, length);
    return xQueueSend(log_queue, &message, 0U) == pdTRUE;
}

/**
 * @brief  在栈上完成格式化并提交一条独立日志
 */
bool Log_Printf(const char *format, ...)
{
    char buffer[LOG_MESSAGE_CAPACITY];
    va_list arguments;
    int length;

    if (format == NULL)
    {
        return false;
    }
    va_start(arguments, format);
    length = vsnprintf(buffer, sizeof(buffer), format, arguments);
    va_end(arguments);
    if (length <= 0)
    {
        return false;
    }
    if (length >= (int)sizeof(buffer))
    {
        length = (int)sizeof(buffer) - 1;
    }
    return Log_Write(buffer, (uint16_t)length);
}
