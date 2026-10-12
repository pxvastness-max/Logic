/**
 * @file   la_service.c
 * @brief  逻辑分析仪采集服务实现
 */

/* ================================================== */

#include "la/la_service_internal.h"
#include "la/la_storage.h"
#include "la/la_transfer_internal.h"
#include "log/log_service.h"
#include "stddef.h"
#include "string.h"

/* ========================= 配置 ========================= */

#define LA_SERVICE_TASK_STACK_DEPTH 512U                              /* 服务任务栈深度 */
#define LA_SERVICE_TASK_PRIORITY    (configMAX_PRIORITIES - 2U)       /* 服务任务优先级 */
#define LA_SERVICE_DMA_HALF_COUNT   (BSP_LA_IC_DMA_BUFFER_COUNT / 2U) /* DMA半区数据量 */

#if (LA_SERVICE_CHANNEL_COUNT != BSP_LA_IC_CHANNEL_COUNT)
    #error "LA service channel count does not match BSP channel count"
#endif

#if (LA_STORAGE_CHANNEL_COUNT != LA_SERVICE_CHANNEL_COUNT)
    #error "LA storage channel count does not match service channel count"
#endif

#if (LA_STORAGE_BLOCK_CAPACITY != LA_SERVICE_DMA_HALF_COUNT)
    #error "LA storage block must equal one DMA half buffer"
#endif

/* ================================================== */

typedef struct
{
    LA_Service_Info_t metadata;                   /* 待发布的文件采集信息 */
    uint32_t last_tick[LA_SERVICE_CHANNEL_COUNT]; /* 各通道最后提交的时间戳 */
    uint32_t token;                               /* 当前独占导入凭据 */
    TaskHandle_t owner;                           /* Begin调用任务 */
    uint8_t failed;                               /* 存储失败后只允许Abort */
} LA_Service_Import_Handle_t;

typedef struct
{
    const uint32_t *data;     /* 直接源或压缩暂存区 */
    uint16_t count;           /* 去除截止值和重复值后的数量 */
    uint32_t duplicate_count; /* 本段删除数量 */
    uint32_t last_timestamp;  /* 本段最后保留的时间戳 */
    uint8_t last_valid;       /* 本段处理后是否存在历史时间戳 */
    uint8_t compacted;        /* 1表示data指向共享暂存区 */
} LA_Service_Prepared_Range_t;

/* ================================================== */

LA_Service_Shared_t la_service_shared = {.state = LA_SERVICE_STATE_RESET};

static TaskHandle_t la_service_task_handle = NULL; /* 服务任务句柄 */
/* 停止原因 */
static volatile LA_Service_Stop_Reason_t la_service_stop_reason = LA_SERVICE_STOP_NONE;
static volatile uint8_t la_service_start_ready = 0U; /* 启动结果是否已发布 */
static const uint32_t *la_service_pending_data[LA_SERVICE_CHANNEL_COUNT][2]; /* DMA半区地址 */
static uint16_t la_service_pending_count[LA_SERVICE_CHANNEL_COUNT][2];       /* DMA半区数据量 */
static volatile uint32_t la_service_contaminated_mask = 0U; /* DMA已开始复用的未提交半区 */
static uint32_t la_service_timestamp_hz = 0U;               /* 实际时间戳频率 */
static uint32_t la_service_cutoff_tick = 0U;                /* 实际截止计数值 */
static uint32_t la_service_overcapture_mask = 0U;           /* TIM捕获溢出位图 */
static uint32_t la_service_overcapture_count[LA_SERVICE_CHANNEL_COUNT]; /* 各通道CCxOF观察次数 */
static uint32_t la_service_duplicate_count[LA_SERVICE_CHANNEL_COUNT];   /* 各通道已删除重复记录数 */
static uint32_t la_service_last_timestamp[LA_SERVICE_CHANNEL_COUNT];    /* 各通道最后提交时间戳 */
static uint8_t la_service_last_timestamp_valid[LA_SERVICE_CHANNEL_COUNT]; /* 最后时间戳是否有效 */
/* 六路串行复用的异常半区暂存 */
static uint32_t la_service_compact_buffer[LA_SERVICE_DMA_HALF_COUNT];
static uint8_t la_service_finalize_failed = 0U;           /* BSP收尾是否无法验证硬件停稳 */
static LA_Service_Event_Callback_t la_service_event_callback = NULL; /* 任务上下文事件回调 */
static void *la_service_event_context = NULL;                        /* 事件回调上下文 */

static LA_Service_Import_Handle_t la_service_import; /* 单实例SDRAM导入 */

static uint32_t la_service_next_import_token = 0U; /* 导入凭据代数 */

/* ================================================== */

static void LA_Service_Task(void *argument);
static void LA_Service_BSP_Event_Callback(const BSP_LA_IC_Event_t *event, void *context);

/* ================================================== */

/**
 * @brief  把采集搬运错误原因转换为简短日志文本
 */
static const char *LA_Service_Stop_Reason_Text(LA_Service_Stop_Reason_t reason)
{
    switch (reason)
    {
        case LA_SERVICE_STOP_STORAGE_FULL:
            return "storage_full";
        case LA_SERVICE_STOP_DMA_OVERRUN:
            return "dma_overrun";
        case LA_SERVICE_STOP_DMA_ERROR:
            return "dma_error";
        case LA_SERVICE_STOP_TIM_OVERCAPTURE:
            return "tim_overcapture";
        case LA_SERVICE_STOP_HARDWARE_ERROR:
            return "hardware_error";
        case LA_SERVICE_STOP_TRANSFER_TIMEOUT:
            return "transfer_timeout";
        default:
            return "unknown";
    }
}

/**
 * @brief  输出LA采集任务的最终错误诊断
 * @note   只在LA服务任务上下文调用，不允许从DMA/TIM中断直接写日志队列
 */
static void LA_Service_Log_Transfer_Error(const LA_Service_Info_t *info)
{
    (void)Log_Printf("la transfer error: %s cut=%lu dirty=%03lX oc=%02lX\r\n",
                     LA_Service_Stop_Reason_Text(info->stop_reason),
                     (unsigned long)info->cutoff_tick,
                     (unsigned long)la_service_contaminated_mask,
                     (unsigned long)info->overcapture_mask);
}

/* ================================================== */

/**
 * @brief  填充逻辑分析仪服务信息
 * @note   调用者负责保证读取期间服务状态不会变化
 */
static void LA_Service_Fill_Info(LA_Service_Info_t *info)
{
    taskENTER_CRITICAL();
    info->state = la_service_shared.state;
    info->capture_id = la_service_shared.capture_id;
    info->stop_reason = la_service_stop_reason;
    info->timestamp_hz = la_service_timestamp_hz;
    info->deadline_tick = la_service_shared.deadline_tick;
    info->cutoff_tick = la_service_cutoff_tick;
    info->overcapture_mask = la_service_overcapture_mask;
    info->initial_levels = la_service_shared.initial_levels;
    info->finalize_failed = la_service_finalize_failed;
    taskEXIT_CRITICAL();

    for (uint8_t i = 0U; i < LA_SERVICE_CHANNEL_COUNT; i++)
    {
        info->data_count[i] = LA_Storage_Get_Count(i);
    }
}

/**
 * @brief  发布采集结束状态和对应的事件数据副本
 * @note   状态发布与信息复制属于同一个临界区，回调在解锁后调用
 */
