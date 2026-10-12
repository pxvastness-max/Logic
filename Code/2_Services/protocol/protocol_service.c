/**
 * @file   protocol_service.c
 * @brief  协议解析统一调度、结果存储和事件通知
 */

/* ================================================== */

#include "protocol/protocol_service.h"
#include "protocol/protocol_internal.h"
#include "memory/memory_placement.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "stddef.h"

/* ========================= 配置 ========================= */

#define PROTOCOL_TASK_STACK_DEPTH 768U /* 解析任务栈深度 */
#define PROTOCOL_TASK_PRIORITY    2U   /* 解析任务优先级 */
#define PROTOCOL_PROCESS_BUDGET   256U /* 单次处理的帧或边沿预算 */
#define PROTOCOL_NOTIFY_PERIOD_MS 33U  /* 增量事件最小通知间隔 */
#define PROTOCOL_PUBLISH_BATCH    32U  /* 每次发布最多512字节，避免逐条加锁 */

#define PROTOCOL_QUERY_RETRY_MS 1U /* 查询锁忙时等待并重新检查取消请求 */

/* ================================================== */

typedef char Protocol_Record_Size_Must_Be_16[(sizeof(Protocol_Record_t) == 16U) ? 1 : -1];

/* ================================================== */

PLATFORM_SDRAM_PROTOCOL_ZI
static Protocol_Record_t protocol_records[PROTOCOL_RESULT_CAPACITY];

static TaskHandle_t protocol_task_handle = NULL;
/* 保护会话状态与已发布结果前缀，解码过程不长期持有 */
static SemaphoreHandle_t protocol_mutex = NULL;
static volatile uint8_t protocol_cancel_requested = 0U;
static Protocol_Config_t protocol_config;
static Protocol_Info_t protocol_info;
/* Start 请求任务取得后交给协议任务，终态发布前唯一释放 */
static LA_Service_Capture_Lease_t protocol_capture_lease;
/* 只有解析任务写入，按批次加锁发布，不供 GUI 直接读取 */
static Protocol_Record_t protocol_pending_records [PROTOCOL_PUBLISH_BATCH];
static uint16_t protocol_pending_count = 0U;
static uint16_t protocol_pending_errors = 0U;
static Protocol_Event_Callback_t protocol_event_callback = NULL;
static void *protocol_event_context = NULL;

/* ================================================== */

static void Protocol_Task(void *argument);

/* ================================================== */

/**
 * @brief  填充协议解析会话信息
 * @note   调用者持有协议状态互斥锁
 */
static void Protocol_Fill_Info(Protocol_Info_t *info)
{
    *info = protocol_info;
}

/**
 * @brief  复制会话状态并上报协议事件
 * @note   释放协议状态锁后在服务任务中调用用户回调
 */
static void Protocol_Report_Event(Protocol_Event_Type_t type)
{
    Protocol_Event_t event;
    Protocol_Event_Callback_t callback;
    void *context;

    (void)xSemaphoreTake(protocol_mutex, portMAX_DELAY);
    event.type = type;
    Protocol_Fill_Info(&event.info);
    callback = protocol_event_callback;
    context = protocol_event_context;
    (void)xSemaphoreGive(protocol_mutex);

    if (callback != NULL)
    {
        callback(&event, context);
    }
}

/**
 * @brief  批量发布已暂存的记录和计数
 * @note   暂存区仅由解析任务使用
 */
static void Protocol_Publish_Records(void)
{
    if (protocol_pending_count == 0U)
    {
        return;
    }

    (void)xSemaphoreTake(protocol_mutex, portMAX_DELAY);
    for (uint16_t i = 0U; i < protocol_pending_count; i++)
    {
        if (protocol_info.result_count < PROTOCOL_RESULT_CAPACITY)
        {
            protocol_records[protocol_info.result_count] =
                                                protocol_pending_records[i];
            protocol_info.result_count++;
        }
        else
        {
            protocol_info.dropped_count++;
        }
    }
    protocol_info.error_count += protocol_pending_errors;
    (void)xSemaphoreGive(protocol_mutex);
    protocol_pending_count = 0U;
    protocol_pending_errors = 0U;
}

/**
 * @brief  暂存解码记录并在批次满时发布
 */
