/**
 * @file   protocol_service.h
 * @brief  逻辑分析仪协议解析服务公开接口
 */

#ifndef PROTOCOL_SERVICE_H
#define PROTOCOL_SERVICE_H

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================== */

#include "stdint.h"
#include "la/la_service.h"

/* ================================================== */

#define PROTOCOL_RESULT_CAPACITY   131072UL /* 2MB / 16B */
#define PROTOCOL_RECORD_READ_LIMIT 64U      /* 单次读取上限，限制锁内复制为1KB */
#define PROTOCOL_CHANNEL_UNUSED    LA_SERVICE_CHANNEL_COUNT

/* ================================================== */

typedef enum
{
    PROTOCOL_TYPE_UART = 0,
    PROTOCOL_TYPE_IIC,
    PROTOCOL_TYPE_SPI,
    PROTOCOL_TYPE_COUNT
} Protocol_Type_t;

typedef enum
{
    PROTOCOL_STATE_RESET = 0, /* 服务资源尚未初始化 */
    PROTOCOL_STATE_IDLE,      /* 无活动解析会话 */
    PROTOCOL_STATE_STARTING,  /* 已接受请求，正在取得查询视图或打开解码器 */
    PROTOCOL_STATE_RUNNING,   /* 后台按预算增量解析 */
    PROTOCOL_STATE_COMPLETE,  /* 范围内解析结束且结果池未溢出 */
    PROTOCOL_STATE_TRUNCATED, /* 解析完成但结果池已满，部分记录被丢弃 */
    PROTOCOL_STATE_CANCELLED, /* 取消完成，查询锁和采集数据保留权已释放 */
    PROTOCOL_STATE_ERROR      /* 查询或解码失败后的终态 */
} Protocol_State_t;

typedef enum
{
    PROTOCOL_OK = 0,
    PROTOCOL_ERROR_BUSY,
    PROTOCOL_ERROR_INVALID_PARAMETER,
    PROTOCOL_ERROR_NOT_READY,
    PROTOCOL_ERROR_NO_MEMORY,
    PROTOCOL_ERROR_LA_SERVICE,
    PROTOCOL_ERROR_DECODER,
    PROTOCOL_ERROR_STATE
} Protocol_Result_t;

typedef enum
{
    PROTOCOL_EVENT_UPDATED = 0,
    PROTOCOL_EVENT_COMPLETE,
    PROTOCOL_EVENT_CANCELLED,
    PROTOCOL_EVENT_ERROR
} Protocol_Event_Type_t;

typedef enum
{
    PROTOCOL_UART_PARITY_NONE = 0,
    PROTOCOL_UART_PARITY_EVEN,
    PROTOCOL_UART_PARITY_ODD
} Protocol_UART_Parity_t;

typedef enum
{
    PROTOCOL_BIT_ORDER_MSB_FIRST = 0,
    PROTOCOL_BIT_ORDER_LSB_FIRST
} Protocol_Bit_Order_t;

/* TX、RX至少启用一路，同时启用时必须使用不同通道，共用帧格式和极性 */
typedef struct
{
    LA_Service_Channel_t tx_channel; /* 不使用时为PROTOCOL_CHANNEL_UNUSED */
    LA_Service_Channel_t rx_channel; /* 不使用时为PROTOCOL_CHANNEL_UNUSED */
    uint32_t baud_rate;              /* 请求波特率，非零且不超过采集 timestamp_hz 的三分之一 */
    uint8_t data_bits;               /* 5..9 */
    uint8_t stop_bits;               /* 1..2 */
    Protocol_UART_Parity_t parity;   /* 无校验、偶校验或奇校验，两方向共用 */
    uint8_t inverted;                /* 0-空闲高电平，1-空闲低电平 */
} Protocol_UART_Config_t;

typedef struct
{
    LA_Service_Channel_t scl_channel; /* 时钟通道 0..5，与 SDA 不同 */
    LA_Service_Channel_t sda_channel; /* 数据通道 0..5，与 SCL 不同 */
    uint8_t address_bits;             /* 7或10 */
} Protocol_IIC_Config_t;

typedef struct
{
    LA_Service_Channel_t clk_channel;  /* 必需的时钟通道 0..5，与启用的其他信号不同 */
    LA_Service_Channel_t mosi_channel; /* 不使用时为PROTOCOL_CHANNEL_UNUSED */
    LA_Service_Channel_t miso_channel; /* 不使用时为PROTOCOL_CHANNEL_UNUSED */
    LA_Service_Channel_t cs_channel;   /* 不使用时为PROTOCOL_CHANNEL_UNUSED */
    uint8_t clock_mode;                /* CPOL << 1 | CPHA，范围0..3 */
    Protocol_Bit_Order_t bit_order;    /* 字内最高位先行或最低位先行 */
    uint8_t word_bits;                 /* 1..16 */
    uint8_t cs_active_level;           /* 0-低有效，1-高有效 */
} Protocol_SPI_Config_t;