static void LA_Service_Publish_Terminal(LA_Service_State_t state)
{
    LA_Service_Event_t event;
    LA_Service_Event_Callback_t callback;
    void *context;

    (void)xSemaphoreTake(la_service_shared.storage_mutex, portMAX_DELAY);
    taskENTER_CRITICAL();
    la_service_shared.state = state;
    la_service_shared.pending_mask = 0U;
    callback = la_service_event_callback;
    context = la_service_event_context;
    event.type =
        (state == LA_SERVICE_STATE_DONE) ? LA_SERVICE_EVENT_COMPLETE : LA_SERVICE_EVENT_ERROR;
    LA_Service_Fill_Info(&event.info);
    taskEXIT_CRITICAL();

    (void)xSemaphoreGive(la_service_shared.storage_mutex);

    if (state == LA_SERVICE_STATE_ERROR)
    {
        LA_Service_Log_Transfer_Error(&event.info);
    }

    if (callback == NULL)
    {
        return;
    }

    callback(&event, context);
}

/**
 * @brief  请求停止采集
 */
static void LA_Service_Request_Stop(LA_Service_Stop_Reason_t reason)
{
    uint32_t cutoff_tick;

    taskENTER_CRITICAL();
    if (la_service_shared.state == LA_SERVICE_STATE_RUNNING)
    {
        la_service_shared.state = LA_SERVICE_STATE_STOPPING;
        la_service_stop_reason = reason;
        taskEXIT_CRITICAL();

        if (BSP_LA_IC_Stop(&cutoff_tick) == BSP_LA_IC_OK)
        {
            la_service_cutoff_tick = cutoff_tick;
        }
        /* 停止失败不覆盖触发收尾的首次异常 */
        return;
    }
    if ((la_service_shared.state == LA_SERVICE_STATE_STOPPING) &&
        ((la_service_stop_reason == LA_SERVICE_STOP_DEADLINE) ||
         (la_service_stop_reason == LA_SERVICE_STOP_USER)))
    {
        /* 截止IRQ可能在CPU复制期间到达，不能把之后的失败丢掉或重试 */
        la_service_stop_reason = reason;
    }
    taskEXIT_CRITICAL();
}

/**
 * @brief  扫描一段升序时间戳，必要时压缩到共享暂存区
 */
static void LA_Service_Prepare_Range(uint8_t channel,
                                     const uint32_t *data,
                                     uint16_t count,
                                     uint32_t cutoff_tick,
                                     LA_Service_Prepared_Range_t *prepared)
{
    uint32_t previous = la_service_last_timestamp[channel];
    uint8_t previous_valid = la_service_last_timestamp_valid[channel];
    uint8_t compacted =
        ((LA_Storage_Get_Count(channel) % LA_STORAGE_BLOCK_CAPACITY) != 0U) ? 1U : 0U;
    uint16_t output_count = 0U;

    prepared->duplicate_count = 0U;
    prepared->last_timestamp = previous;
    prepared->last_valid = previous_valid;

    for (uint16_t i = 0U; i < count; i++)
    {
        uint32_t current = data[i];

        if (current >= cutoff_tick)
        {
            break;
        }
        if ((previous_valid != 0U) && (current == previous))
        {
            if (compacted == 0U)
            {
                if (output_count != 0U)
                {
                    memcpy(la_service_compact_buffer, data, (size_t)output_count * sizeof(*data));
                }
                compacted = 1U;
            }
            prepared->duplicate_count++;
            continue;
        }

        if (compacted != 0U)
        {
            la_service_compact_buffer[output_count] = current;
        }
        output_count++;
        previous = current;
        previous_valid = 1U;
    }

    prepared->data = (compacted != 0U) ? la_service_compact_buffer : data;
    prepared->count = output_count;
    prepared->last_timestamp = previous;
    prepared->last_valid = previous_valid;
    prepared->compacted = compacted;
}

/**
 * @brief  把稳定源数据按SDRAM物理块剩余容量分段追加
 */
static LA_Service_Result_t LA_Service_Append_Stable_Range(uint8_t channel,
                                                          const uint32_t *data,
                                                          uint16_t count,
                                                          uint32_t cutoff_tick)
{
    uint16_t copied = 0U;

    while (copied < count)
    {
        uint16_t space = (uint16_t)(LA_STORAGE_BLOCK_CAPACITY -
                                    (LA_Storage_Get_Count(channel) % LA_STORAGE_BLOCK_CAPACITY));
        uint16_t chunk = (uint16_t)(count - copied);

        if (chunk > space)
        {
            chunk = space;
        }
        LA_Service_Result_t result = LA_Transfer_Append(channel, data + copied, chunk, cutoff_tick);
        if (result != LA_SERVICE_OK)
        {
            return result;
        }
        copied = (uint16_t)(copied + chunk);
    }
    return LA_SERVICE_OK;
}

/**
 * @brief  校验并释放已安全复制到暂存区的DMA半区
 */
static LA_Service_Result_t LA_Service_Release_DMA_Half(uint8_t channel, uint8_t half)
{
    uint32_t pending_bit = LA_SERVICE_PENDING_BIT(channel, half);

    taskENTER_CRITICAL();
    if (((la_service_contaminated_mask & pending_bit) != 0U) ||
        ((la_service_shared.pending_mask & pending_bit) == 0U) ||
        (BSP_LA_IC_Validate_DMA_Half((BSP_LA_IC_Channel_t)channel, half) != BSP_LA_IC_OK))
    {
        la_service_contaminated_mask |= pending_bit;
        if ((la_service_shared.state == LA_SERVICE_STATE_STOPPING) &&
            ((la_service_stop_reason == LA_SERVICE_STOP_USER) ||
             (la_service_stop_reason == LA_SERVICE_STOP_DEADLINE)))
        {
            la_service_stop_reason = LA_SERVICE_STOP_DMA_OVERRUN;
        }
        taskEXIT_CRITICAL();
        LA_Service_Request_Stop(LA_SERVICE_STOP_DMA_OVERRUN);
        return LA_SERVICE_ERROR_HARDWARE;
    }

    la_service_shared.consumed_index[channel] = (half == 0U) ? LA_SERVICE_DMA_HALF_COUNT : 0U;
    la_service_shared.pending_mask &= ~pending_bit;
    taskEXIT_CRITICAL();
    return LA_SERVICE_OK;
}

/**
 * @brief  搬运一个DMA半缓冲区
 */
static LA_Service_Result_t LA_Service_Commit_DMA_Half(
        uint8_t channel, uint16_t count, void *context)
{
    uint8_t half = *(const uint8_t *)context;
    uint32_t pending_bit = LA_SERVICE_PENDING_BIT(channel, half);

    taskENTER_CRITICAL();
    if (((la_service_contaminated_mask & pending_bit) != 0U) ||
        ((la_service_shared.pending_mask & pending_bit) == 0U) ||
        (BSP_LA_IC_Validate_DMA_Half((BSP_LA_IC_Channel_t)channel, half) != BSP_LA_IC_OK))
    {
        la_service_contaminated_mask |= pending_bit;
        if ((la_service_shared.state == LA_SERVICE_STATE_STOPPING) &&
            ((la_service_stop_reason == LA_SERVICE_STOP_USER) ||
             (la_service_stop_reason == LA_SERVICE_STOP_DEADLINE)))
        {
            la_service_stop_reason = LA_SERVICE_STOP_DMA_OVERRUN;
        }
        taskEXIT_CRITICAL();
        LA_Service_Request_Stop(LA_SERVICE_STOP_DMA_OVERRUN);
        return LA_SERVICE_ERROR_HARDWARE;
    }
    if ((count != 0U) && (LA_Storage_Commit(channel, count) != LA_STORAGE_OK))
    {
        taskEXIT_CRITICAL();
        return LA_SERVICE_ERROR_STORAGE;
    }
    la_service_shared.consumed_index[channel] = (half == 0U) ?
                                               LA_SERVICE_DMA_HALF_COUNT : 0U;
    la_service_shared.pending_mask &= ~pending_bit;
    taskEXIT_CRITICAL();
    return LA_SERVICE_OK;
}

