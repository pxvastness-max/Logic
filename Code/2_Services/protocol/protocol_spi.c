/**
 * @file   protocol_spi.c
 * @brief  SPI边沿时间戳解析器
 */

/* ================================================== */

#include "protocol/protocol_internal.h"
#include "stddef.h"

/* ================================================== */

typedef struct
{
    Protocol_SPI_Config_t config; /* SPI模式、位序和信号配置 */
    Protocol_Edge_Cursor_t clk;   /* CLK边沿游标 */
    Protocol_Edge_Cursor_t mosi;  /* MOSI边沿游标 */
    Protocol_Edge_Cursor_t miso;  /* MISO边沿游标 */
    Protocol_Edge_Cursor_t cs;    /* CS边沿游标 */
    Protocol_Emit_t emit;         /* 记录输出回调 */
    void *emit_context;           /* 记录回调上下文 */
    uint32_t word_start_tick;     /* 当前字首个采样计数值 */
    uint32_t end_tick;            /* 解析截止计数值 */
    uint16_t mosi_value;          /* 当前字MOSI数据 */
    uint16_t miso_value;          /* 当前字MISO数据 */
    uint8_t bit_count;            /* 当前字的已采样位数 */
    uint8_t has_mosi;             /* 是否启用MOSI */
    uint8_t has_miso;             /* 是否启用MISO */
    uint8_t has_cs;               /* 是否启用CS */
} Protocol_SPI_Context_t;

/* ================================================== */

/* 单会话私有状态，仅由解析服务任务串行访问，Open负责重新初始化 */
static Protocol_SPI_Context_t protocol_spi;

/* ================================================== */

/**
 * @brief  构造并输出一条SPI记录
 */
static Protocol_Result_t Protocol_SPI_Emit(uint8_t type,
                                            uint32_t start_tick,
                                            uint32_t end_tick,
                                            uint32_t value,
                                            uint16_t aux,
                                            uint8_t flags)
{
    Protocol_Record_t record;

    record.start_tick = start_tick;
    record.end_tick = end_tick;
    record.value = value;
    record.aux = aux;
    record.type = type;
    record.flags = flags;
    return protocol_spi.emit(&record, protocol_spi.emit_context);
}

/**
 * @brief  初始化SPI模式及可选信号的边沿游标
 */
static Protocol_Result_t Protocol_SPI_Open(
                                    const Protocol_Config_t *config,
                                    const Protocol_Info_t *info,
                                    const LA_Service_Query_View_t *view,
                                    Protocol_Emit_t emit,
                                    void *emit_context)
{
    Protocol_Result_t result;

    if ((config == NULL) || (info == NULL) || (view == NULL) ||
        (emit == NULL))
    {
        return PROTOCOL_ERROR_INVALID_PARAMETER;
    }

    protocol_spi.config = config->decoder.spi;
    protocol_spi.emit = emit;
    protocol_spi.emit_context = emit_context;
    protocol_spi.word_start_tick = 0U;
    protocol_spi.end_tick = info->end_tick;
    protocol_spi.mosi_value = 0U;
    protocol_spi.miso_value = 0U;
    protocol_spi.bit_count = 0U;
    protocol_spi.has_mosi = ((uint32_t)protocol_spi.config.mosi_channel <
                             LA_SERVICE_CHANNEL_COUNT) ? 1U : 0U;
    protocol_spi.has_miso = ((uint32_t)protocol_spi.config.miso_channel <
                             LA_SERVICE_CHANNEL_COUNT) ? 1U : 0U;
    protocol_spi.has_cs = ((uint32_t)protocol_spi.config.cs_channel <
                           LA_SERVICE_CHANNEL_COUNT) ? 1U : 0U;

    result = Protocol_Edge_Cursor_Open(&protocol_spi.clk,
                                        view,
                                        protocol_spi.config.clk_channel,
                                        info->start_tick,
                                        info->end_tick);
    if ((result == PROTOCOL_OK) && (protocol_spi.has_mosi != 0U))
    {
        result = Protocol_Edge_Cursor_Open(&protocol_spi.mosi,
                                            view,
                                            protocol_spi.config.mosi_channel,
                                            info->start_tick,
                                            info->end_tick);
    }
    if ((result == PROTOCOL_OK) && (protocol_spi.has_miso != 0U))
    {
        result = Protocol_Edge_Cursor_Open(&protocol_spi.miso,
                                            view,
                                            protocol_spi.config.miso_channel,
                                            info->start_tick,
                                            info->end_tick);
    }
    if ((result == PROTOCOL_OK) && (protocol_spi.has_cs != 0U))
    {
        result = Protocol_Edge_Cursor_Open(&protocol_spi.cs,
                                            view,
                                            protocol_spi.config.cs_channel,
                                            info->start_tick,
                                            info->end_tick);
    }
    return result;
}