static Protocol_Result_t Protocol_Emit_Record(
                                        const Protocol_Record_t *record,
                                        void *context)
{
    (void)context;

    if (record == NULL)
    {
        return PROTOCOL_ERROR_INVALID_PARAMETER;
    }

    protocol_pending_records[protocol_pending_count] = *record;
    protocol_pending_count++;
    if (((protocol_info.protocol == PROTOCOL_TYPE_UART) &&
         ((record->flags & (PROTOCOL_UART_FLAG_PARITY_ERROR |
                            PROTOCOL_UART_FLAG_FRAME_ERROR |
                            PROTOCOL_UART_FLAG_INCOMPLETE |
                            PROTOCOL_UART_FLAG_START_ERROR)) != 0U)) ||
        ((protocol_info.protocol == PROTOCOL_TYPE_IIC) &&
         (record->type == PROTOCOL_IIC_RECORD_ERROR)) ||
        ((protocol_info.protocol == PROTOCOL_TYPE_SPI) &&
         ((record->type == PROTOCOL_SPI_RECORD_ERROR) ||
          ((record->flags & PROTOCOL_SPI_FLAG_INCOMPLETE) != 0U))))
    {
        protocol_pending_errors++;
    }
    if (protocol_pending_count == PROTOCOL_PUBLISH_BATCH)
    {
        Protocol_Publish_Records();
    }
    return PROTOCOL_OK;
}

/**
 * @brief  选择会话使用的解析器操作表
 */
static const Protocol_Decoder_Ops_t *Protocol_Get_Ops(Protocol_Type_t protocol)
{
    switch (protocol)
    {
        case PROTOCOL_TYPE_UART:
            return &protocol_uart_decoder_ops;

        case PROTOCOL_TYPE_IIC:
            return &protocol_iic_decoder_ops;

        case PROTOCOL_TYPE_SPI:
            return &protocol_spi_decoder_ops;

        default:
            return NULL;
    }
}

/**
 * @brief  检查是否为有效LA输入通道
 */
static uint8_t Protocol_Channel_Is_Valid(LA_Service_Channel_t channel)
{
    return ((uint32_t)channel < LA_SERVICE_CHANNEL_COUNT) ? 1U : 0U;
}

/**
 * @brief  检查协议参数和通道组合
 */
static uint8_t Protocol_Config_Is_Valid(const Protocol_Config_t *config)
{
    if ((config == NULL) || ((uint32_t)config->protocol >= (uint32_t)PROTOCOL_TYPE_COUNT) ||
        (config->start_tick > config->end_tick))
    {
        return 0U;
    }

    switch (config->protocol)
    {
        case PROTOCOL_TYPE_UART: {
            const Protocol_UART_Config_t *uart = &config->decoder.uart;
            uint8_t has_tx = Protocol_Channel_Is_Valid(uart->tx_channel);
            uint8_t has_rx = Protocol_Channel_Is_Valid(uart->rx_channel);

            return (((has_tx != 0U) || (uart->tx_channel == PROTOCOL_CHANNEL_UNUSED)) &&
                    ((has_rx != 0U) || (uart->rx_channel == PROTOCOL_CHANNEL_UNUSED)) &&
                    ((has_tx != 0U) || (has_rx != 0U)) &&
                    ((has_tx == 0U) || (has_rx == 0U) || (uart->tx_channel != uart->rx_channel)) &&
                    (uart->baud_rate != 0U) && (uart->data_bits >= 5U) && (uart->data_bits <= 9U) &&
                    (uart->stop_bits >= 1U) && (uart->stop_bits <= 2U) &&
                    ((uint32_t)uart->parity <= (uint32_t)PROTOCOL_UART_PARITY_ODD) &&
                    (uart->inverted <= 1U)) ?
                       1U :
                       0U;
        }

        case PROTOCOL_TYPE_IIC:
            return ((Protocol_Channel_Is_Valid(config->decoder.iic.scl_channel) != 0U) &&
                    (Protocol_Channel_Is_Valid(config->decoder.iic.sda_channel) != 0U) &&
                    (config->decoder.iic.scl_channel != config->decoder.iic.sda_channel) &&
                    ((config->decoder.iic.address_bits == 7U) ||
                     (config->decoder.iic.address_bits == 10U))) ?
                       1U :
                       0U;

        case PROTOCOL_TYPE_SPI: {
            const Protocol_SPI_Config_t *spi = &config->decoder.spi;
            uint8_t has_mosi = Protocol_Channel_Is_Valid(spi->mosi_channel);
            uint8_t has_miso = Protocol_Channel_Is_Valid(spi->miso_channel);
            uint8_t has_cs = Protocol_Channel_Is_Valid(spi->cs_channel);

            if ((Protocol_Channel_Is_Valid(spi->clk_channel) == 0U) ||
                ((has_mosi == 0U) && (spi->mosi_channel != PROTOCOL_CHANNEL_UNUSED)) ||
                ((has_miso == 0U) && (spi->miso_channel != PROTOCOL_CHANNEL_UNUSED)) ||
                ((has_cs == 0U) && (spi->cs_channel != PROTOCOL_CHANNEL_UNUSED)) ||
                ((has_mosi == 0U) && (has_miso == 0U)) || (spi->clock_mode > 3U) ||
                ((uint32_t)spi->bit_order > (uint32_t)PROTOCOL_BIT_ORDER_LSB_FIRST) ||
                (spi->word_bits == 0U) || (spi->word_bits > 16U) || (spi->cs_active_level > 1U))
            {
                return 0U;
            }
            if (((has_mosi != 0U) && (spi->mosi_channel == spi->clk_channel)) ||
                ((has_miso != 0U) && (spi->miso_channel == spi->clk_channel)) ||
                ((has_cs != 0U) && (spi->cs_channel == spi->clk_channel)) ||
                ((has_mosi != 0U) && (has_miso != 0U) &&
                 (spi->mosi_channel == spi->miso_channel)) ||
                ((has_cs != 0U) && (has_mosi != 0U) && (spi->cs_channel == spi->mosi_channel)) ||
                ((has_cs != 0U) && (has_miso != 0U) && (spi->cs_channel == spi->miso_channel)))
            {
                return 0U;
            }
            return 1U;
        }

        default:
            return 0U;
    }
}