/**
 * @brief  检查并搬运一个 DMA 半区的数据
 * @note   数据写入存储或复制到暂存区后才释放源半区；只在读取和更新共享状态时短暂关中断
 */
static LA_Service_Result_t LA_Service_Copy_DMA_Half(uint8_t channel,
                                                    uint8_t half)
{
    uint32_t pending_bit = LA_SERVICE_PENDING_BIT(channel, half);
    const uint32_t *data;
    uint16_t count;
    LA_Service_Prepared_Range_t prepared;
    LA_Service_Result_t service_result;

    taskENTER_CRITICAL();
    if ((la_service_shared.pending_mask & pending_bit) == 0U)
    {
        taskEXIT_CRITICAL();
        return LA_SERVICE_OK;
    }
    data = la_service_pending_data[channel][half];
    count = la_service_pending_count[channel][half];
    taskEXIT_CRITICAL();

    (void)xSemaphoreTake(la_service_shared.storage_mutex, portMAX_DELAY);
    taskENTER_CRITICAL();
    if (la_service_stop_reason == LA_SERVICE_STOP_DMA_OVERRUN)
    {
        taskEXIT_CRITICAL();
        (void)xSemaphoreGive(la_service_shared.storage_mutex);
        return LA_SERVICE_OK;
    }
    taskEXIT_CRITICAL();

    LA_Service_Prepare_Range(channel, data, count, la_service_shared.deadline_tick, &prepared);

    if (prepared.compacted == 0U)
    {
        /* 常态路径：块对齐且没有重复，DMA半区直接复制到SDRAM */
        service_result = LA_Transfer_Append_Checked(channel,
                                                    data,
                                                    prepared.count,
                                                    la_service_shared.deadline_tick,
                                                    LA_Service_Commit_DMA_Half,
                                                    &half);
    }
    else
    {
        /* 异常/错位路径：先验证并释放源半区，再从稳定暂存区分段写SDRAM */
        service_result = LA_Service_Release_DMA_Half(channel, half);
        if (service_result == LA_SERVICE_OK)
        {
            service_result = LA_Service_Append_Stable_Range(channel,
                                                            prepared.data,
                                                            prepared.count,
                                                            la_service_shared.deadline_tick);
        }
    }

    if (service_result == LA_SERVICE_OK)
    {
        la_service_duplicate_count[channel] += prepared.duplicate_count;
        la_service_last_timestamp[channel] = prepared.last_timestamp;
        la_service_last_timestamp_valid[channel] = prepared.last_valid;
    }
    (void)xSemaphoreGive(la_service_shared.storage_mutex);
    return service_result;
}

/**
 * @brief  搬运全部待处理DMA半缓冲区
 */
static LA_Service_Result_t LA_Service_Process_Pending(void)
{
    for (uint8_t channel = 0U; channel < LA_SERVICE_CHANNEL_COUNT; channel++)
    {
        uint8_t first_half =
                (la_service_shared.consumed_index[channel] == 0U) ? 0U : 1U;

        for (uint8_t i = 0U; i < 2U; i++)
        {
            uint8_t half = (uint8_t)(first_half ^ i);
            LA_Service_Result_t service_result =
                                LA_Service_Copy_DMA_Half(channel, half);

            if (service_result != LA_SERVICE_OK)
            {
                return service_result;
            }
        }
    }
    return LA_SERVICE_OK;
}

/**
 * @brief  恢复停稳时未被中断消费的完整半区
 */
static LA_Service_Result_t LA_Service_Recover_Pending(
                                const BSP_LA_IC_Final_Result_t *result)
{
    taskENTER_CRITICAL();
    uint32_t completed = la_service_shared.pending_mask | result->completed_half_mask;

    for (uint8_t channel = 0U; channel < LA_SERVICE_CHANNEL_COUNT; channel++)
    {
        uint8_t first_half = (la_service_shared.consumed_index[channel] == 0U) ? 0U : 1U;
        uint32_t first_bit = LA_SERVICE_PENDING_BIT(channel, first_half);
        uint32_t second_bit = LA_SERVICE_PENDING_BIT(channel, (first_half ^ 1U));

        if (((completed & second_bit) != 0U) && ((completed & first_bit) == 0U))
        {
            /* 后一半区已完整产生，却找不到先一半区：不能用短尾部掩盖缺口 */
            taskEXIT_CRITICAL();
            LA_Service_Request_Stop(LA_SERVICE_STOP_DMA_OVERRUN);
            return LA_SERVICE_ERROR_HARDWARE;
        }
        for (uint8_t half = 0U; half < 2U; half++)
        {
            uint32_t bit = LA_SERVICE_PENDING_BIT(channel, half);

            if ((completed & bit) == 0U)
            {
                continue;
            }
            if (BSP_LA_IC_Validate_DMA_Half((BSP_LA_IC_Channel_t)channel, half) != BSP_LA_IC_OK)
            {
                la_service_contaminated_mask |= bit;
                taskEXIT_CRITICAL();
                LA_Service_Request_Stop(LA_SERVICE_STOP_DMA_OVERRUN);
                return LA_SERVICE_ERROR_HARDWARE;
            }
            la_service_pending_data[channel][half] =
                             result->buffer[channel] + (half * LA_SERVICE_DMA_HALF_COUNT);
            la_service_pending_count[channel][half] = LA_SERVICE_DMA_HALF_COUNT;
        }
    }
    la_service_shared.pending_mask = completed;
    taskEXIT_CRITICAL();
    return LA_SERVICE_OK;
}

/**
 * @brief  扫描、去重并追加停稳后的尾部
 */
static LA_Service_Result_t LA_Service_Append_Tail_Range(
        uint8_t channel, const uint32_t *data, uint16_t count, uint32_t cutoff_tick)
{
    LA_Service_Prepared_Range_t prepared;
    LA_Service_Result_t result;

    LA_Service_Prepare_Range(channel, data, count, cutoff_tick, &prepared);
    if (prepared.compacted != 0U)
    {
        result =
            LA_Service_Append_Stable_Range(channel, prepared.data, prepared.count, cutoff_tick);
    }
    else
    {
        result = LA_Transfer_Append(channel, prepared.data, prepared.count, cutoff_tick);
    }

    if (result == LA_SERVICE_OK)
    {
        la_service_duplicate_count[channel] += prepared.duplicate_count;
        la_service_last_timestamp[channel] = prepared.last_timestamp;
        la_service_last_timestamp_valid[channel] = prepared.last_valid;
    }
    return result;
}

/**
 * @brief  搬运已停稳且完整半区已校验提交后的尾部
 */
