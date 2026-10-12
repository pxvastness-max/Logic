/**
 * @file   protocol_internal.h
 * @brief  协议解析服务内部接口
 */

#ifndef PROTOCOL_INTERNAL_H
#define PROTOCOL_INTERNAL_H

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================== */

#include "protocol/protocol_service.h"

/* ========================= 配置 ========================= */

#define PROTOCOL_EDGE_BATCH_COUNT 64U /* 每个游标的边沿读取批量 */

/* ================================================== */

typedef Protocol_Result_t (*Protocol_Emit_t)(const Protocol_Record_t *record, void *context);

typedef struct
{
    const LA_Service_Query_View_t *view;      /* 服务持有的视图，游标不拥有其寿命 */
    LA_Service_Channel_t channel;             /* 当前输入通道 */
    uint32_t current_index;                   /* 下一条尚未消费的边沿下标 */
    uint32_t after_index;                     /* 结束时间之后第一条边沿的下标 */
    uint32_t data[PROTOCOL_EDGE_BATCH_COUNT]; /* 分批读取的边沿缓冲 */
    uint16_t data_count;                      /* 本批有效记录数量 */
    uint16_t data_position;                   /* 本批下一条待消费记录位置 */
    uint8_t level;                            /* 已消费全部边沿后的当前电平 */
} Protocol_Edge_Cursor_t;

/* 单会话解码器，静态状态仅由协议任务串行使用 */
typedef struct
{
    Protocol_Result_t (*open)(const Protocol_Config_t *config,
                              const Protocol_Info_t *info,
                              const LA_Service_Query_View_t *view,
                              Protocol_Emit_t emit,
                              void *emit_context);
    Protocol_Result_t (*process)(uint16_t budget, uint8_t *complete);
    void (*close)(void);
} Protocol_Decoder_Ops_t;

/* ================================================== */

/**
 * @brief  在固定的 LA 数据中按时间读取单通道边沿，包含起止时间
 * @param  cursor 非 NULL 的游标输出，Open 成功后由当前解码器独占使用
 * @retval PROTOCOL_OK 游标位置及起点翻转前电平初始化完成
 * @note   游标借用 view，不获取或释放查询锁，不可跨视图拥有者任务使用
 */
Protocol_Result_t Protocol_Edge_Cursor_Open(
                                    Protocol_Edge_Cursor_t *cursor,
                                    const LA_Service_Query_View_t *view,
                                    LA_Service_Channel_t channel,
                                    uint32_t start_tick,
                                    uint32_t end_tick);

/**
 * @brief  查看下一条边沿而不消费游标或翻转电平
 * @param  timestamp 非 NULL 的输出
 * @retval PROTOCOL_OK 查看成功，范围耗尽也是成功
 */
Protocol_Result_t Protocol_Edge_Cursor_Peek(
                                    Protocol_Edge_Cursor_t *cursor,
                                    uint32_t *timestamp,
                                    uint8_t *has_edge);

/**
 * @brief  消费下一条边沿并更新游标电平
 * @param  timestamp 非 NULL 的边沿 CNT 输出
 * @retval PROTOCOL_OK 消费成功或范围已耗尽
 */
Protocol_Result_t Protocol_Edge_Cursor_Pop(
                                    Protocol_Edge_Cursor_t *cursor,
                                    uint32_t *timestamp,
                                    uint8_t *level_after,
                                    uint8_t *has_edge);

/**
 * @brief  消费所有严格早于目标 CNT 的边沿并维持正确电平
 * @retval PROTOCOL_OK 推进成功，范围耗尽也为成功
 */
Protocol_Result_t Protocol_Edge_Cursor_Advance_Before(
                                    Protocol_Edge_Cursor_t *cursor,
                                    uint32_t timestamp);

extern const Protocol_Decoder_Ops_t protocol_uart_decoder_ops;
extern const Protocol_Decoder_Ops_t protocol_iic_decoder_ops;
extern const Protocol_Decoder_Ops_t protocol_spi_decoder_ops;

/* ================================================== */

#ifdef __cplusplus
}
#endif

#endif /* PROTOCOL_INTERNAL_H */
