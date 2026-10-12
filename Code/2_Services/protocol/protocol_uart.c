/**
 * @file   protocol_uart.c
 * @brief  UART边沿时间戳解析器
 */

/* ================================================== */

#include "protocol/protocol_internal.h"
#include "stddef.h"

/* ================================================== */

#define PROTOCOL_UART_MAX_PHASE 26U /* 9数据位、校验位和2停止位的帧结束相位 */

/* ================================================== */

typedef struct
{
    Protocol_Edge_Cursor_t edge; /* 当前方向的边沿游标 */
    uint8_t direction;           /* TX或RX记录标志 */
    uint8_t finished;            /* 未启用、边沿耗尽或已输出末尾不完整帧 */
} Protocol_UART_Line_t;

typedef struct
{
    Protocol_UART_Config_t config;                       /* UART波特率、位数、校验和极性配置 */
    Protocol_UART_Line_t tx;                             /* TX解析状态 */
    Protocol_UART_Line_t rx;                             /* RX解析状态 */
    Protocol_Emit_t emit;                                /* 记录输出回调 */
    void *emit_context;                                  /* 记录回调上下文 */
    uint32_t phase_offset[PROTOCOL_UART_MAX_PHASE + 1U]; /* 预计算的半位相位偏移 */
    uint32_t end_tick;                                   /* 解析截止计数值 */
} Protocol_UART_Context_t;

/* ================================================== */

/* 单会话私有状态，仅由解析服务任务串行访问，Open负责重新初始化 */
static Protocol_UART_Context_t protocol_uart;

/* ================================================== */

/**
 * @brief  计算UART帧内采样计数值
 */
static uint32_t Protocol_UART_Sample_Tick(uint32_t start_tick,
                                          uint32_t phase)
{
    uint32_t offset = protocol_uart.phase_offset[phase];

    if (offset > (UINT32_MAX - start_tick))
    {
        return UINT32_MAX;
    }
    return start_tick + offset;
}

/**
 * @brief  初始化UART解析配置和可选TX、RX边沿游标
 * @note   时间戳频率须支持至少每位三个计数值，由服务任务串行调用
 */
static Protocol_Result_t Protocol_UART_Open(
                                    const Protocol_Config_t *config,
                                    const Protocol_Info_t *info,
                                    const LA_Service_Query_View_t *view,
                                    Protocol_Emit_t emit,
                                    void *emit_context)
{
    Protocol_Result_t result = PROTOCOL_OK;

    if ((config == NULL) || (info == NULL) || (view == NULL) ||
        (emit == NULL))
    {
        return PROTOCOL_ERROR_INVALID_PARAMETER;
    }
    if (((uint32_t)config->decoder.uart.tx_channel >
         PROTOCOL_CHANNEL_UNUSED) ||
        ((uint32_t)config->decoder.uart.rx_channel >
         PROTOCOL_CHANNEL_UNUSED) ||
        ((config->decoder.uart.tx_channel == PROTOCOL_CHANNEL_UNUSED) &&
         (config->decoder.uart.rx_channel == PROTOCOL_CHANNEL_UNUSED)) ||
        ((config->decoder.uart.tx_channel != PROTOCOL_CHANNEL_UNUSED) &&
         (config->decoder.uart.tx_channel == config->decoder.uart.rx_channel)) ||
        (config->decoder.uart.baud_rate == 0U) ||
        (config->decoder.uart.data_bits < 5U) ||
        (config->decoder.uart.data_bits > 9U) ||
        (config->decoder.uart.stop_bits < 1U) ||
        (config->decoder.uart.stop_bits > 2U) ||
        ((uint32_t)config->decoder.uart.parity >
         (uint32_t)PROTOCOL_UART_PARITY_ODD) ||
        (config->decoder.uart.inverted > 1U) ||
        ((uint64_t)config->decoder.uart.baud_rate * 3ULL >
         info->timestamp_hz))
    {
        return PROTOCOL_ERROR_INVALID_PARAMETER;
    }

    protocol_uart.config = config->decoder.uart;
    protocol_uart.emit = emit;
    protocol_uart.emit_context = emit_context;
    for (uint32_t phase = 0U; phase <= PROTOCOL_UART_MAX_PHASE; phase++)
    {
        uint64_t numerator = ((uint64_t)phase * info->timestamp_hz) +
                             protocol_uart.config.baud_rate;
        uint64_t offset = numerator /
                         ((uint64_t)protocol_uart.config.baud_rate * 2ULL);

        protocol_uart.phase_offset[phase] = (offset > UINT32_MAX) ?
                                            UINT32_MAX : (uint32_t)offset;
    }
    protocol_uart.end_tick = info->end_tick;
    protocol_uart.tx.direction = PROTOCOL_UART_FLAG_TX;
    protocol_uart.rx.direction = PROTOCOL_UART_FLAG_RX;
    protocol_uart.tx.finished = (protocol_uart.config.tx_channel ==
                                 PROTOCOL_CHANNEL_UNUSED) ? 1U : 0U;
    protocol_uart.rx.finished = (protocol_uart.config.rx_channel ==
                                 PROTOCOL_CHANNEL_UNUSED) ? 1U : 0U;
    if (protocol_uart.tx.finished == 0U)
    {
        result = Protocol_Edge_Cursor_Open(&protocol_uart.tx.edge,
                                            view,
                                            protocol_uart.config.tx_channel,
                                            info->start_tick,
                                            info->end_tick);
    }
    if ((result == PROTOCOL_OK) && (protocol_uart.rx.finished == 0U))
    {
        result = Protocol_Edge_Cursor_Open(&protocol_uart.rx.edge,
                                            view,
                                            protocol_uart.config.rx_channel,
                                            info->start_tick,
                                            info->end_tick);
    }
    return result;
}