static LA_Service_Result_t LA_Service_Copy_Tail(
                                const BSP_LA_IC_Final_Result_t *result)
{
    LA_Service_Result_t service_result = LA_SERVICE_OK;

    (void)xSemaphoreTake(la_service_shared.storage_mutex, portMAX_DELAY);
    for (uint8_t channel = 0U;
         (channel < LA_SERVICE_CHANNEL_COUNT) &&
         (service_result == LA_SERVICE_OK);
         channel++)
    {
        uint16_t consumed_index = la_service_shared.consumed_index[channel];
        uint16_t write_index = result->write_index[channel];

        if (write_index > BSP_LA_IC_DMA_BUFFER_COUNT)
        {
            write_index = 0U;
        }
        if (write_index == BSP_LA_IC_DMA_BUFFER_COUNT)
        {
            /* 未消费TC已恢复为完整后半区 */
            write_index = 0U;
        }

        if (write_index >= consumed_index)
        {
            service_result = LA_Service_Append_Tail_Range(
                                  channel,
                                  result->buffer[channel] + consumed_index,
                                  (uint16_t)(write_index - consumed_index),
                                  result->cutoff_tick);
        }
        else
        {
            service_result = LA_Service_Append_Tail_Range(
                    channel,
                    result->buffer[channel] + consumed_index,
                    (uint16_t)(BSP_LA_IC_DMA_BUFFER_COUNT - consumed_index),
                    result->cutoff_tick);
            if (service_result == LA_SERVICE_OK)
            {
                service_result = LA_Service_Append_Tail_Range(
                                      channel,
                                      result->buffer[channel],
                                      write_index,
                                      result->cutoff_tick);
            }
        }
    }

    (void)xSemaphoreGive(la_service_shared.storage_mutex);
    return service_result;
}

/**
 * @brief  比较软件删除的重复记录数与硬件观察到的CCxOF次数
 */
static uint32_t LA_Service_Check_Overcapture_Repair(const BSP_LA_IC_Final_Result_t *result)
{
    uint32_t mismatch_mask = 0U;
    uint32_t duplicate_total = 0U;
    uint32_t overcapture_total = 0U;

    for (uint8_t channel = 0U; channel < LA_SERVICE_CHANNEL_COUNT; channel++)
    {
        uint32_t duplicate_count = la_service_duplicate_count[channel];
        uint32_t overcapture_count = result->overcapture_count[channel];

        la_service_overcapture_count[channel] = overcapture_count;
        duplicate_total += duplicate_count;
        overcapture_total += overcapture_count;
        if (duplicate_count != overcapture_count)
        {
            mismatch_mask |= 1UL << channel;
        }
        if ((duplicate_count != 0U) || (overcapture_count != 0U))
        {
            (void)Log_Printf("la: overcapture repair D%u duplicate=%lu overcapture=%lu\r\n",
                             (unsigned int)(channel + 1U),
                             (unsigned long)duplicate_count,
                             (unsigned long)overcapture_count);
        }
    }

    if ((duplicate_total != 0U) || (overcapture_total != 0U) || (mismatch_mask != 0U))
    {
        (void)Log_Printf("la: overcapture duplicate=%lu overcapture=%lu mismatch=%02lX\r\n",
                         (unsigned long)duplicate_total,
                         (unsigned long)overcapture_total,
                         (unsigned long)mismatch_mask);
    }
    return mismatch_mask;
}

/**
 * @brief  完成逻辑分析仪采集收尾
 */
static void LA_Service_Finalize(void)
{
    BSP_LA_IC_Final_Result_t result;
    LA_Service_Result_t transfer_result = LA_SERVICE_OK;
    uint32_t repair_mismatch_mask = 0U;
    uint8_t save_remaining = ((la_service_stop_reason == LA_SERVICE_STOP_DEADLINE) ||
                              (la_service_stop_reason == LA_SERVICE_STOP_USER)) ?
                                 1U :
                                 0U;

    if (BSP_LA_IC_Finalize(&result) != BSP_LA_IC_OK)
    {
        taskENTER_CRITICAL();
        la_service_finalize_failed = 1U;
        if ((la_service_stop_reason == LA_SERVICE_STOP_DEADLINE) ||
            (la_service_stop_reason == LA_SERVICE_STOP_USER) ||
            (la_service_stop_reason == LA_SERVICE_STOP_NONE))
        {
            la_service_stop_reason = LA_SERVICE_STOP_HARDWARE_ERROR;
        }
        taskEXIT_CRITICAL();
        LA_Service_Publish_Terminal(LA_SERVICE_STATE_ERROR);
        return;
    }

    la_service_cutoff_tick = result.cutoff_tick;
    la_service_overcapture_mask = result.overcapture_mask;

    /* 即使输入事件源已停，最后一笔DMA请求仍可能在途 */
    if (save_remaining != 0U)
    {
        transfer_result = LA_Service_Recover_Pending(&result);
        if (transfer_result == LA_SERVICE_OK)
        {
            transfer_result = LA_Service_Process_Pending();
        }
    }

    if ((transfer_result == LA_SERVICE_OK) && (save_remaining != 0U))
    {
        transfer_result = LA_Service_Copy_Tail(&result);
    }
    if ((transfer_result == LA_SERVICE_OK) && (save_remaining != 0U))
    {
        repair_mismatch_mask = LA_Service_Check_Overcapture_Repair(&result);
    }

    if ((la_service_stop_reason == LA_SERVICE_STOP_DEADLINE) ||
        (la_service_stop_reason == LA_SERVICE_STOP_USER))
    {
        if (transfer_result == LA_SERVICE_ERROR_STORAGE)
        {
            la_service_stop_reason = LA_SERVICE_STOP_STORAGE_FULL;
        }
        else if (transfer_result == LA_SERVICE_ERROR_TIMEOUT)
        {
            la_service_stop_reason = LA_SERVICE_STOP_TRANSFER_TIMEOUT;
        }
        else if (transfer_result != LA_SERVICE_OK)
        {
            la_service_stop_reason = LA_SERVICE_STOP_HARDWARE_ERROR;
        }
        else if (repair_mismatch_mask != 0U)
        {
            la_service_stop_reason = LA_SERVICE_STOP_TIM_OVERCAPTURE;
        }
    }

    if ((la_service_stop_reason == LA_SERVICE_STOP_DEADLINE) ||
        (la_service_stop_reason == LA_SERVICE_STOP_USER))
    {
        LA_Service_Publish_Terminal(LA_SERVICE_STATE_DONE);
    }
    else
    {
        LA_Service_Publish_Terminal(LA_SERVICE_STATE_ERROR);
    }
}

/**
 * @brief  逻辑分析仪服务任务
 */
static void LA_Service_Task(void *argument)
{
    (void)argument;

    for (;;)
    {
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        if (la_service_shared.state == LA_SERVICE_STATE_RUNNING)
        {
            LA_Service_Result_t service_result = LA_Service_Process_Pending();

            if (service_result == LA_SERVICE_ERROR_STORAGE)
            {
                LA_Service_Request_Stop(LA_SERVICE_STOP_STORAGE_FULL);
            }
            else if (service_result == LA_SERVICE_ERROR_TIMEOUT)
            {
                LA_Service_Request_Stop(LA_SERVICE_STOP_TRANSFER_TIMEOUT);
            }
            else if (service_result != LA_SERVICE_OK)
            {
                LA_Service_Request_Stop(LA_SERVICE_STOP_HARDWARE_ERROR);
            }
        }

        if ((la_service_shared.state == LA_SERVICE_STATE_STOPPING) &&
            (la_service_start_ready != 0U))
        {
            LA_Service_Finalize();
        }
    }
}

/**
 * @brief  BSP输入捕获事件回调
 * @note   该函数在DMA或TIM中断上下文调用，只记录事件并通知服务任务
 */
