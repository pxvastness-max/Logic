/**
 * @file   wave_capture_data.c
 * @brief  采集数据查询适配
 */

/* ================================================== */

#include "waveform/wave_capture_data.h"
#include "la/la_service.h"
#include <string.h>

/* ========================= 配置 ========================= */

#define WAVE_CAPTURE_EDGE_BATCH_COUNT 128U

/* ================================================== */

static uint32_t wave_capture_data_last_tick;
static uint32_t wave_capture_data_edge_batch[WAVE_CAPTURE_EDGE_BATCH_COUNT];

/* ================================================== */

void Wave_Capture_Data_Init(void)
{
    wave_capture_data_last_tick = 0U;
}

bool Wave_Capture_Data_Begin(uint32_t timestamp_hz, uint32_t duration_us, uint8_t input_filter)
{
    LA_Service_Info_t info;

    if (timestamp_hz == 0U || duration_us == 0U || input_filter > 15U ||
        LA_Service_Get_Info(&info) != LA_SERVICE_OK)
    {
        return false;
    }
    if (info.state != LA_SERVICE_STATE_IDLE)
    {
        if (LA_Service_Reset() != LA_SERVICE_OK)
        {
            return false;
        }
    }
    if (LA_Service_Start(timestamp_hz, duration_us, input_filter) != LA_SERVICE_OK)
    {
        return false;
    }
    wave_capture_data_last_tick = 0U;
    return true;
}

bool Wave_Capture_Data_Can_Configure(void)
{
    LA_Service_Info_t info;

    if (LA_Service_Get_Info(&info) != LA_SERVICE_OK)
    {
        return false;
    }
    return info.state == LA_SERVICE_STATE_IDLE || info.state == LA_SERVICE_STATE_DONE ||
           info.state == LA_SERVICE_STATE_ERROR;
}

bool Wave_Capture_Data_Stop(void)
{
    LA_Service_Info_t info;

    if (LA_Service_Get_Info(&info) != LA_SERVICE_OK)
    {
        return false;
    }
    if (info.state == LA_SERVICE_STATE_RUNNING || info.state == LA_SERVICE_STATE_STARTING)
    {
        return LA_Service_Stop() == LA_SERVICE_OK;
    }
    return info.state == LA_SERVICE_STATE_STOPPING || info.state == LA_SERVICE_STATE_DONE ||
           info.state == LA_SERVICE_STATE_ERROR;
}

bool Wave_Capture_Data_Get_Progress(uint32_t *elapsed_cnt, bool *finished)
{
    uint32_t tick;
    LA_Service_Info_t info;

    if (elapsed_cnt == NULL || finished == NULL || LA_Service_Get_Info(&info) != LA_SERVICE_OK)
    {
        return false;
    }

    *finished = info.state == LA_SERVICE_STATE_DONE || info.state == LA_SERVICE_STATE_ERROR;
    if (*finished || info.state == LA_SERVICE_STATE_STOPPING)
    {
        wave_capture_data_last_tick = info.cutoff_tick;
    }
    else if (info.state != LA_SERVICE_STATE_RUNNING && info.state != LA_SERVICE_STATE_STARTING)
    {
        return false;
    }
    else if (LA_Service_Get_Current_Tick(&tick) == LA_SERVICE_OK)
    {
        wave_capture_data_last_tick = tick;
    }
    *elapsed_cnt = wave_capture_data_last_tick;
    return true;
}

uint32_t Wave_Capture_Data_Get_Now_Cnt(void)
{
    uint32_t elapsed_cnt;
    bool finished;

    (void)Wave_Capture_Data_Get_Progress(&elapsed_cnt, &finished);
    return wave_capture_data_last_tick;
}

uint32_t Wave_Capture_Data_Get_Cnt_At_Tick(uint32_t rtos_tick)
{
    (void)rtos_tick;
    return Wave_Capture_Data_Get_Now_Cnt();
}

void Wave_Capture_Data_Use_Loaded(uint32_t timestamp_hz, uint32_t end_cnt)
{
    (void)timestamp_hz;
    wave_capture_data_last_tick = end_cnt;
}