/**
 * @brief  按起始位边沿采样并输出一帧UART记录
 */
static Protocol_Result_t Protocol_UART_Emit_Frame(Protocol_UART_Line_t *line,
                                                  uint32_t start_tick)
{
    Protocol_Record_t record;
    Protocol_Result_t result;
    uint16_t value = 0U;
    uint8_t flags = 0U;
    uint8_t parity = 0U;
    uint8_t phase = 1U;
    uint32_t sample_tick;

    sample_tick = Protocol_UART_Sample_Tick(start_tick, phase);
    if (sample_tick > protocol_uart.end_tick)
    {
        flags |= PROTOCOL_UART_FLAG_INCOMPLETE;
    }
    else
    {
        result = Protocol_Edge_Cursor_Advance_Before(&line->edge,
                                                      sample_tick);
        if (result != PROTOCOL_OK)
        {
            return result;
        }
        if ((uint8_t)(line->edge.level ^
                      protocol_uart.config.inverted) != 0U)
        {
            flags |= PROTOCOL_UART_FLAG_START_ERROR;
        }
    }

    for (uint8_t bit = 0U;
         (bit < protocol_uart.config.data_bits) &&
         ((flags & PROTOCOL_UART_FLAG_INCOMPLETE) == 0U);
         bit++)
    {
        uint8_t logical_level;

        phase = (uint8_t)(3U + (bit * 2U));
        sample_tick = Protocol_UART_Sample_Tick(start_tick, phase);
        if (sample_tick > protocol_uart.end_tick)
        {
            flags |= PROTOCOL_UART_FLAG_INCOMPLETE;
            break;
        }
        result = Protocol_Edge_Cursor_Advance_Before(&line->edge,
                                                      sample_tick);
        if (result != PROTOCOL_OK)
        {
            return result;
        }
        logical_level = (uint8_t)(line->edge.level ^
                                  protocol_uart.config.inverted);
        if (logical_level != 0U)
        {
            value |= (uint16_t)(1U << bit);
            parity ^= 1U;
        }
    }

    if ((protocol_uart.config.parity != PROTOCOL_UART_PARITY_NONE) &&
        ((flags & PROTOCOL_UART_FLAG_INCOMPLETE) == 0U))
    {
        uint8_t parity_level;
        uint8_t expected;

        phase = (uint8_t)(3U + (protocol_uart.config.data_bits * 2U));
        sample_tick = Protocol_UART_Sample_Tick(start_tick, phase);
        if (sample_tick > protocol_uart.end_tick)
        {
            flags |= PROTOCOL_UART_FLAG_INCOMPLETE;
        }
        else
        {
            result = Protocol_Edge_Cursor_Advance_Before(&line->edge,
                                                          sample_tick);
            if (result != PROTOCOL_OK)
            {
                return result;
            }
            parity_level = (uint8_t)(line->edge.level ^
                                     protocol_uart.config.inverted);
            expected = (protocol_uart.config.parity ==
                        PROTOCOL_UART_PARITY_EVEN) ? parity :
                                                   (uint8_t)(parity ^ 1U);
            if (parity_level != expected)
            {
                flags |= PROTOCOL_UART_FLAG_PARITY_ERROR;
            }
        }
    }

    for (uint8_t stop = 0U;
         (stop < protocol_uart.config.stop_bits) &&
         ((flags & PROTOCOL_UART_FLAG_INCOMPLETE) == 0U);
         stop++)
    {
        uint8_t parity_bits = (protocol_uart.config.parity ==
                               PROTOCOL_UART_PARITY_NONE) ? 0U : 1U;

        phase = (uint8_t)(3U +
                          ((protocol_uart.config.data_bits + parity_bits +
                            stop) * 2U));
        sample_tick = Protocol_UART_Sample_Tick(start_tick, phase);
        if (sample_tick > protocol_uart.end_tick)
        {
            flags |= PROTOCOL_UART_FLAG_INCOMPLETE;
            break;
        }
        result = Protocol_Edge_Cursor_Advance_Before(&line->edge,
                                                      sample_tick);
        if (result != PROTOCOL_OK)
        {
            return result;
        }
        if ((uint8_t)(line->edge.level ^
                      protocol_uart.config.inverted) == 0U)
        {
            flags |= PROTOCOL_UART_FLAG_FRAME_ERROR;
        }
    }

    {
        uint8_t parity_bits = (protocol_uart.config.parity ==
                               PROTOCOL_UART_PARITY_NONE) ? 0U : 1U;
        uint32_t end_phase = (uint32_t)(2U *
                             (1U + protocol_uart.config.data_bits +
                              parity_bits + protocol_uart.config.stop_bits));
        uint32_t frame_end = Protocol_UART_Sample_Tick(start_tick, end_phase);

        if (frame_end > protocol_uart.end_tick)
        {
            frame_end = protocol_uart.end_tick;
        }
        record.start_tick = start_tick;
        record.end_tick = frame_end;
    }
    record.value = value;
    record.aux = protocol_uart.config.data_bits;
    record.type = PROTOCOL_UART_RECORD_DATA;
    record.flags = (uint8_t)(flags | line->direction);
    if ((flags & PROTOCOL_UART_FLAG_INCOMPLETE) != 0U)
    {
        line->finished = 1U;
    }
    return protocol_uart.emit(&record, protocol_uart.emit_context);
}