static void LA_Service_BSP_Event_Callback(const BSP_LA_IC_Event_t *event, void *context)
{
    BaseType_t higher_priority_task_woken = pdFALSE;

    (void)context;

    if ((event->type == BSP_LA_IC_EVENT_DMA_HALF) ||
        (event->type == BSP_LA_IC_EVENT_DMA_FULL))
    {
        uint8_t half = (event->type == BSP_LA_IC_EVENT_DMA_HALF) ? 0U : 1U;
        uint32_t pending_bit;
        uint32_t reuse_bit;

        if (((la_service_shared.state != LA_SERVICE_STATE_STARTING) &&
             (la_service_shared.state != LA_SERVICE_STATE_RUNNING)) ||
            (event->channel >= BSP_LA_IC_CHANNEL_NONE))
        {
            return;
        }

        pending_bit = LA_SERVICE_PENDING_BIT(event->channel, half);
        reuse_bit = LA_SERVICE_PENDING_BIT(event->channel, (half ^ 1U));
        if ((la_service_shared.pending_mask & (pending_bit | reuse_bit)) != 0U)
        {
            uint32_t cutoff_tick = 0U;

            la_service_contaminated_mask |=
                    la_service_shared.pending_mask & (pending_bit | reuse_bit);
            la_service_stop_reason = LA_SERVICE_STOP_DMA_OVERRUN;
            la_service_shared.state = LA_SERVICE_STATE_STOPPING;
            if (BSP_LA_IC_Stop(&cutoff_tick) == BSP_LA_IC_OK)
            {
                la_service_cutoff_tick = cutoff_tick;
            }
        }
        else
        {
            la_service_pending_data[event->channel][half] = event->data;
            la_service_pending_count[event->channel][half] = event->count;
            la_service_shared.pending_mask |= pending_bit;
        }
    }
    else if (event->type == BSP_LA_IC_EVENT_DEADLINE)
    {
        if ((la_service_shared.state == LA_SERVICE_STATE_STARTING) ||
            (la_service_shared.state == LA_SERVICE_STATE_RUNNING))
        {
            la_service_stop_reason = LA_SERVICE_STOP_DEADLINE;
            la_service_shared.state = LA_SERVICE_STATE_STOPPING;
        }
    }
    else if (event->type == BSP_LA_IC_EVENT_DMA_ERROR)
    {
        if ((la_service_shared.state == LA_SERVICE_STATE_RUNNING) ||
            (la_service_shared.state == LA_SERVICE_STATE_STARTING))
        {
            la_service_stop_reason = LA_SERVICE_STOP_DMA_ERROR;
            la_service_shared.state = LA_SERVICE_STATE_STOPPING;
        }
    }
    else
    {
        return;
    }

    if (la_service_task_handle != NULL)
    {
        vTaskNotifyGiveFromISR(la_service_task_handle, &higher_priority_task_woken);
        portYIELD_FROM_ISR(higher_priority_task_woken);
    }
}

/* ================================================== */

/**
 * @brief  逻辑分析仪服务初始化
 * @note   必须在BSP逻辑分析仪输入捕获驱动初始化后调用
 */
uint8_t LA_Service_Init(void)
{
    if (la_service_task_handle != NULL)
    {
        return 0U;
    }
    if (la_service_shared.state != LA_SERVICE_STATE_RESET)
    {
        return 1U;
    }

    LA_Storage_Init();

    la_service_shared.storage_mutex = xSemaphoreCreateMutex();
    if (la_service_shared.storage_mutex == NULL)
    {
        return 1U;
    }

    la_service_shared.query_mutex = xSemaphoreCreateMutex();
    if (la_service_shared.query_mutex == NULL)
    {
        vSemaphoreDelete(la_service_shared.storage_mutex);
        la_service_shared.storage_mutex = NULL;
        return 1U;
    }

    if (xTaskCreate(LA_Service_Task,
                    "LAService",
                    LA_SERVICE_TASK_STACK_DEPTH,
                    NULL,
                    LA_SERVICE_TASK_PRIORITY,
                    &la_service_task_handle) != pdPASS)
    {
        vSemaphoreDelete(la_service_shared.query_mutex);
        la_service_shared.query_mutex = NULL;
        vSemaphoreDelete(la_service_shared.storage_mutex);
        la_service_shared.storage_mutex = NULL;
        return 1U;
    }

    BSP_LA_IC_Set_Event_Callback(LA_Service_BSP_Event_Callback, NULL);
    la_service_shared.state = LA_SERVICE_STATE_IDLE;

    return 0U;
}

/**
 * @brief  设置逻辑分析仪服务事件回调
 * @note   回调由逻辑分析仪服务任务调用，可以使用任务上下文接口
 */
void LA_Service_Set_Event_Callback(LA_Service_Event_Callback_t callback, void *context)
{
    taskENTER_CRITICAL();
    la_service_event_callback = callback;
    la_service_event_context = context;
    taskEXIT_CRITICAL();
}

/**
 * @brief  启动逻辑分析仪采集
 * @note   在服务初始化后的任务上下文调用，接受请求后清空存储并分配新采集编号
 */
LA_Service_Result_t LA_Service_Start(uint32_t timestamp_hz,
                                     uint32_t duration_us,
                                     uint8_t input_filter)
{
    BSP_LA_IC_Start_Result_t result;
    BSP_LA_IC_Result_t bsp_result;

    if ((timestamp_hz == 0U) || (duration_us == 0U) || (input_filter > 15U))
    {
        return LA_SERVICE_ERROR_INVALID_PARAMETER;
    }

    taskENTER_CRITICAL();
    if ((la_service_shared.state != LA_SERVICE_STATE_IDLE) ||
        (la_service_shared.capture_lease_token != 0U))
    {
        taskEXIT_CRITICAL();
        return LA_SERVICE_ERROR_BUSY;
    }
    la_service_shared.state = LA_SERVICE_STATE_STARTING;
    la_service_shared.capture_id++;
    if (la_service_shared.capture_id == 0U)
    {
        la_service_shared.capture_id = 1U;
    }
    la_service_stop_reason = LA_SERVICE_STOP_NONE;
    la_service_shared.pending_mask = 0U;
    la_service_contaminated_mask = 0U;
    la_service_start_ready = 0U;
    la_service_finalize_failed = 0U;
    taskEXIT_CRITICAL();

    (void)xSemaphoreTake(la_service_shared.storage_mutex, portMAX_DELAY);
    LA_Storage_Reset();
    for (uint8_t i = 0U; i < LA_SERVICE_CHANNEL_COUNT; i++)
    {
        la_service_shared.consumed_index[i] = 0U;
    }
    (void)xSemaphoreGive(la_service_shared.storage_mutex);
    la_service_timestamp_hz = 0U;
    la_service_shared.deadline_tick = 0U;
    la_service_cutoff_tick = 0U;
    la_service_overcapture_mask = 0U;
    memset(la_service_overcapture_count, 0, sizeof(la_service_overcapture_count));
    memset(la_service_duplicate_count, 0, sizeof(la_service_duplicate_count));
    memset(la_service_last_timestamp, 0, sizeof(la_service_last_timestamp));
    memset(la_service_last_timestamp_valid, 0, sizeof(la_service_last_timestamp_valid));
    la_service_shared.initial_levels = 0U;

    bsp_result = BSP_LA_IC_Start(timestamp_hz, duration_us, input_filter, &result);
    if (bsp_result != BSP_LA_IC_OK)
    {
        taskENTER_CRITICAL();
        la_service_shared.pending_mask = 0U;
        la_service_start_ready = 0U;
        if (bsp_result == BSP_LA_IC_ERROR_INVALID_PARAMETER)
        {
            /* BSP参数计算失败发生在启用硬件前，不需要错误恢复 */
            la_service_shared.state = LA_SERVICE_STATE_IDLE;
            taskEXIT_CRITICAL();
            return LA_SERVICE_ERROR_INVALID_PARAMETER;
        }
        /* 即使BSP启动回滚失败，也不再宣称硬件可直接接受下一次Start */
        la_service_stop_reason = LA_SERVICE_STOP_HARDWARE_ERROR;
        la_service_shared.state = LA_SERVICE_STATE_ERROR;
        taskEXIT_CRITICAL();
        if (bsp_result == BSP_LA_IC_ERROR_BUSY)
        {
            return LA_SERVICE_ERROR_BUSY;
        }
        return LA_SERVICE_ERROR_HARDWARE;
    }

    la_service_timestamp_hz = result.timestamp_hz;
    la_service_shared.deadline_tick = result.deadline_tick;
    la_service_shared.initial_levels = result.initial_levels;

    taskENTER_CRITICAL();
    la_service_start_ready = 1U;
    if (la_service_shared.state == LA_SERVICE_STATE_STARTING)
    {
        la_service_shared.state = LA_SERVICE_STATE_RUNNING;
    }
    taskEXIT_CRITICAL();

    if ((la_service_shared.pending_mask != 0U) ||
        (la_service_shared.state == LA_SERVICE_STATE_STOPPING))
    {
        xTaskNotifyGive(la_service_task_handle);
    }

    return LA_SERVICE_OK;
}