bool Wave_Capture_Data_Request_Window(uint32_t s_tick,
                                      uint32_t e_tick,
                                      uint32_t window_tick,
                                      uint16_t *columns,
                                      uint32_t *valid_columns,
                                      uint8_t *initial_levels)
{
    LA_Service_Query_View_t view = {0};
    LA_Service_Result_t result = LA_SERVICE_OK;
    uint32_t channel_index;
    bool success = false;

    if (columns == NULL || valid_columns == NULL || initial_levels == NULL || window_tick == 0U ||
        e_tick < s_tick)
    {
        return false;
    }

    memset(columns, 0, sizeof(columns[0]) * WAVE_CAPTURE_DATA_COLUMN_COUNT);
    *initial_levels = 0U;
    *valid_columns =
        (e_tick == s_tick) ?
            0U :
            (((e_tick - s_tick) >= window_tick) ?
                 WAVE_CAPTURE_DATA_COLUMN_COUNT :
                 (uint32_t)((((uint64_t)(e_tick - s_tick) * WAVE_CAPTURE_DATA_COLUMN_COUNT) +
                             window_tick - 1U) /
                            window_tick));

    /* 尚无有效采集区间时，零列本身就是一个完整且可显示的查询结果 */
    if (*valid_columns == 0U)
    {
        return true;
    }

    if (LA_Service_Query_Try_Begin(&view) != LA_SERVICE_OK)
    {
        return false;
    }

    for (channel_index = 0U; channel_index < WAVE_CAPTURE_DATA_CHANNEL_COUNT; channel_index++)
    {
        LA_Service_Range_Result_t range;
        uint32_t next_index;
        uint32_t remaining;
        uint16_t batch_count = 0U;
        uint16_t batch_offset = 0U;
        uint32_t column_index;
        const uint32_t bit_shift = channel_index * 2U;
        bool level;

        result = LA_Service_Query_View_Range(&view,
                                             (LA_Service_Channel_t)channel_index,
                                             s_tick,
                                             e_tick,
                                             &range);
        if (result != LA_SERVICE_OK)
        {
            goto query_end;
        }

        level = range.start_level != 0U;
        if (level)
        {
            *initial_levels |= (uint8_t)(1U << channel_index);
        }
        next_index = range.first_index;
        remaining = range.after_index - range.first_index;

        for (column_index = 0U; column_index < *valid_columns; column_index++)
        {
            /* 64 位加法避免接近 UINT32_MAX 的窗口回绕 */
            const uint64_t column_end_wide =
                (uint64_t)s_tick +
                (((uint64_t)(column_index + 1U) * window_tick) / WAVE_CAPTURE_DATA_COLUMN_COUNT);
            uint32_t column_end = column_end_wide > e_tick ? e_tick : (uint32_t)column_end_wide;
            bool has_transition = false;

            /* e_tick为有效数据右边界，不能把未来列或右边界边沿提前画入 */
            if (column_end > e_tick)
            {
                column_end = e_tick;
            }

            for (;;)
            {
                uint32_t edge_tick;

                if (batch_offset >= batch_count)
                {
                    uint16_t request_count;

                    if (remaining == 0U)
                    {
                        break;
                    }
                    request_count = (remaining > WAVE_CAPTURE_EDGE_BATCH_COUNT) ?
                                        WAVE_CAPTURE_EDGE_BATCH_COUNT :
                                        (uint16_t)remaining;
                    result = LA_Service_Query_View_Get_Data(&view,
                                                            (LA_Service_Channel_t)channel_index,
                                                            next_index,
                                                            wave_capture_data_edge_batch,
                                                            request_count,
                                                            &batch_count);
                    if (result != LA_SERVICE_OK || batch_count == 0U || batch_count > request_count)
                    {
                        goto query_end;
                    }
                    next_index += batch_count;
                    remaining -= batch_count;
                    batch_offset = 0U;
                }
                edge_tick = wave_capture_data_edge_batch[batch_offset];
                if (edge_tick >= column_end)
                {
                    break;
                }
                batch_offset++;
                level = !level;
                has_transition = true;
            }
            if (has_transition)
            {
                columns[column_index] |= (uint16_t)((level ? 0x02U : 0x01U) << bit_shift);
            }
        }
    }
    success = true;

query_end:
    if (LA_Service_Query_End(&view) != LA_SERVICE_OK)
    {
        success = false;
    }
    return success;
}