/**
 * @brief  持有状态锁时确定结束状态，并生成这次会话的事件副本
 */
static void Protocol_Set_Final_State(Protocol_State_t state,
                                     Protocol_Event_Type_t event_type)
{
    Protocol_Event_t event;
    Protocol_Event_Callback_t callback;
    void *context;

    (void)xSemaphoreTake(protocol_mutex, portMAX_DELAY);
    if (protocol_cancel_requested != 0U)
    {
        state = PROTOCOL_STATE_CANCELLED;
        event_type = PROTOCOL_EVENT_CANCELLED;
    }
    if ((state == PROTOCOL_STATE_COMPLETE) &&
        (protocol_info.dropped_count != 0U))
    {
        state = PROTOCOL_STATE_TRUNCATED;
    }
    if (state == PROTOCOL_STATE_ERROR)
    {
        protocol_info.error_count++;
    }
    protocol_info.state = state;
    event.type = event_type;
    Protocol_Fill_Info(&event.info);
    callback = protocol_event_callback;
    context = protocol_event_context;
    (void)xSemaphoreGive(protocol_mutex);

    if (callback != NULL)
    {
        callback(&event, context);
    }
}

/**
 * @brief  持有LA查询视图并按预算运行增量解析
 */
static void Protocol_Task(void *argument)
{
    (void)argument;

    for (;;)
    {
        const Protocol_Decoder_Ops_t *ops;
        LA_Service_Query_View_t view;
        LA_Service_Capture_Lease_t lease;
        Protocol_Config_t config;
        Protocol_Info_t info;
        Protocol_Result_t result = PROTOCOL_OK;
        uint8_t complete = 0U;
        uint8_t opened = 0U;
        uint8_t view_opened = 0U;
        uint32_t reported_count = 0U;
        uint32_t reported_dropped = 0U;
        TickType_t last_report_tick = xTaskGetTickCount();

        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        (void)xSemaphoreTake(protocol_mutex, portMAX_DELAY);
        config = protocol_config;
        info = protocol_info;
        lease = protocol_capture_lease;
        protocol_capture_lease.token = 0U;
        protocol_capture_lease.capture_id = 0U;
        (void)xSemaphoreGive(protocol_mutex);
        ops = Protocol_Get_Ops(config.protocol);

        while ((ops != NULL) && (protocol_cancel_requested == 0U))
        {
            LA_Service_Result_t query_result;

            query_result = LA_Service_Query_Try_Begin(&view);
            if (query_result == LA_SERVICE_OK)
            {
                view_opened = 1U;
                break;
            }
            if (query_result != LA_SERVICE_ERROR_BUSY)
            {
                result = PROTOCOL_ERROR_LA_SERVICE;
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(PROTOCOL_QUERY_RETRY_MS));
        }
        if (ops == NULL)
        {
            result = PROTOCOL_ERROR_DECODER;
        }
        if ((view_opened != 0U) && (protocol_cancel_requested == 0U))
        {
            (void)xSemaphoreTake(protocol_mutex, portMAX_DELAY);
            protocol_info.state = PROTOCOL_STATE_RUNNING;
            (void)xSemaphoreGive(protocol_mutex);
            result = ops->open(&config, &info, &view,
                               Protocol_Emit_Record, NULL);
            if (result == PROTOCOL_OK)
            {
                opened = 1U;
            }
        }

        while ((opened != 0U) && (result == PROTOCOL_OK) && (complete == 0U) &&
               (protocol_cancel_requested == 0U))
        {
            result = ops->process(PROTOCOL_PROCESS_BUDGET, &complete);
            Protocol_Publish_Records();

            (void)xSemaphoreTake(protocol_mutex, portMAX_DELAY);
            if (((protocol_info.result_count != reported_count) ||
                 (protocol_info.dropped_count != reported_dropped)) &&
                ((xTaskGetTickCount() - last_report_tick) >=
                 pdMS_TO_TICKS(PROTOCOL_NOTIFY_PERIOD_MS)))
            {
                reported_count = protocol_info.result_count;
                reported_dropped = protocol_info.dropped_count;
                last_report_tick = xTaskGetTickCount();
                (void)xSemaphoreGive(protocol_mutex);
                Protocol_Report_Event(PROTOCOL_EVENT_UPDATED);
            }
            else
            {
                (void)xSemaphoreGive(protocol_mutex);
            }
            taskYIELD();
        }

        if (opened != 0U)
        {
            ops->close();
        }
        Protocol_Publish_Records();
        if (view_opened != 0U)
        {
            (void)LA_Service_Query_End(&view);
        }
        (void)LA_Service_Capture_Release(&lease);

        if (result != PROTOCOL_OK)
        {
            Protocol_Set_Final_State(PROTOCOL_STATE_ERROR,
                                     PROTOCOL_EVENT_ERROR);
        }
        else
        {
            Protocol_Set_Final_State(PROTOCOL_STATE_COMPLETE,
                                     PROTOCOL_EVENT_COMPLETE);
        }
    }
}

