/**
 * @file   protocol_iic.c
 * @brief  IIC边沿时间戳解析器
 */

/* ================================================== */

#include "protocol/protocol_internal.h"
#include "stddef.h"

/* ================================================== */

typedef struct
{
    Protocol_IIC_Config_t config; /* IIC通道和地址长度配置 */
    Protocol_Edge_Cursor_t scl;   /* SCL边沿游标 */
    Protocol_Edge_Cursor_t sda;   /* SDA边沿游标 */
    Protocol_Emit_t emit;         /* 记录输出回调 */
    void *emit_context;           /* 记录回调上下文 */
    uint32_t byte_start_tick;     /* 当前字节首个采样计数值 */
    uint16_t last_10_address;     /* 已解析的10位地址 */
    uint16_t pending_10_high;     /* 等待第二地址字节的高位 */
    uint8_t byte_value;           /* 当前字节的已采样数据 */
    uint8_t bit_count;            /* 当前字节的已采样位数 */
    uint8_t first_sample_pending; /* 首个高相位需等下降沿确认，可能用于STOP/RESTART */
    uint8_t started;              /* 是否处于有效事务 */
    uint8_t expecting_address;    /* 下一字节是否为地址 */
    uint8_t expecting_10_second;  /* 是否等待10位地址第二字节 */
    uint8_t has_last_10_address;  /* 是否保存了可用于重复起始的10位地址 */
    uint8_t transaction_read;     /* 当前事务是否为读 */
} Protocol_IIC_Context_t;

/* ================================================== */

/* 单会话私有状态，仅由解析服务任务串行访问，Open负责重新初始化 */
static Protocol_IIC_Context_t protocol_iic;

/* ================================================== */

/**
 * @brief  构造并输出一条IIC记录
 */
static Protocol_Result_t Protocol_IIC_Emit(uint8_t type,
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
    return protocol_iic.emit(&record, protocol_iic.emit_context);
}

/**
 * @brief  初始化IIC事务状态和SCL/SDA边沿游标
 */
static Protocol_Result_t Protocol_IIC_Open(
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

    protocol_iic.config = config->decoder.iic;
    protocol_iic.emit = emit;
    protocol_iic.emit_context = emit_context;
    protocol_iic.byte_start_tick = 0U;
    protocol_iic.last_10_address = 0U;
    protocol_iic.pending_10_high = 0U;
    protocol_iic.byte_value = 0U;
    protocol_iic.bit_count = 0U;
    protocol_iic.first_sample_pending = 0U;
    protocol_iic.started = 0U;
    protocol_iic.expecting_address = 0U;
    protocol_iic.expecting_10_second = 0U;
    protocol_iic.has_last_10_address = 0U;
    protocol_iic.transaction_read = 0U;

    result = Protocol_Edge_Cursor_Open(&protocol_iic.scl,
                                        view,
                                        protocol_iic.config.scl_channel,
                                        info->start_tick,
                                        info->end_tick);
    if (result != PROTOCOL_OK)
    {
        return result;
    }
    return Protocol_Edge_Cursor_Open(&protocol_iic.sda,
                                      view,
                                      protocol_iic.config.sda_channel,
                                      info->start_tick,
                                      info->end_tick);
}

/**
 * @brief  处理IIC起始或重复起始条件
 */
static Protocol_Result_t Protocol_IIC_Handle_Start(uint32_t timestamp)
{
    uint8_t type = (protocol_iic.started == 0U) ?
                   PROTOCOL_IIC_RECORD_START :
                   PROTOCOL_IIC_RECORD_RESTART;
    Protocol_Result_t result;

    if ((protocol_iic.started != 0U) && (protocol_iic.bit_count != 0U))
    {
        result = Protocol_IIC_Emit(PROTOCOL_IIC_RECORD_ERROR,
                                   protocol_iic.byte_start_tick,
                                   timestamp,
                                   protocol_iic.byte_value,
                                   protocol_iic.bit_count,
                                   PROTOCOL_IIC_FLAG_INCOMPLETE);
        if (result != PROTOCOL_OK)
        {
            return result;
        }
    }

    protocol_iic.started = 1U;
    protocol_iic.expecting_address = 1U;
    protocol_iic.expecting_10_second = 0U;
    protocol_iic.bit_count = 0U;
    protocol_iic.first_sample_pending = 0U;
    protocol_iic.byte_value = 0U;
    return Protocol_IIC_Emit(type,
                              timestamp,
                              timestamp,
                              0U,
                              0U,
                              0U);
}

/**
 * @brief  处理IIC停止条件并报告未完成字节
 */
