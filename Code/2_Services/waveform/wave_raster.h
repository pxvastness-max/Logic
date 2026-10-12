/**
 * @file   wave_raster.h
 * @brief  RGB565 波形与协议结果绘制
 */

#ifndef WAVE_RASTER_H
#define WAVE_RASTER_H

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================== */

#include "waveform/wave_service.h"

/* ================================================== */

/* 两行协议结果的固定布局，左栏复用中心位置 */
#define WAVE_PROTOCOL_BLOCK_HEIGHT 22U
#define WAVE_PROTOCOL_FIRST_ROW_Y  18U

#define WAVE_PROTOCOL_RESULT_ROWS         2U
#define WAVE_PROTOCOL_RESULT_LIMIT        512U
#define WAVE_PROTOCOL_VALUE_TEXT_CAPACITY 12U /* 方向前缀、括号、8 位 HEX 和 NUL */

/* ================================================== */

typedef struct
{
    uint32_t start_cnt;   /* 当前可见时间窗口左边界的相对 CNT */
    uint32_t now_cnt;     /* 当前可见时间窗口右边界的相对 CNT */
    uint32_t window_size; /* 时间窗口宽度，单位为 CNT */
} Wave_View_t;

typedef struct
{
    uint32_t start_cnt;       /* 本条协议记录起点，相对于采集起点的 CNT */
    uint32_t end_cnt;         /* 本条协议记录终点，必须不小于起点 */
    uint32_t value;           /* 完整协议数值，ASCII 判断不能只取低八位 */
    uint8_t row_index;        /* 顶部结果行编号，合法值为 0 或 1 */
    uint8_t digits;           /* 数值十六进制位数，普通记录为 1~8 */
    char prefix;              /* I2C方向提示：W或R，无前缀时为0 */
    uint8_t uart_data_bits;   /* UART 数据位数，仅 UART 帧使用，范围为 5~9 */
    uint8_t uart_stop_bits;   /* UART 停止位数，仅 UART 帧使用，取 1 或 2 */
    bool iic_address;         /* I2C设备地址固定 HEX，并使用区别于数据的颜色 */
    bool iic_data;            /* I2C写数据与读回数据分别着色 */
    bool iic_read_data;       /* true表示从机返回的数据 */
    bool uart_frame;          /* true 时按 START、数据和 STOP 三段绘制 */
    bool uart_parity_enabled; /* UART 数据段是否额外包含一个校验位 */
    bool compact_block;       /* 密集模式色块，不绘制数值文字 */
} Wave_Protocol_Result_t;

typedef struct
{
    bool enabled;      /* 是否在本帧绘制此轨道，不改变 LA 六路采集选择 */
    uint8_t source_id; /* 六路打包列状态中的物理通道编号，范围为 0~5 */
    uint32_t color;    /* 轨道颜色，编码为 0xRRGGBB */
    int16_t high_y;    /* 高电平绝对屏幕 Y 坐标，包含顶部时间标尺 */
    int16_t low_y;     /* 低电平绝对屏幕 Y 坐标，包含顶部时间标尺 */
} Wave_Frame_Channel_t;

typedef struct
{
    Wave_View_t view;                                       /* 本帧一致的相对 CNT 时间窗口 */
    Wave_Frame_Channel_t channels[WAVE_CHANNEL_COUNT];      /* 这一帧使用的六路通道数据 */
    bool protocol_layout_active;                            /* 是否保留顶部两行作为协议结果区 */
    Wave_Protocol_Display_Format_t protocol_display_format; /* 本帧固定的数值文字格式 */
    uint16_t protocol_result_count; /* 独立记录数组中有效项数，范围为 0~512 */
    bool clear_background;          /* 清空后缓冲，擦除旧布局 */
} Wave_Frame_Snapshot_t;

/* ================================================== */

/**
 * @brief  将完整协议数值转换为独立的短文本
 * @note   输出容量至少为 WAVE_PROTOCOL_VALUE_TEXT_CAPACITY
 */
uint8_t Wave_Raster_Format_Protocol_Value(uint32_t value,
                                          uint8_t digits,
                                          char prefix,
                                          Wave_Protocol_Display_Format_t format,
                                          char text[WAVE_PROTOCOL_VALUE_TEXT_CAPACITY]);

/**
 * @brief  把一路数字列状态同步绘制到调用者的 RGB565 横向条带
 * @param  tile_y : 波形层相对 Y，不含标尺
 * @param  tile_width : RGB565 行步长，至少 valid_columns
 * @note   调用者保证容量和通道有效，并先清空条带背景
 */
void Wave_Raster_Render_Channel_Tile(uint16_t *tile_pixels,
                                     uint32_t tile_y,
                                     uint32_t tile_height,
                                     uint32_t tile_width,
                                     const Wave_Frame_Channel_t *channel,
                                     const uint16_t *columns,
                                     uint32_t valid_columns,
                                     uint8_t initial_levels);

/**
 * @brief  按不可变时间窗口同步重绘两行协议结果
 * @param  pixels : 容量至少 stride × height
 * @param  records : 数量由 snapshot 指定，上限 512
 * @note   调用者保证缓冲容量和记录范围有效
 */
void Wave_Raster_Render_Protocol_Results(uint16_t *pixels,
                                         uint32_t width,
                                         uint32_t height,
                                         uint32_t stride,
                                         const Wave_Frame_Snapshot_t *snapshot,
                                         const Wave_Protocol_Result_t *records);

/* ================================================== */

#ifdef __cplusplus
}
#endif

#endif /* WAVE_RASTER_H */