/* ================================================== */

/**
 * @brief  初始化协议状态锁和服务任务
 * @note   在LA服务初始化后、调度器启动前调用
 */
uint8_t Protocol_Service_Init(void)
{
    if (protocol_task_handle != NULL)
    {
        return 0U;
    }

    protocol_mutex = xSemaphoreCreateMutex();
    if (protocol_mutex == NULL)
    {
        return 1U;
    }

    protocol_info.state = PROTOCOL_STATE_IDLE;
    protocol_info.session_id = 0U;
    protocol_info.capture_id = 0U;
    protocol_info.result_count = 0U;
    protocol_info.dropped_count = 0U;
    protocol_info.error_count = 0U;
    if (xTaskCreate(Protocol_Task,
                    "Protocol",
                    PROTOCOL_TASK_STACK_DEPTH,
                    NULL,
                    PROTOCOL_TASK_PRIORITY,
                    &protocol_task_handle) != pdPASS)
    {
        vSemaphoreDelete(protocol_mutex);
        protocol_mutex = NULL;
        return 1U;
    }

    return 0U;
}

/**
 * @brief  设置协议解析事件回调
 * @note   回调由解析服务任务执行，事件指针只在回调中有效
 */
void Protocol_Service_Set_Event_Callback(Protocol_Event_Callback_t callback,
                                         void *context)
{
    if (protocol_mutex == NULL)
    {
        return;
    }

    (void)xSemaphoreTake(protocol_mutex, portMAX_DELAY);
    protocol_event_callback = callback;
    protocol_event_context = context;
    (void)xSemaphoreGive(protocol_mutex);
}

/**
 * @brief  启动对已结束采集数据的协议解析
 */
