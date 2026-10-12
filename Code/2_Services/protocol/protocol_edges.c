/**
 * @file   protocol_edges.c
 * @brief  协议解析使用的LA边沿游标实现
 */

/* ================================================== */

#include "protocol/protocol_internal.h"
#include "stddef.h"

/* ================================================== */

/**
 * @brief  将LA查询结果转换为协议模块结果
 */
static Protocol_Result_t Protocol_Convert_LA_Result(
                                            LA_Service_Result_t result)
{
    return (result == LA_SERVICE_OK) ? PROTOCOL_OK :
                                      PROTOCOL_ERROR_LA_SERVICE;
}

/* ================================================== */

/**
 * @brief  准备按时间读取指定通道的边沿，包含起止时间
 * @note   视图由服务任务持有，游标自身不获取或释放查询锁
 */
Protocol_Result_t Protocol_Edge_Cursor_Open(
                                    Protocol_Edge_Cursor_t *cursor,
                                    const LA_Service_Query_View_t *view,
                                    LA_Service_Channel_t channel,
                                    uint32_t start_tick,
                                    uint32_t end_tick)
{
    LA_Service_Range_Result_t range;
    LA_Service_Result_t result;

    if ((cursor == NULL) || (view == NULL))
    {
        return PROTOCOL_ERROR_INVALID_PARAMETER;
    }

    result = LA_Service_Query_View_Range(view,
                                         channel,
                                         start_tick,
                                         end_tick,
                                         &range);
    if (result != LA_SERVICE_OK)
    {
        return Protocol_Convert_LA_Result(result);
    }

    cursor->view = view;
    cursor->channel = channel;
    cursor->current_index = range.first_index;
    cursor->after_index = range.after_index;
    cursor->data_count = 0U;
    cursor->data_position = 0U;
    cursor->level = range.start_level;
    return PROTOCOL_OK;
}

/**
 * @brief  从固定查询视图分批补充边沿缓冲
 */
static Protocol_Result_t Protocol_Edge_Cursor_Fill(
                                    Protocol_Edge_Cursor_t *cursor)
{
    LA_Service_Result_t result;

    if (cursor->current_index >= cursor->after_index)
    {
        cursor->data_count = 0U;
        cursor->data_position = 0U;
        return PROTOCOL_OK;
    }

    uint32_t remaining = cursor->after_index - cursor->current_index;
    uint16_t request_count = (remaining > PROTOCOL_EDGE_BATCH_COUNT) ?
                             PROTOCOL_EDGE_BATCH_COUNT :
                             (uint16_t)remaining;

    result = LA_Service_Query_View_Get_Data(cursor->view,
                                             cursor->channel,
                                             cursor->current_index,
                                             cursor->data,
                                             request_count,
                                             &cursor->data_count);
    cursor->data_position = 0U;
    return Protocol_Convert_LA_Result(result);
}

/**
 * @brief  查看下一条边沿且不改变位置或电平
 */
Protocol_Result_t Protocol_Edge_Cursor_Peek(
                                    Protocol_Edge_Cursor_t *cursor,
                                    uint32_t *timestamp,
                                    uint8_t *has_edge)
{
    Protocol_Result_t result;

    if ((cursor == NULL) || (timestamp == NULL) || (has_edge == NULL))
    {
        return PROTOCOL_ERROR_INVALID_PARAMETER;
    }

    if (cursor->data_position >= cursor->data_count)
    {
        result = Protocol_Edge_Cursor_Fill(cursor);
        if (result != PROTOCOL_OK)
        {
            return result;
        }
    }

    if (cursor->data_count == 0U)
    {
        *has_edge = 0U;
        return PROTOCOL_OK;
    }

    *timestamp = cursor->data[cursor->data_position];
    *has_edge = 1U;
    return PROTOCOL_OK;
}

/**
 * @brief  消费下一条边沿并更新通道电平
 */
Protocol_Result_t Protocol_Edge_Cursor_Pop(
                                    Protocol_Edge_Cursor_t *cursor,
                                    uint32_t *timestamp,
                                    uint8_t *level_after,
                                    uint8_t *has_edge)
{
    Protocol_Result_t result = Protocol_Edge_Cursor_Peek(cursor,
                                                          timestamp,
                                                          has_edge);

    if ((result != PROTOCOL_OK) || (*has_edge == 0U))
    {
        return result;
    }

    cursor->data_position++;
    cursor->current_index++;
    cursor->level ^= 1U;
    if (level_after != NULL)
    {
        *level_after = cursor->level;
    }
    return PROTOCOL_OK;
}

/**
 * @brief  消费严格早于指定计数值的所有边沿
 */
Protocol_Result_t Protocol_Edge_Cursor_Advance_Before(
                                    Protocol_Edge_Cursor_t *cursor,
                                    uint32_t timestamp)
{
    Protocol_Result_t result;
    uint32_t edge_tick;
    uint8_t has_edge;
    uint8_t crossed_cache = 0U;

    if (cursor == NULL)
    {
        return PROTOCOL_ERROR_INVALID_PARAMETER;
    }
    result = Protocol_Edge_Cursor_Peek(cursor, &edge_tick, &has_edge);
    if ((result != PROTOCOL_OK) || (has_edge == 0U) ||
        (edge_tick >= timestamp))
    {
        return result;
    }

    for (;;)
    {
        uint16_t next_position = cursor->data_position;
        uint16_t skipped;
        LA_Service_Result_t la_result;
        uint32_t next_index;

        while ((next_position < cursor->data_count) &&
               (cursor->data[next_position] < timestamp))
        {
            next_position++;
        }
        skipped = (uint16_t)(next_position - cursor->data_position);
        cursor->data_position = next_position;
        cursor->current_index += skipped;
        cursor->level ^= (uint8_t)(skipped & 1U);
        if ((next_position < cursor->data_count) ||
            (cursor->current_index >= cursor->after_index))
        {
            return PROTOCOL_OK;
        }
        if (crossed_cache == 0U)
        {
            /* 跨缓存的小步先补一批，避免每64条的普通顺序推进都做二分 */
            result = Protocol_Edge_Cursor_Fill(cursor);
            if ((result != PROTOCOL_OK) || (cursor->data_count == 0U))
            {
                return result;
            }
            crossed_cache = 1U;
            continue;
        }

        /* 已跳过至少一条小于目标的边沿，timestamp必定非零 */
        la_result = LA_Service_Query_View_Upper_Bound(cursor->view,
                                                       cursor->channel,
                                                       timestamp - 1U,
                                                       cursor->current_index,
                                                       cursor->after_index,
                                                       &next_index);
        if (la_result != LA_SERVICE_OK)
        {
            return Protocol_Convert_LA_Result(la_result);
        }
        cursor->level ^= (uint8_t)((next_index - cursor->current_index) & 1U);
        cursor->current_index = next_index;
        cursor->data_count = 0U;
        cursor->data_position = 0U;
        return PROTOCOL_OK;
    }
}