/**
 * @brief  查看一个UART方向的下一条边沿
 */
static Protocol_Result_t Protocol_UART_Peek_Line(Protocol_UART_Line_t *line,
                                                 uint32_t *timestamp,
                                                 uint8_t *has_edge)
{
    Protocol_Result_t result;

    *has_edge = 0U;
    if (line->finished != 0U)
    {
        return PROTOCOL_OK;
    }
    result = Protocol_Edge_Cursor_Peek(&line->edge, timestamp, has_edge);
    if ((result == PROTOCOL_OK) && (*has_edge == 0U))
    {
        line->finished = 1U;
    }
    return result;
}

/**
 * @brief  按预算增量解析UART的TX和RX边沿
 * @note   双方向按起始计数值合并，同一计数值优先TX
 */
static Protocol_Result_t Protocol_UART_Process(uint16_t budget,
                                                uint8_t *complete)
{
    uint16_t processed = 0U;
    uint8_t active_level = protocol_uart.config.inverted;

    if (complete == NULL)
    {
        return PROTOCOL_ERROR_INVALID_PARAMETER;
    }
    *complete = 0U;

    if ((protocol_uart.tx.finished != 0U) &&
        (protocol_uart.rx.finished != 0U))
    {
        *complete = 1U;
        return PROTOCOL_OK;
    }

    while (processed < budget)
    {
        Protocol_Result_t result;
        Protocol_UART_Line_t *line;
        uint32_t tx_tick = 0U;
        uint32_t rx_tick = 0U;
        uint32_t timestamp;
        uint8_t level_after;
        uint8_t has_edge;
        uint8_t has_tx;
        uint8_t has_rx;

        result = Protocol_UART_Peek_Line(&protocol_uart.tx, &tx_tick, &has_tx);
        if (result != PROTOCOL_OK)
        {
            return result;
        }
        result = Protocol_UART_Peek_Line(&protocol_uart.rx, &rx_tick, &has_rx);
        if (result != PROTOCOL_OK)
        {
            return result;
        }
        if ((has_tx == 0U) && (has_rx == 0U))
        {
            *complete = 1U;
            return PROTOCOL_OK;
        }
        line = ((has_tx != 0U) &&
                ((has_rx == 0U) || (tx_tick <= rx_tick))) ?
               &protocol_uart.tx : &protocol_uart.rx;
        result = Protocol_Edge_Cursor_Pop(&line->edge,
                                           &timestamp,
                                           &level_after,
                                           &has_edge);
        if (result != PROTOCOL_OK)
        {
            return result;
        }
        if (has_edge == 0U)
        {
            line->finished = 1U;
            continue;
        }
        processed++;
        if (level_after == active_level)
        {
            result = Protocol_UART_Emit_Frame(line, timestamp);
            if (result != PROTOCOL_OK)
            {
                return result;
            }
            if ((protocol_uart.tx.finished != 0U) &&
                (protocol_uart.rx.finished != 0U))
            {
                *complete = 1U;
                return PROTOCOL_OK;
            }
        }
    }
    return PROTOCOL_OK;
}

/**
 * @brief  结束本次UART解析
 */
static void Protocol_UART_Close(void)
{
}

/* 解析器接口 */

const Protocol_Decoder_Ops_t protocol_uart_decoder_ops = {Protocol_UART_Open,
                                                          Protocol_UART_Process,
                                                          Protocol_UART_Close};