/**
 * @brief  手动停止逻辑分析仪采集
 * @note   仅快速停止硬件，数据收尾由逻辑分析仪服务任务完成
 */
LA_Service_Result_t LA_Service_Stop(void)
{
    uint32_t cutoff_tick;

    taskENTER_CRITICAL();
    if (la_service_shared.state != LA_SERVICE_STATE_RUNNING)
    {
        taskEXIT_CRITICAL();
        return LA_SERVICE_ERROR_STATE;
    }

    if (BSP_LA_IC_Stop(&cutoff_tick) != BSP_LA_IC_OK)
    {
        /* 状态失配也要进入可观察的收尾流程，不能永久停留RUNNING */
        la_service_stop_reason = LA_SERVICE_STOP_HARDWARE_ERROR;
        la_service_shared.state = LA_SERVICE_STATE_STOPPING;
        taskEXIT_CRITICAL();
        xTaskNotifyGive(la_service_task_handle);
        return LA_SERVICE_ERROR_HARDWARE;
    }

    la_service_cutoff_tick = cutoff_tick;
    la_service_stop_reason = LA_SERVICE_STOP_USER;
    la_service_shared.state = LA_SERVICE_STATE_STOPPING;
    taskEXIT_CRITICAL();

    xTaskNotifyGive(la_service_task_handle);
    return LA_SERVICE_OK;
}

/**
 * @brief  复位逻辑分析仪服务
 */
LA_Service_Result_t LA_Service_Reset(void)
{
    if (la_service_shared.storage_mutex == NULL)
    {
        return LA_SERVICE_ERROR_STATE;
    }

    taskENTER_CRITICAL();
    if ((la_service_shared.state == LA_SERVICE_STATE_STARTING) ||
        (la_service_shared.state == LA_SERVICE_STATE_RUNNING) ||
        (la_service_shared.state == LA_SERVICE_STATE_STOPPING) ||
        (la_service_shared.state == LA_SERVICE_STATE_IMPORTING))
    {
        taskEXIT_CRITICAL();
        return LA_SERVICE_ERROR_BUSY;
    }
    taskEXIT_CRITICAL();

    if (xSemaphoreTake(la_service_shared.query_mutex, 0U) != pdTRUE)
    {
        return LA_SERVICE_ERROR_BUSY;
    }

    (void)xSemaphoreTake(la_service_shared.storage_mutex, portMAX_DELAY);
    taskENTER_CRITICAL();
    if ((la_service_shared.state == LA_SERVICE_STATE_STARTING) ||
        (la_service_shared.state == LA_SERVICE_STATE_RUNNING) ||
        (la_service_shared.state == LA_SERVICE_STATE_STOPPING) ||
        (la_service_shared.state == LA_SERVICE_STATE_IMPORTING) ||
        (la_service_shared.capture_lease_token != 0U))
    {
        taskEXIT_CRITICAL();
        (void)xSemaphoreGive(la_service_shared.storage_mutex);
        (void)xSemaphoreGive(la_service_shared.query_mutex);
        return LA_SERVICE_ERROR_BUSY;
    }
    /* 阻止检查完成后另一个任务从IDLE启动，或取得终态数据保留权 */
    LA_Service_State_t saved_state = la_service_shared.state;
    la_service_shared.state = LA_SERVICE_STATE_STOPPING;
    taskEXIT_CRITICAL();
    if (BSP_LA_IC_Reset() != BSP_LA_IC_OK)
    {
        la_service_shared.state = saved_state;
        (void)xSemaphoreGive(la_service_shared.storage_mutex);
        (void)xSemaphoreGive(la_service_shared.query_mutex);
        return LA_SERVICE_ERROR_HARDWARE;
    }
    LA_Storage_Reset();
    for (uint8_t i = 0U; i < LA_SERVICE_CHANNEL_COUNT; i++)
    {
        la_service_shared.consumed_index[i] = 0U;
    }

    taskENTER_CRITICAL();
    la_service_shared.pending_mask = 0U;
    la_service_contaminated_mask = 0U;
    la_service_start_ready = 0U;
    la_service_stop_reason = LA_SERVICE_STOP_NONE;
    la_service_timestamp_hz = 0U;
    la_service_shared.deadline_tick = 0U;
    la_service_cutoff_tick = 0U;
    la_service_overcapture_mask = 0U;
    memset(la_service_overcapture_count, 0, sizeof(la_service_overcapture_count));
    memset(la_service_duplicate_count, 0, sizeof(la_service_duplicate_count));
    memset(la_service_last_timestamp, 0, sizeof(la_service_last_timestamp));
    memset(la_service_last_timestamp_valid, 0, sizeof(la_service_last_timestamp_valid));
    la_service_finalize_failed = 0U;
    la_service_shared.initial_levels = 0U;
    la_service_shared.state = LA_SERVICE_STATE_IDLE;
    taskEXIT_CRITICAL();
    (void)xSemaphoreGive(la_service_shared.storage_mutex);
    (void)xSemaphoreGive(la_service_shared.query_mutex);

    return LA_SERVICE_OK;
}

/**
 * @brief  获取逻辑分析仪服务信息
 * @note   可在任务上下文调用
 */
LA_Service_Result_t LA_Service_Get_Info(LA_Service_Info_t *info)
{
    if (info == NULL)
    {
        return LA_SERVICE_ERROR_INVALID_PARAMETER;
    }
    if (la_service_shared.storage_mutex == NULL)
    {
        return LA_SERVICE_ERROR_NOT_READY;
    }

    (void)xSemaphoreTake(la_service_shared.storage_mutex, portMAX_DELAY);
    LA_Service_Fill_Info(info);
    (void)xSemaphoreGive(la_service_shared.storage_mutex);

    return LA_SERVICE_OK;
}

/**
 * @brief  获取逻辑分析仪当前计数值
 * @note   只能在任务上下文调用，采集运行时返回当前TIM2 CNT，采集结束后返回截止计数值
 */
LA_Service_Result_t LA_Service_Get_Current_Tick(uint32_t *current_tick)
{
    LA_Service_State_t state;

    if (current_tick == NULL)
    {
        return LA_SERVICE_ERROR_INVALID_PARAMETER;
    }

    taskENTER_CRITICAL();
    state = la_service_shared.state;
    if ((state == LA_SERVICE_STATE_DONE) ||
        (state == LA_SERVICE_STATE_ERROR))
    {
        *current_tick = la_service_cutoff_tick;
        taskEXIT_CRITICAL();
        return LA_SERVICE_OK;
    }
    taskEXIT_CRITICAL();

    if (state != LA_SERVICE_STATE_RUNNING)
    {
        return LA_SERVICE_ERROR_NOT_READY;
    }
    if (BSP_LA_IC_Get_Current_Tick(current_tick) != BSP_LA_IC_OK)
    {
        return LA_SERVICE_ERROR_NOT_READY;
    }

    return LA_SERVICE_OK;
}