Protocol_Result_t Protocol_Service_Start(const Protocol_Config_t *config)
{
    LA_Service_Info_t la_info;
    LA_Service_Capture_Lease_t lease = {0U, 0U};
    LA_Service_Result_t la_result;
    Protocol_Config_t resolved_config;
    uint32_t capture_end;

    if ((protocol_mutex == NULL) || (protocol_task_handle == NULL))
    {
        return PROTOCOL_ERROR_NOT_READY;
    }
    if (Protocol_Config_Is_Valid(config) == 0U)
    {
        return PROTOCOL_ERROR_INVALID_PARAMETER;
    }
    la_result = LA_Service_Capture_Acquire(&lease, &la_info);
    if (la_result != LA_SERVICE_OK)
    {
        if (la_result == LA_SERVICE_ERROR_BUSY)
        {
            return PROTOCOL_ERROR_BUSY;
        }
        return (la_result == LA_SERVICE_ERROR_NOT_READY) ?
                PROTOCOL_ERROR_NOT_READY : PROTOCOL_ERROR_LA_SERVICE;
    }
    if (la_info.cutoff_tick == 0U)
    {
        (void)LA_Service_Capture_Release(&lease);
        return PROTOCOL_ERROR_NOT_READY;
    }

    resolved_config = *config;
    capture_end = la_info.cutoff_tick - 1U;
    if (resolved_config.end_tick == UINT32_MAX)
    {
        resolved_config.end_tick = capture_end;
    }
    if ((resolved_config.start_tick > resolved_config.end_tick) ||
        (resolved_config.end_tick > capture_end))
    {
        (void)LA_Service_Capture_Release(&lease);
        return PROTOCOL_ERROR_INVALID_PARAMETER;
    }

    (void)xSemaphoreTake(protocol_mutex, portMAX_DELAY);
    if ((protocol_info.state == PROTOCOL_STATE_STARTING) ||
        (protocol_info.state == PROTOCOL_STATE_RUNNING))
    {
        (void)xSemaphoreGive(protocol_mutex);
        (void)LA_Service_Capture_Release(&lease);
        return PROTOCOL_ERROR_BUSY;
    }

    protocol_config = resolved_config;
    protocol_capture_lease = lease;
    protocol_pending_count = 0U;
    protocol_pending_errors = 0U;
    protocol_cancel_requested = 0U;
    protocol_info.session_id++;
    if (protocol_info.session_id == 0U)
    {
        protocol_info.session_id = 1U;
    }
    protocol_info.timestamp_hz = la_info.timestamp_hz;
    protocol_info.capture_id = lease.capture_id;
    protocol_info.start_tick = resolved_config.start_tick;
    protocol_info.end_tick = resolved_config.end_tick;
    protocol_info.result_count = 0U;
    protocol_info.dropped_count = 0U;
    protocol_info.error_count = 0U;
    protocol_info.protocol = resolved_config.protocol;
    protocol_info.state = PROTOCOL_STATE_STARTING;
    protocol_info.config = resolved_config;
    (void)xSemaphoreGive(protocol_mutex);

    xTaskNotifyGive(protocol_task_handle);
    return PROTOCOL_OK;
}

/**
 * @brief  请求取消当前解析会话
 * @note   服务任务负责关闭解析器、释放查询视图并上报取消事件
 */
Protocol_Result_t Protocol_Service_Cancel(void)
{
    Protocol_State_t state;

    if (protocol_mutex == NULL)
    {
        return PROTOCOL_ERROR_NOT_READY;
    }

    (void)xSemaphoreTake(protocol_mutex, portMAX_DELAY);
    state = protocol_info.state;
    if ((state != PROTOCOL_STATE_STARTING) &&
        (state != PROTOCOL_STATE_RUNNING))
    {
        (void)xSemaphoreGive(protocol_mutex);
        return PROTOCOL_ERROR_STATE;
    }
    protocol_cancel_requested = 1U;
    (void)xSemaphoreGive(protocol_mutex);
    return PROTOCOL_OK;
}

/**
 * @brief  丢弃当前解析结果并恢复空闲状态
 */
Protocol_Result_t Protocol_Service_Reset(void)
{
    if (protocol_mutex == NULL)
    {
        return PROTOCOL_ERROR_NOT_READY;
    }

    (void)xSemaphoreTake(protocol_mutex, portMAX_DELAY);
    if ((protocol_info.state == PROTOCOL_STATE_STARTING) ||
        (protocol_info.state == PROTOCOL_STATE_RUNNING))
    {
        (void)xSemaphoreGive(protocol_mutex);
        return PROTOCOL_ERROR_BUSY;
    }

    protocol_info.result_count = 0U;
    protocol_info.dropped_count = 0U;
    protocol_info.error_count = 0U;
    protocol_info.state = PROTOCOL_STATE_IDLE;
    (void)xSemaphoreGive(protocol_mutex);
    return PROTOCOL_OK;
}

/**
 * @brief  读取当前协议解析会话状态
 */
Protocol_Result_t Protocol_Service_Get_Info(Protocol_Info_t *info)
{
    if (info == NULL)
    {
        return PROTOCOL_ERROR_INVALID_PARAMETER;
    }
    if (protocol_mutex == NULL)
    {
        return PROTOCOL_ERROR_NOT_READY;
    }

    (void)xSemaphoreTake(protocol_mutex, portMAX_DELAY);
    Protocol_Fill_Info(info);
    (void)xSemaphoreGive(protocol_mutex);
    return PROTOCOL_OK;
}