/**
 * @brief  输出并清除尚未收满的SPI字
 */
static Protocol_Result_t Protocol_SPI_Emit_Partial(uint32_t end_tick)
{
    Protocol_Result_t result;

    if (protocol_spi.bit_count == 0U)
    {
        return PROTOCOL_OK;
    }

    result = Protocol_SPI_Emit(PROTOCOL_SPI_RECORD_WORD,
                               protocol_spi.word_start_tick,
                               end_tick,
                               ((uint32_t)protocol_spi.miso_value << 16) |
                               protocol_spi.mosi_value,
                               protocol_spi.bit_count,
                               PROTOCOL_SPI_FLAG_INCOMPLETE);
    protocol_spi.bit_count = 0U;
    protocol_spi.mosi_value = 0U;
    protocol_spi.miso_value = 0U;
    return result;
}

/**
 * @brief  输出SPI片选边沿并在失效时提交部分字
 */
static Protocol_Result_t Protocol_SPI_Handle_CS(uint32_t timestamp,
                                                 uint8_t level_after)
{
    uint8_t active = (level_after ==
                      protocol_spi.config.cs_active_level) ? 1U : 0U;
    Protocol_Result_t result = PROTOCOL_OK;

    if (active == 0U)
    {
        result = Protocol_SPI_Emit_Partial(timestamp);
    }
    if (result != PROTOCOL_OK)
    {
        return result;
    }

    return Protocol_SPI_Emit((active != 0U) ?
                              PROTOCOL_SPI_RECORD_CS_ASSERT :
                              PROTOCOL_SPI_RECORD_CS_DEASSERT,
                              timestamp,
                              timestamp,
                              level_after,
                              0U,
                              0U);
}

/**
 * @brief  按CPOL/CPHA和位序采样SPI数据
 */
static Protocol_Result_t Protocol_SPI_Handle_Clock(uint32_t timestamp,
                                                    uint8_t level_after)
{
    uint8_t cpol = (uint8_t)((protocol_spi.config.clock_mode >> 1) & 1U);
    uint8_t cpha = (uint8_t)(protocol_spi.config.clock_mode & 1U);
    uint8_t leading = (level_after != cpol) ? 1U : 0U;
    uint8_t sample_edge = (cpha == 0U) ? leading : (uint8_t)(leading ^ 1U);
    uint8_t active = (protocol_spi.has_cs == 0U) ? 1U :
                     ((protocol_spi.cs.level ==
                       protocol_spi.config.cs_active_level) ? 1U : 0U);

    if ((sample_edge == 0U) || (active == 0U))
    {
        return PROTOCOL_OK;
    }

    if (protocol_spi.bit_count == 0U)
    {
        protocol_spi.word_start_tick = timestamp;
    }

    if (protocol_spi.config.bit_order == PROTOCOL_BIT_ORDER_MSB_FIRST)
    {
        protocol_spi.mosi_value = (uint16_t)(protocol_spi.mosi_value << 1);
        protocol_spi.miso_value = (uint16_t)(protocol_spi.miso_value << 1);
        if ((protocol_spi.has_mosi != 0U) &&
            (protocol_spi.mosi.level != 0U))
        {
            protocol_spi.mosi_value |= 1U;
        }
        if ((protocol_spi.has_miso != 0U) &&
            (protocol_spi.miso.level != 0U))
        {
            protocol_spi.miso_value |= 1U;
        }
    }
    else
    {
        if ((protocol_spi.has_mosi != 0U) &&
            (protocol_spi.mosi.level != 0U))
        {
            protocol_spi.mosi_value |= (uint16_t)(1U <<
                                                   protocol_spi.bit_count);
        }
        if ((protocol_spi.has_miso != 0U) &&
            (protocol_spi.miso.level != 0U))
        {
            protocol_spi.miso_value |= (uint16_t)(1U <<
                                                   protocol_spi.bit_count);
        }
    }
    protocol_spi.bit_count++;

    if (protocol_spi.bit_count >= protocol_spi.config.word_bits)
    {
        Protocol_Result_t result = Protocol_SPI_Emit(
                                    PROTOCOL_SPI_RECORD_WORD,
                                    protocol_spi.word_start_tick,
                                    timestamp,
                                    ((uint32_t)protocol_spi.miso_value << 16) |
                                    protocol_spi.mosi_value,
                                    protocol_spi.config.word_bits,
                                    0U);

        protocol_spi.bit_count = 0U;
        protocol_spi.mosi_value = 0U;
        protocol_spi.miso_value = 0U;
        return result;
    }
    return PROTOCOL_OK;
}