/* ================================================== */

/**
 * @brief  校验文件来源元数据及共享块池容量
 */
LA_Service_Result_t LA_Service_Import_Validate(
                                        const LA_Service_Info_t *metadata)
{
    uint32_t block_count = 0U;
    uint32_t channel_mask = (1UL << LA_SERVICE_CHANNEL_COUNT) - 1UL;

    if (metadata == NULL)
    {
        return LA_SERVICE_ERROR_INVALID_PARAMETER;
    }
    if (((metadata->state != LA_SERVICE_STATE_DONE) &&
         (metadata->state != LA_SERVICE_STATE_ERROR)) ||
        (metadata->capture_id == 0U) || ((uint32_t)metadata->stop_reason == LA_SERVICE_STOP_NONE) ||
        ((uint32_t)metadata->stop_reason > LA_SERVICE_STOP_TRANSFER_TIMEOUT) ||
        ((metadata->initial_levels & ~channel_mask) != 0U) ||
        ((metadata->overcapture_mask & ~channel_mask) != 0U) || (metadata->finalize_failed > 1U))
    {
        return LA_SERVICE_ERROR_INVALID_PARAMETER;
    }
    if ((metadata->state == LA_SERVICE_STATE_DONE) &&
        (((metadata->stop_reason != LA_SERVICE_STOP_DEADLINE) &&
          (metadata->stop_reason != LA_SERVICE_STOP_USER)) ||
         (metadata->finalize_failed != 0U)))
    {
        return LA_SERVICE_ERROR_INVALID_PARAMETER;
    }
    if (metadata->timestamp_hz == 0U)
    {
        /* 同步启动硬件失败也可存档，但没有有效的时间轴或已提交边沿 */
        if ((metadata->state != LA_SERVICE_STATE_ERROR) ||
            (metadata->stop_reason != LA_SERVICE_STOP_HARDWARE_ERROR) ||
            (metadata->deadline_tick != 0U) || (metadata->cutoff_tick != 0U))
        {
            return LA_SERVICE_ERROR_INVALID_PARAMETER;
        }
    }
    else if (metadata->deadline_tick == 0U)
    {
        return LA_SERVICE_ERROR_INVALID_PARAMETER;
    }

    for (uint8_t channel = 0U; channel < LA_SERVICE_CHANNEL_COUNT; channel++)
    {
        uint32_t count = metadata->data_count[channel];

        if ((metadata->timestamp_hz == 0U) && (count != 0U))
        {
            return LA_SERVICE_ERROR_INVALID_PARAMETER;
        }
        block_count += count / LA_STORAGE_BLOCK_CAPACITY;
        if ((count % LA_STORAGE_BLOCK_CAPACITY) != 0U)
        {
            block_count++;
        }
        if (block_count > LA_STORAGE_BLOCK_COUNT)
        {
            return LA_SERVICE_ERROR_STORAGE;
        }
    }
    return LA_SERVICE_OK;
}

/**
 * @brief  校验当前任务拥有的导入凭据，调用者持有存储锁
 */
static uint8_t LA_Service_Import_Is_Valid(const LA_Service_Import_t *import)
{
    return ((import != NULL) &&
            (import->token != 0U) &&
            (import->token == la_service_import.token) &&
            (la_service_import.owner == xTaskGetCurrentTaskHandle()) &&
            (la_service_shared.state == LA_SERVICE_STATE_IMPORTING)) ? 1U : 0U;
}

/**
 * @brief  开始独占导入，确认BSP采集硬件已停稳后再允许CPU写入SDRAM
 */
LA_Service_Result_t LA_Service_Import_Begin(
                                        LA_Service_Import_t *import,
                                        const LA_Service_Info_t *metadata)
{
    LA_Service_Result_t result;

    if ((import == NULL) || (metadata == NULL))
    {
        return LA_SERVICE_ERROR_INVALID_PARAMETER;
    }
    result = LA_Service_Import_Validate(metadata);
    if (result != LA_SERVICE_OK)
    {
        return result;
    }
    if ((la_service_shared.storage_mutex == NULL) || (la_service_shared.query_mutex == NULL))
    {
        return LA_SERVICE_ERROR_NOT_READY;
    }
    taskENTER_CRITICAL();
    if ((la_service_shared.state != LA_SERVICE_STATE_IDLE) ||
        (la_service_shared.capture_lease_token != 0U))
    {
        taskEXIT_CRITICAL();
        return LA_SERVICE_ERROR_BUSY;
    }
    taskEXIT_CRITICAL();

    if (xSemaphoreTake(la_service_shared.query_mutex, 0U) != pdTRUE)
    {
        return LA_SERVICE_ERROR_BUSY;
    }
    if (xSemaphoreTake(la_service_shared.storage_mutex, 0U) != pdTRUE)
    {
        (void)xSemaphoreGive(la_service_shared.query_mutex);
        return LA_SERVICE_ERROR_BUSY;
    }
    taskENTER_CRITICAL();
    if ((la_service_shared.state != LA_SERVICE_STATE_IDLE) ||
        (la_service_shared.capture_lease_token != 0U))
    {
        taskEXIT_CRITICAL();
        (void)xSemaphoreGive(la_service_shared.storage_mutex);
        (void)xSemaphoreGive(la_service_shared.query_mutex);
        return LA_SERVICE_ERROR_BUSY;
    }
    /* Start先检查状态再取得存储锁，此屏障也保护硬件停稳检查期间 */
    la_service_shared.state = LA_SERVICE_STATE_IMPORTING;
    taskEXIT_CRITICAL();

    if (BSP_LA_IC_Reset() != BSP_LA_IC_OK)
    {
        taskENTER_CRITICAL();
        la_service_shared.state = LA_SERVICE_STATE_IDLE;
        taskEXIT_CRITICAL();
        (void)xSemaphoreGive(la_service_shared.storage_mutex);
        (void)xSemaphoreGive(la_service_shared.query_mutex);
        return LA_SERVICE_ERROR_HARDWARE;
    }
    LA_Storage_Reset();
    memset(la_service_import.last_tick, 0, sizeof(la_service_import.last_tick));
    la_service_import.metadata = *metadata;
    la_service_import.failed = 0U;
    la_service_import.owner = xTaskGetCurrentTaskHandle();
    la_service_next_import_token++;
    if (la_service_next_import_token == 0U)
    {
        la_service_next_import_token = 1U;
    }
    la_service_import.token = la_service_next_import_token;
    import->token = la_service_next_import_token;
    (void)xSemaphoreGive(la_service_shared.storage_mutex);
    (void)xSemaphoreGive(la_service_shared.query_mutex);
    return LA_SERVICE_OK;
}

/**
 * @brief  CPU按存储块边界追加绝对时间戳，不修改输入缓冲
 */