typedef struct
{
    Protocol_Type_t protocol; /* 联合体中本次有效的 UART/IIC/SPI 配置类型 */
    uint32_t start_tick;      /* 解析从此计数开始，包含这一时刻 */
    uint32_t end_tick;        /* UINT32_MAX表示使用采集截止点 */

    union
    {
        Protocol_UART_Config_t uart;
        Protocol_IIC_Config_t iic;
        Protocol_SPI_Config_t spi;
    } decoder;
} Protocol_Config_t;

/**
 * @brief  统一的16字节解析记录
 */
typedef struct
{
    uint32_t start_tick; /* 记录起始 CNT，结果按该字段有序发布 */
    uint32_t end_tick;   /* 记录结束 CNT，允许与其他方向的 UART 帧重叠 */
    uint32_t value;      /* UART 数据、IIC 地址/数据或 SPI 的双向字值 */
    uint16_t aux;        /* 协议附加信息，例如 UART 数据位数和 SPI 字位数 */
    uint8_t type;        /* 当前协议对应的记录类型枚举 */
    uint8_t flags;       /* 当前协议的方向、应答和错误位图 */
} Protocol_Record_t;

typedef struct
{
    uint32_t session_id;      /* 非零会话代数，Start 更新，读取时用于防止跨会话混读 */
    uint32_t capture_id;      /* 接受Start时保留的LA采集编号 */
    uint32_t timestamp_hz;    /* 本会话采集时间轴频率，用于解释 CNT */
    uint32_t start_tick;      /* 实际解析的起始计数，包含这一时刻 */
    uint32_t end_tick;        /* 实际解析的结束计数，包含这一时刻；已处理 UINT32_MAX */
    uint32_t result_count;    /* 已经发布且可读取的记录数量 */
    uint32_t dropped_count;   /* 结果池容量不足而未保留的记录数量 */
    uint32_t error_count;     /* 包含协议错误标志的记录数量，UART 方向标志不计为错误 */
    Protocol_Type_t protocol; /* 本会话协议类型 */
    Protocol_State_t state;   /* 当前会话状态，不表示 LA 硬件采集状态 */
    Protocol_Config_t config; /* 本会话实际使用的解析配置 */
} Protocol_Info_t;

typedef struct
{
    Protocol_Event_Type_t type; /* 增量发布或结束事件类型 */
    Protocol_Info_t info;       /* 事件所属会话的不可变状态副本 */
} Protocol_Event_t;

/* 协议任务调用回调；event 是当次状态的独立副本，只在回调期间有效 */
typedef void (*Protocol_Event_Callback_t)(const Protocol_Event_t *event, void *context);

/* UART记录：value为数据，aux为数据位数 */
typedef enum
{
    PROTOCOL_UART_RECORD_DATA = 0,
    PROTOCOL_UART_RECORD_BREAK,
    PROTOCOL_UART_RECORD_IDLE,
    PROTOCOL_UART_RECORD_ERROR
} Protocol_UART_Record_Type_t;

#define PROTOCOL_UART_FLAG_PARITY_ERROR (1U << 0)
#define PROTOCOL_UART_FLAG_FRAME_ERROR  (1U << 1)
#define PROTOCOL_UART_FLAG_INCOMPLETE   (1U << 2)
#define PROTOCOL_UART_FLAG_START_ERROR  (1U << 3)
#define PROTOCOL_UART_FLAG_TX           (1U << 4) /* 记录来自TX通道 */
#define PROTOCOL_UART_FLAG_RX           (1U << 5) /* 记录来自RX通道 */

/* IIC记录类型和标志 */
typedef enum
{
    PROTOCOL_IIC_RECORD_START = 0,
    PROTOCOL_IIC_RECORD_RESTART,
    PROTOCOL_IIC_RECORD_ADDRESS,
    PROTOCOL_IIC_RECORD_DATA,
    PROTOCOL_IIC_RECORD_STOP,
    PROTOCOL_IIC_RECORD_ERROR
} Protocol_IIC_Record_Type_t;

