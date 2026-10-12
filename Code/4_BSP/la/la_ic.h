/**
 * @file   la_ic.h
 * @brief  逻辑分析仪采集驱动头文件
 */

#ifndef LA_IC_H
#define LA_IC_H

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================== */

#include "stdint.h"

/* ================================================== */

#define BSP_LA_IC_CHANNEL_COUNT    6U    /* 输入捕获通道数量 */
#define BSP_LA_IC_DMA_BUFFER_COUNT 1024U /* 每路DMA循环缓冲区记录数，必须为偶数 */

/* ================================================== */

typedef enum
{
    BSP_LA_IC_CHANNEL_TIM2_CH1 = 0,
    BSP_LA_IC_CHANNEL_TIM2_CH3,
    BSP_LA_IC_CHANNEL_TIM2_CH4,
    BSP_LA_IC_CHANNEL_TIM5_CH1,
    BSP_LA_IC_CHANNEL_TIM5_CH3,
    BSP_LA_IC_CHANNEL_TIM5_CH4,
    BSP_LA_IC_CHANNEL_NONE
} BSP_LA_IC_Channel_t;

typedef enum
{
    BSP_LA_IC_EVENT_DMA_HALF = 0,
    BSP_LA_IC_EVENT_DMA_FULL,
    BSP_LA_IC_EVENT_DEADLINE,
    BSP_LA_IC_EVENT_DMA_ERROR
} BSP_LA_IC_Event_Type_t;

typedef enum
{
    BSP_LA_IC_OK = 0,
    BSP_LA_IC_ERROR_BUSY,
    BSP_LA_IC_ERROR_INVALID_PARAMETER,
    BSP_LA_IC_ERROR_DMA,
    BSP_LA_IC_ERROR_START_COLLISION,
    BSP_LA_IC_ERROR_STATE
} BSP_LA_IC_Result_t;

typedef struct
{
    BSP_LA_IC_Event_Type_t type; /* 事件类型 */
    BSP_LA_IC_Channel_t channel; /* 事件对应通道 */
    const uint32_t *data;        /* DMA半区首地址，无数据时为NULL */
    uint16_t count;              /* DMA半区记录数，无数据时为0 */
} BSP_LA_IC_Event_t;

typedef struct
{
    uint32_t timestamp_hz;  /* 实际时间戳频率 */
    uint32_t deadline_tick; /* 采集截止计数值 */
    uint8_t initial_levels; /* 六路初始电平，bit0..5对应通道枚举0..5 */
} BSP_LA_IC_Start_Result_t;

typedef struct
{
    const uint32_t *buffer[BSP_LA_IC_CHANNEL_COUNT]; /* 六路DMA循环缓冲区首地址 */
    /* 停止时六路DMA写入位置，等于缓冲区容量表示末尾刚写满 */
    uint16_t write_index [BSP_LA_IC_CHANNEL_COUNT];
    uint32_t cutoff_tick;          /* 统一采集截止计数值 */
    uint32_t overcapture_mask;     /* TIM捕获溢出通道位图 */
    uint32_t overcapture_count[BSP_LA_IC_CHANNEL_COUNT]; /* 各通道已观察并清除的CCxOF次数 */
    uint32_t completed_half_mask; /* 停稳时未被回调消费的HT/TC，bit(channel*2+half) */
} BSP_LA_IC_Final_Result_t;

typedef struct
{
    const uint32_t *buffer; /* 通道DMA循环缓冲区首地址 */
    uint16_t write_index;   /* 读取时 DMA 的写入位置；等于容量表示缓冲区刚写满 */
} BSP_LA_IC_Live_Result_t;

/* event指针只在回调执行期间有效，回调在DMA或TIM中断上下文执行 */
typedef void (*BSP_LA_IC_Event_Callback_t)(const BSP_LA_IC_Event_t *event, void *context);

/* ================================================== */

/**
 * @brief  准备全部捕获硬件与状态；任何初始化或 DMA 停稳失败都会上报错误
 */
BSP_LA_IC_Result_t BSP_LA_IC_Init(void);

/**
 * @brief  注册六路 DMA 半区及采集截止事件的统一回调
 * @param  callback 在 DMA/TIM 中断上下文执行的函数
 * @note   注册过程保存并恢复 PRIMASK
 */
void BSP_LA_IC_Set_Event_Callback(BSP_LA_IC_Event_Callback_t callback, void *context);

/**
 * @brief  以 TIM1 更新触发同时启动 TIM2/TIM5 和六路输入捕获 DMA
 * @param  input_filter 六路统一的 ICxF 硬件档位 0..15
 * @param  result 返回实际频率、统一截止计数和六路初始电平
 * @note   由采集服务任务串行调用
 */
BSP_LA_IC_Result_t BSP_LA_IC_Start(uint32_t timestamp_hz,
                                   uint32_t duration_us,
                                   uint8_t input_filter,
                                   BSP_LA_IC_Start_Result_t *result);

/**
 * @brief  快速关闭捕获事件源并锁存统一截止计数，不等待在途 DMA 请求结束
 * @param  cutoff_tick 返回停止时的 TIM2 计数
 * @note   可以从任务或中断调用；成功返回不代表 DMA 缓冲已可安全复用
 */
BSP_LA_IC_Result_t BSP_LA_IC_Stop(uint32_t *cutoff_tick);

/**
 * @brief  读取一路正在采集的 DMA 缓冲地址和当前写入位置
 * @param  channel 通道枚举 0..5
 * @param  result 调用者提供的输出结构，不能为 NULL
 * @note   只读取一次 NDTR，DMA 仍会继续写；不同通道的结果可能来自不同时间
 */
BSP_LA_IC_Result_t BSP_LA_IC_Get_Live(BSP_LA_IC_Channel_t channel,
                                      BSP_LA_IC_Live_Result_t *result);

/**
 * @brief  取得运行中的 TIM2 CNT，或停机后的锁存截止计数
 * @param  current_tick 输出计数，不能为 NULL
 */
BSP_LA_IC_Result_t BSP_LA_IC_Get_Current_Tick(uint32_t *current_tick);

/**
 * @brief  停稳六路 DMA 并汇总尾部位置、未消费半区和捕获溢出信息
 * @param  result 输出停止时的 DMA 状态，不能为 NULL
 * @note   只在任务上下文、HAL 时基可运行时调用，不能在事件回调中等待 DMA
 */
BSP_LA_IC_Result_t BSP_LA_IC_Finalize(BSP_LA_IC_Final_Result_t *result);

/**
 * @brief  关闭事件源，停止 DMA，并恢复下一次启动所需的空闲状态
 */
BSP_LA_IC_Result_t BSP_LA_IC_Reset(void);

/**
 * @brief  校验待提交的 DMA 半区尚未被循环写入复用
 * @param  half 0 表示前半区，1 表示后半区
 */
BSP_LA_IC_Result_t BSP_LA_IC_Validate_DMA_Half(
        BSP_LA_IC_Channel_t channel, uint8_t half);

/* ================================================== */

#ifdef __cplusplus
}
#endif

#endif /* LA_IC_H */