LA_Service_Result_t LA_Service_Import_Append(
                                        const LA_Service_Import_t *import,
                                        LA_Service_Channel_t channel,
                                        const uint32_t *data,
                                        uint16_t count)
{
    uint32_t stored_count;
    uint16_t copied = 0U;
    LA_Service_Result_t result = LA_SERVICE_OK;

    if ((import == NULL) ||
        ((uint32_t)channel >= LA_SERVICE_CHANNEL_COUNT) ||
        ((data == NULL) && (count != 0U)))
    {
        return LA_SERVICE_ERROR_INVALID_PARAMETER;
    }
    if (la_service_shared.storage_mutex == NULL)
    {
        return LA_SERVICE_ERROR_NOT_READY;
    }
    if (xSemaphoreTake(la_service_shared.storage_mutex, portMAX_DELAY) != pdTRUE)
    {
        return LA_SERVICE_ERROR_BUSY;
    }
    if (LA_Service_Import_Is_Valid(import) == 0U)
    {
        result = LA_SERVICE_ERROR_INVALID_PARAMETER;
    }
    else if (la_service_import.failed != 0U)
    {
        result = LA_SERVICE_ERROR_STORAGE;
    }
    else
    {
        stored_count = LA_Storage_Get_Count((uint8_t)channel);
        if ((stored_count > la_service_import.metadata.data_count[channel]) ||
            ((uint32_t)count >
             (la_service_import.metadata.data_count[channel] - stored_count)))
        {
            result = LA_SERVICE_ERROR_INVALID_PARAMETER;
        }
        for (uint16_t i = 0U; (i < count) && (result == LA_SERVICE_OK); i++)
        {
            uint32_t previous = (i == 0U) ?
                                la_service_import.last_tick[channel] : data[i - 1U];

            if (((stored_count != 0U) || (i != 0U)) && (data[i] < previous))
            {
                result = LA_SERVICE_ERROR_INVALID_PARAMETER;
            }
        }
        while ((copied < count) && (result == LA_SERVICE_OK))
        {
            uint32_t *destination;
            uint16_t block_remaining = (uint16_t)(LA_STORAGE_BLOCK_CAPACITY -
                                      (stored_count % LA_STORAGE_BLOCK_CAPACITY));
            uint16_t append_count = (uint16_t)(count - copied);

            if (append_count > block_remaining)
            {
                append_count = block_remaining;
            }
            if (LA_Storage_Reserve((uint8_t)channel, append_count, &destination) !=
                LA_STORAGE_OK)
            {
                result = LA_SERVICE_ERROR_STORAGE;
                break;
            }
            memcpy(destination, data + copied, (size_t)append_count * sizeof(*data));
            if (LA_Storage_Commit((uint8_t)channel, append_count) != LA_STORAGE_OK)
            {
                result = LA_SERVICE_ERROR_STORAGE;
                break;
            }
            copied = (uint16_t)(copied + append_count);
            stored_count += append_count;
            la_service_import.last_tick[channel] = data[copied - 1U];
        }
        if (result == LA_SERVICE_ERROR_STORAGE)
        {
            la_service_import.failed = 1U;
        }
    }
    (void)xSemaphoreGive(la_service_shared.storage_mutex);
    return result;
}

/**
 * @brief  发布完整导入及新的本机采集代数，不产生采集完成事件
 */
LA_Service_Result_t LA_Service_Import_Commit(LA_Service_Import_t *import)
{
    LA_Service_Result_t result = LA_SERVICE_OK;

    if (import == NULL)
    {
        return LA_SERVICE_ERROR_INVALID_PARAMETER;
    }
    if (la_service_shared.storage_mutex == NULL)
    {
        return LA_SERVICE_ERROR_NOT_READY;
    }
    if (xSemaphoreTake(la_service_shared.storage_mutex, portMAX_DELAY) != pdTRUE)
    {
        return LA_SERVICE_ERROR_BUSY;
    }
    if (LA_Service_Import_Is_Valid(import) == 0U)
    {
        result = LA_SERVICE_ERROR_INVALID_PARAMETER;
    }
    else if (la_service_import.failed != 0U)
    {
        result = LA_SERVICE_ERROR_STORAGE;
    }
    else
    {
        for (uint8_t channel = 0U; channel < LA_SERVICE_CHANNEL_COUNT; channel++)
        {
            if (LA_Storage_Get_Count(channel) != la_service_import.metadata.data_count[channel])
            {
                result = LA_SERVICE_ERROR_NOT_READY;
                break;
            }
        }
    }
    if (result == LA_SERVICE_OK)
    {
        taskENTER_CRITICAL();
        la_service_shared.capture_id++;
        if (la_service_shared.capture_id == 0U)
        {
            la_service_shared.capture_id = 1U;
        }
        la_service_stop_reason = la_service_import.metadata.stop_reason;
        la_service_timestamp_hz = la_service_import.metadata.timestamp_hz;
        la_service_shared.deadline_tick = la_service_import.metadata.deadline_tick;
        la_service_cutoff_tick = la_service_import.metadata.cutoff_tick;
        la_service_overcapture_mask = la_service_import.metadata.overcapture_mask;
        memset(la_service_overcapture_count, 0, sizeof(la_service_overcapture_count));
        memset(la_service_duplicate_count, 0, sizeof(la_service_duplicate_count));
        memset(la_service_last_timestamp, 0, sizeof(la_service_last_timestamp));
        memset(la_service_last_timestamp_valid, 0, sizeof(la_service_last_timestamp_valid));
        la_service_shared.initial_levels = la_service_import.metadata.initial_levels;
        la_service_finalize_failed = la_service_import.metadata.finalize_failed;
        la_service_shared.pending_mask = 0U;
        la_service_contaminated_mask = 0U;
        la_service_start_ready = 0U;
        memset(la_service_shared.consumed_index, 0, sizeof(la_service_shared.consumed_index));
        la_service_import.token = 0U;
        la_service_import.owner = NULL;
        import->token = 0U;
        la_service_shared.state = la_service_import.metadata.state;
        taskEXIT_CRITICAL();
    }
    (void)xSemaphoreGive(la_service_shared.storage_mutex);
    return result;
}

/**
 * @brief  丢弃未发布的部分导入并恢复空闲，不恢复旧采集
 */
LA_Service_Result_t LA_Service_Import_Abort(LA_Service_Import_t *import)
{
    if (import == NULL)
    {
        return LA_SERVICE_ERROR_INVALID_PARAMETER;
    }
    if (la_service_shared.storage_mutex == NULL)
    {
        return LA_SERVICE_ERROR_NOT_READY;
    }
    if (xSemaphoreTake(la_service_shared.storage_mutex, portMAX_DELAY) != pdTRUE)
    {
        return LA_SERVICE_ERROR_BUSY;
    }
    if (LA_Service_Import_Is_Valid(import) == 0U)
    {
        (void)xSemaphoreGive(la_service_shared.storage_mutex);
        return LA_SERVICE_ERROR_INVALID_PARAMETER;
    }
    LA_Storage_Reset();
    taskENTER_CRITICAL();
    la_service_shared.pending_mask = 0U;
    la_service_contaminated_mask = 0U;
    la_service_start_ready = 0U;
    la_service_stop_reason = LA_SERVICE_STOP_NONE;
    la_service_timestamp_hz = 0U;
    la_service_shared.deadline_tick = 0U;
    la_service_cutoff_tick = 0U;
    la_service_overcapture_mask = 0U;
    memset(la_service_overcapture_count, 0, sizeof(la_service_overcapture_count));
    memset(la_service_duplicate_count, 0, sizeof(la_service_duplicate_count));
    memset(la_service_last_timestamp, 0, sizeof(la_service_last_timestamp));
    memset(la_service_last_timestamp_valid, 0, sizeof(la_service_last_timestamp_valid));
    la_service_finalize_failed = 0U;
    la_service_shared.initial_levels = 0U;
    memset(la_service_shared.consumed_index, 0, sizeof(la_service_shared.consumed_index));
    la_service_import.token = 0U;
    la_service_import.owner = NULL;
    la_service_import.failed = 0U;
    import->token = 0U;
    la_service_shared.state = LA_SERVICE_STATE_IDLE;
    taskEXIT_CRITICAL();
    (void)xSemaphoreGive(la_service_shared.storage_mutex);
    return LA_SERVICE_OK;
}