static Protocol_Result_t Protocol_IIC_Handle_Stop(uint32_t timestamp)
{
    Protocol_Result_t result = PROTOCOL_OK;

    if ((protocol_iic.started != 0U) && (protocol_iic.bit_count != 0U))
    {
        result = Protocol_IIC_Emit(PROTOCOL_IIC_RECORD_ERROR,
                                   protocol_iic.byte_start_tick,
                                   timestamp,
                                   protocol_iic.byte_value,
                                   protocol_iic.bit_count,
                                   PROTOCOL_IIC_FLAG_INCOMPLETE);
    }
    if (result == PROTOCOL_OK)
    {
        result = Protocol_IIC_Emit(PROTOCOL_IIC_RECORD_STOP,
                                   timestamp,
                                   timestamp,
                                   0U,
                                   0U,
                                   0U);
    }

    protocol_iic.started = 0U;
    protocol_iic.expecting_address = 0U;
    protocol_iic.expecting_10_second = 0U;
    protocol_iic.bit_count = 0U;
    protocol_iic.first_sample_pending = 0U;
    protocol_iic.byte_value = 0U;
    return result;
}

/**
 * @brief  结合ACK电平输出IIC地址或数据字节
 */
static Protocol_Result_t Protocol_IIC_Complete_Byte(uint32_t timestamp,
                                                     uint8_t ack_level)
{
    uint8_t flags = (ack_level == 0U) ?
                    PROTOCOL_IIC_FLAG_ACK : PROTOCOL_IIC_FLAG_NACK;
    Protocol_Result_t result = PROTOCOL_OK;

    if (protocol_iic.expecting_address != 0U)
    {
        uint8_t read = (uint8_t)(protocol_iic.byte_value & 1U);

        flags |= (read != 0U) ? PROTOCOL_IIC_FLAG_READ :
                               PROTOCOL_IIC_FLAG_WRITE;
        protocol_iic.transaction_read = read;
        if (protocol_iic.config.address_bits == 7U)
        {
            result = Protocol_IIC_Emit(PROTOCOL_IIC_RECORD_ADDRESS,
                                       protocol_iic.byte_start_tick,
                                       timestamp,
                                       protocol_iic.byte_value >> 1,
                                       7U,
                                       flags);
            protocol_iic.expecting_address = 0U;
        }
        else if ((protocol_iic.byte_value & 0xF8U) == 0xF0U)
        {
            uint16_t high = (uint16_t)((protocol_iic.byte_value & 0x06U) << 7);

            if ((read != 0U) &&
                (protocol_iic.has_last_10_address != 0U) &&
                ((protocol_iic.last_10_address & 0x0300U) == high))
            {
                result = Protocol_IIC_Emit(PROTOCOL_IIC_RECORD_ADDRESS,
                                           protocol_iic.byte_start_tick,
                                           timestamp,
                                           protocol_iic.last_10_address,
                                           10U,
                                           flags);
                protocol_iic.expecting_address = 0U;
            }
            else
            {
                protocol_iic.pending_10_high = high;
                protocol_iic.expecting_10_second = 1U;
                protocol_iic.expecting_address = 0U;
            }
        }
        else
        {
            flags |= PROTOCOL_IIC_FLAG_BUS_ERROR;
            result = Protocol_IIC_Emit(PROTOCOL_IIC_RECORD_ERROR,
                                       protocol_iic.byte_start_tick,
                                       timestamp,
                                       protocol_iic.byte_value,
                                       10U,
                                       flags);
            protocol_iic.expecting_address = 0U;
        }
    }
    else if (protocol_iic.expecting_10_second != 0U)
    {
        protocol_iic.last_10_address = (uint16_t)(
                                        protocol_iic.pending_10_high |
                                        protocol_iic.byte_value);
        protocol_iic.has_last_10_address = 1U;
        protocol_iic.expecting_10_second = 0U;
        flags |= PROTOCOL_IIC_FLAG_WRITE;
        result = Protocol_IIC_Emit(PROTOCOL_IIC_RECORD_ADDRESS,
                                   protocol_iic.byte_start_tick,
                                   timestamp,
                                   protocol_iic.last_10_address,
                                   10U,
                                   flags);
    }
    else
    {
        flags |= (protocol_iic.transaction_read != 0U) ?
                 PROTOCOL_IIC_FLAG_READ : PROTOCOL_IIC_FLAG_WRITE;
        result = Protocol_IIC_Emit(PROTOCOL_IIC_RECORD_DATA,
                                   protocol_iic.byte_start_tick,
                                   timestamp,
                                   protocol_iic.byte_value,
                                   8U,
                                   flags);
    }

    protocol_iic.bit_count = 0U;
    protocol_iic.byte_value = 0U;
    return result;
}