/**
 * @brief  在同一锁内核对会话并批量复制已发布记录
 */
static Protocol_Result_t Protocol_Copy_Records(uint32_t expected_session_id,
                                                uint32_t start_index,
                                                Protocol_Record_t *records,
                                                uint16_t max_count,
                                                uint16_t *record_count)
{
    uint16_t copy_count;

    if (record_count != NULL)
    {
        *record_count = 0U;
    }
    if ((records == NULL) || (max_count == 0U) || (record_count == NULL))
    {
        return PROTOCOL_ERROR_INVALID_PARAMETER;
    }
    if (protocol_mutex == NULL)
    {
        return PROTOCOL_ERROR_NOT_READY;
    }

    (void)xSemaphoreTake(protocol_mutex, portMAX_DELAY);
    if ((expected_session_id != 0U) &&
        (expected_session_id != protocol_info.session_id))
    {
        (void)xSemaphoreGive(protocol_mutex);
        return PROTOCOL_ERROR_STATE;
    }
    if (start_index >= protocol_info.result_count)
    {
        (void)xSemaphoreGive(protocol_mutex);
        return PROTOCOL_OK;
    }

    copy_count = max_count;
    if (copy_count > PROTOCOL_RECORD_READ_LIMIT)
    {
        copy_count = PROTOCOL_RECORD_READ_LIMIT;
    }
    if ((uint32_t)copy_count > (protocol_info.result_count - start_index))
    {
        copy_count = (uint16_t)(protocol_info.result_count - start_index);
    }
    for (uint16_t i = 0U; i < copy_count; i++)
    {
        records[i] = protocol_records[start_index + i];
    }
    *record_count = copy_count;
    (void)xSemaphoreGive(protocol_mutex);
    return PROTOCOL_OK;
}

/**
 * @brief  批量复制调用时当前会话的已发布记录
 */
Protocol_Result_t Protocol_Service_Get_Records(uint32_t start_index,
                                                Protocol_Record_t *records,
                                                uint16_t max_count,
                                                uint16_t *record_count)
{
    return Protocol_Copy_Records(0U, start_index, records,
                                  max_count, record_count);
}

/**
 * @brief  只在指定会话仍为当前会话时复制已发布记录
 * @note   会话匹配和复制共用同一互斥锁
 */
Protocol_Result_t Protocol_Service_Get_Records_For_Session(
                                                uint32_t expected_session_id,
                                                uint32_t start_index,
                                                Protocol_Record_t *records,
                                                uint16_t max_count,
                                                uint16_t *record_count)
{
    if (expected_session_id == 0U)
    {
        if (record_count != NULL)
        {
            *record_count = 0U;
        }
        return PROTOCOL_ERROR_INVALID_PARAMETER;
    }
    return Protocol_Copy_Records(expected_session_id, start_index,
                                  records, max_count, record_count);
}

/**
 * @brief  查找第一个可能与target_tick相交的记录
 */
Protocol_Result_t Protocol_Service_Find_First_Record_For_Session(uint32_t expected_session_id,
                                                                 uint32_t target_tick,
                                                                 uint32_t *record_index)
{
    uint32_t lower = 0U;
    uint32_t upper;

    if ((expected_session_id == 0U) || (record_index == NULL))
    {
        return PROTOCOL_ERROR_INVALID_PARAMETER;
    }
    if (protocol_mutex == NULL)
    {
        return PROTOCOL_ERROR_NOT_READY;
    }

    (void)xSemaphoreTake(protocol_mutex, portMAX_DELAY);
    if (expected_session_id != protocol_info.session_id)
    {
        (void)xSemaphoreGive(protocol_mutex);
        return PROTOCOL_ERROR_STATE;
    }

    upper = protocol_info.result_count;
    while (lower < upper)
    {
        uint32_t middle = lower + ((upper - lower) / 2U);

        if (protocol_records[middle].start_tick < target_tick)
        {
            lower = middle + 1U;
        }
        else
        {
            upper = middle;
        }
    }

    /* UART的TX/RX帧可以重叠 */
    while ((lower != 0U) && (protocol_records[lower - 1U].end_tick >= target_tick))
    {
        lower--;
    }
    *record_index = lower;
    (void)xSemaphoreGive(protocol_mutex);
    return PROTOCOL_OK;
}