#define PROTOCOL_IIC_FLAG_ACK        (1U << 0)
#define PROTOCOL_IIC_FLAG_NACK       (1U << 1)
#define PROTOCOL_IIC_FLAG_READ       (1U << 2)
#define PROTOCOL_IIC_FLAG_WRITE      (1U << 3)
#define PROTOCOL_IIC_FLAG_INCOMPLETE (1U << 4)
#define PROTOCOL_IIC_FLAG_BUS_ERROR  (1U << 5)

/* SPI记录类型和标志 */
typedef enum
{
    PROTOCOL_SPI_RECORD_CS_ASSERT = 0,
    PROTOCOL_SPI_RECORD_WORD,
    PROTOCOL_SPI_RECORD_CS_DEASSERT,
    PROTOCOL_SPI_RECORD_ERROR
} Protocol_SPI_Record_Type_t;

#define PROTOCOL_SPI_FLAG_INCOMPLETE  (1U << 0)
#define PROTOCOL_SPI_FLAG_CS_INACTIVE (1U << 1)
#define PROTOCOL_SPI_FLAG_CLOCK_ERROR (1U << 2)

/* ================================================== */

/**
 * @brief  初始化协议结果池、状态锁及后台解析任务
 * @retval 0 初始化成功或此前已完成初始化
 * @note   由启动代码串行调用，LA 服务与 SDRAM 必须先就绪
 */
uint8_t Protocol_Service_Init(void);

/**
 * @brief  设置解析进度和结束通知的回调
 * @note   协议任务会在解锁后调用；event 是这次通知的状态副本
 */
void Protocol_Service_Set_Event_Callback(Protocol_Event_Callback_t callback,
                                         void *context);

/**
 * @brief  保留已结束的采集数据，并让后台按指定协议解析
 * @retval PROTOCOL_OK 请求被接受
 * @note   只能在任务中调用；只解析 DONE/ERROR 数据，结束时间必须早于 cutoff_tick
 */
Protocol_Result_t Protocol_Service_Start(const Protocol_Config_t *config);

/**
 * @brief  请求后台取消还未结束的协议解析
 * @retval PROTOCOL_OK 已设置取消请求，后台负责关闭解码器并释放 LA 资源
 * @note   请求成功后，仍要等到状态变为 CANCELLED，采集数据才会释放
 */
Protocol_Result_t Protocol_Service_Cancel(void);

/**
 * @brief  清空当前解析结果并恢复 IDLE
 * @retval PROTOCOL_OK 结果、丢弃和错误计数已清零
 * @note   任务上下文调用，不改变 LA 采集数据，也不递减 session_id
 */
Protocol_Result_t Protocol_Service_Reset(void);

/**
 * @brief  在状态锁内复制当前会话信息
 * @param  info 非 NULL 的输出，由调用者拥有
 * @retval PROTOCOL_OK 已复制当前解析状态
 * @note   任务上下文调用，返回后可能发生发布、Reset 或新 Start
 */
Protocol_Result_t Protocol_Service_Get_Info(Protocol_Info_t *info);

/**
 * @brief  复制调用时当前会话的已发布记录
 * @param  records 调用者独立缓冲，至少容纳 max_count 条记录
 * @retval PROTOCOL_OK 复制成功，按实际读取数量移动下标
 * @note   任务上下文调用，状态锁只保护本次复制，不保留当前会话
 */
Protocol_Result_t Protocol_Service_Get_Records(uint32_t start_index,
                                                Protocol_Record_t *records,
                                                uint16_t max_count,
                                                uint16_t *record_count);

/**
 * @brief  只在指定会话仍为当前会话时复制已发布记录
 * @param  records 调用者独立输出缓冲，至少容纳 max_count 条记录
 * @retval PROTOCOL_OK 会话匹配且复制完成
 * @note   任务上下文调用，会话核对和复制在同一状态锁内完成
 */
Protocol_Result_t Protocol_Service_Get_Records_For_Session(
                                                uint32_t expected_session_id,
                                                uint32_t start_index,
                                                Protocol_Record_t *records,
                                                uint16_t max_count,
                                                uint16_t *record_count);

/**
 * @brief  查找指定会话中首个可能与目标 CNT 相交的记录下标
 * @param  record_index 非 NULL 的结果输出，允许等于已发布记录总数
 * @retval PROTOCOL_OK 会话匹配，查找完成
 * @note   任务上下文调用，在结果锁内按 start_tick 二分，再向前包含仍跨越目标的相邻记录
 */
Protocol_Result_t Protocol_Service_Find_First_Record_For_Session(uint32_t expected_session_id,
                                                                 uint32_t target_tick,
                                                                 uint32_t *record_index);

/* ================================================== */

#ifdef __cplusplus
}
#endif

#endif /* PROTOCOL_SERVICE_H */