/**
 * @brief  采样SCL高相位并确认首位是否为数据
 */
static Protocol_Result_t Protocol_IIC_Handle_SCL(uint32_t timestamp,
                                                  uint8_t level_after)
{
    if (protocol_iic.started == 0U)
    {
        return PROTOCOL_OK;
    }
    if (level_after == 0U)
    {
        if (protocol_iic.first_sample_pending != 0U)
        {
            protocol_iic.bit_count = 1U;
            protocol_iic.first_sample_pending = 0U;
        }
        return PROTOCOL_OK;
    }

    if (protocol_iic.bit_count == 0U)
    {
        protocol_iic.byte_start_tick = timestamp;
        protocol_iic.byte_value = protocol_iic.sda.level;
        protocol_iic.first_sample_pending = 1U;
        return PROTOCOL_OK;
    }
    if (protocol_iic.bit_count < 8U)
    {
        protocol_iic.byte_value = (uint8_t)(
                    (protocol_iic.byte_value << 1) | protocol_iic.sda.level);
        protocol_iic.bit_count++;
        return PROTOCOL_OK;
    }

    return Protocol_IIC_Complete_Byte(timestamp, protocol_iic.sda.level);
}

/**
 * @brief  合并SCL和SDA时间顺序并按预算增量解析
 * @note   同一计数值时先处理SDA，跨调用保留事务和字节状态
 */
static Protocol_Result_t Protocol_IIC_Process(uint16_t budget,
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
        uint32_t scl_tick = 0U;
        uint32_t sda_tick = 0U;
        uint8_t has_scl;
        uint8_t has_sda;

        result = Protocol_Edge_Cursor_Peek(&protocol_iic.scl,
                                            &scl_tick,
                                            &has_scl);
        if (result != PROTOCOL_OK)
        {
            return result;
        }
        result = Protocol_Edge_Cursor_Peek(&protocol_iic.sda,
                                            &sda_tick,
                                            &has_sda);
        if (result != PROTOCOL_OK)
        {
            return result;
        }
        if ((has_scl == 0U) && (has_sda == 0U))
        {
            if (protocol_iic.first_sample_pending != 0U)
            {
                protocol_iic.bit_count = 1U;
                protocol_iic.first_sample_pending = 0U;
            }
            if ((protocol_iic.started != 0U) &&
                (protocol_iic.bit_count != 0U))
            {
                result = Protocol_IIC_Emit(PROTOCOL_IIC_RECORD_ERROR,
                                           protocol_iic.byte_start_tick,
                                           protocol_iic.byte_start_tick,
                                           protocol_iic.byte_value,
                                           protocol_iic.bit_count,
                                           PROTOCOL_IIC_FLAG_INCOMPLETE);
                if (result != PROTOCOL_OK)
                {
                    return result;
                }
                protocol_iic.bit_count = 0U;
            }
            *complete = 1U;
            return PROTOCOL_OK;
        }

        if ((has_sda != 0U) &&
            ((has_scl == 0U) || (sda_tick <= scl_tick)))
        {
            uint8_t level_after;

            result = Protocol_Edge_Cursor_Pop(&protocol_iic.sda,
                                               &sda_tick,
                                               &level_after,
                                               &has_sda);
            if (result != PROTOCOL_OK)
            {
                return result;
            }
            if (protocol_iic.scl.level != 0U)
            {
                result = (level_after == 0U) ?
                         Protocol_IIC_Handle_Start(sda_tick) :
                         Protocol_IIC_Handle_Stop(sda_tick);
                if (result != PROTOCOL_OK)
                {
                    return result;
                }
            }
        }
        else
        {
            uint8_t level_after;

            result = Protocol_Edge_Cursor_Pop(&protocol_iic.scl,
                                               &scl_tick,
                                               &level_after,
                                               &has_scl);
            if (result != PROTOCOL_OK)
            {
                return result;
            }
            result = Protocol_IIC_Handle_SCL(scl_tick, level_after);
            if (result != PROTOCOL_OK)
            {
                return result;
            }
        }
        processed++;
    }
    return PROTOCOL_OK;
}

/**
 * @brief  结束本次IIC解析
 */
static void Protocol_IIC_Close(void)
{
}

/* 解析器接口 */

const Protocol_Decoder_Ops_t protocol_iic_decoder_ops = {Protocol_IIC_Open,
                                                         Protocol_IIC_Process,
                                                         Protocol_IIC_Close};