/**
 * @brief  按预算处理SPI时钟和片选边沿
 */
static Protocol_Result_t Protocol_SPI_Process(uint16_t budget,
                                               uint8_t *complete)
{
    uint16_t processed = 0U;

    if (complete == NULL)
    {
        return PROTOCOL_ERROR_INVALID_PARAMETER;
    }
    *complete = 0U;

    while (processed < budget)
    {
        Protocol_Result_t result;
        uint32_t clk_tick = 0U;
        uint32_t cs_tick = 0U;
        uint8_t has_clk;
        uint8_t has_cs = 0U;

        result = Protocol_Edge_Cursor_Peek(&protocol_spi.clk,
                                            &clk_tick,
                                            &has_clk);
        if (result != PROTOCOL_OK)
        {
            return result;
        }
        if (protocol_spi.has_cs != 0U)
        {
            result = Protocol_Edge_Cursor_Peek(&protocol_spi.cs,
                                                &cs_tick,
                                                &has_cs);
            if (result != PROTOCOL_OK)
            {
                return result;
            }
        }

        if ((has_clk == 0U) && (has_cs == 0U))
        {
            result = Protocol_SPI_Emit_Partial(
                                        protocol_spi.end_tick);
            if (result != PROTOCOL_OK)
            {
                return result;
            }
            *complete = 1U;
            return PROTOCOL_OK;
        }

        if ((has_cs != 0U) &&
            ((has_clk == 0U) || (cs_tick < clk_tick)))
        {
            uint8_t level_after;

            result = Protocol_Edge_Cursor_Pop(&protocol_spi.cs,
                                               &cs_tick,
                                               &level_after,
                                               &has_cs);
            if (result != PROTOCOL_OK)
            {
                return result;
            }
            result = Protocol_SPI_Handle_CS(cs_tick, level_after);
        }
        else
        {
            uint8_t level_after;

            if (protocol_spi.has_mosi != 0U)
            {
                result = Protocol_Edge_Cursor_Advance_Before(
                                                    &protocol_spi.mosi,
                                                    clk_tick);
                if (result != PROTOCOL_OK)
                {
                    return result;
                }
            }
            if (protocol_spi.has_miso != 0U)
            {
                result = Protocol_Edge_Cursor_Advance_Before(
                                                    &protocol_spi.miso,
                                                    clk_tick);
                if (result != PROTOCOL_OK)
                {
                    return result;
                }
            }
            result = Protocol_Edge_Cursor_Pop(&protocol_spi.clk,
                                               &clk_tick,
                                               &level_after,
                                               &has_clk);
            if (result != PROTOCOL_OK)
            {
                return result;
            }
            result = Protocol_SPI_Handle_Clock(clk_tick, level_after);
        }

        if (result != PROTOCOL_OK)
        {
            return result;
        }
        processed++;
    }
    return PROTOCOL_OK;
}

/**
 * @brief  结束本次SPI解析
 */
static void Protocol_SPI_Close(void)
{
}

/* 解析器接口 */

const Protocol_Decoder_Ops_t protocol_spi_decoder_ops = {Protocol_SPI_Open,
                                                         Protocol_SPI_Process,
                                                         Protocol_SPI_Close};
