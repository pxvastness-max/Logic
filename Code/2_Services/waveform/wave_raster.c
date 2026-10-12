/**
 * @file   wave_raster.c
 * @brief  RGB565 波形与协议结果绘制
 */

/* ================================================== */

#include "waveform/wave_raster.h"
#include <string.h>

/* ========================= 配置 ========================= */

#define WAVE_PROTOCOL_BLOCK_GAP 2U

/* ================================================== */

static uint16_t Wave_Color_To_RGB565(uint32_t color);
static void Wave_Draw_Protocol_Text(uint16_t *pixels,
                                    uint32_t stride,
                                    int32_t x,
                                    int32_t y,
                                    int32_t width,
                                    const char *text,
                                    uint8_t length,
                                    uint16_t color);
static void Wave_Draw_Value_Text(uint16_t *pixels,
                                 uint32_t stride,
                                 int32_t x,
                                 int32_t y,
                                 int32_t width,
                                 uint32_t value,
                                 uint8_t digits,
                                 char prefix,
                                 Wave_Protocol_Display_Format_t format,
                                 uint16_t color);
static void Wave_Draw_Marker_Text(uint16_t *pixels,
                                  uint32_t stride,
                                  int32_t x,
                                  int32_t y,
                                  int32_t width,
                                  const char *text,
                                  char fallback,
                                  uint16_t color);
static void Wave_Render_Protocol_Block(uint16_t *pixels,
                                       uint32_t width,
                                       uint32_t height,
                                       uint32_t stride,
                                       const Wave_Frame_Snapshot_t *snapshot,
                                       uint8_t row_index,
                                       uint32_t start_cnt,
                                       uint32_t end_cnt,
                                       uint16_t block_color,
                                       uint32_t value,
                                       uint8_t digits,
                                       char prefix,
                                       Wave_Protocol_Display_Format_t format,
                                       const char *marker,
                                       char marker_fallback,
                                       uint16_t text_color);

/* ================================================== */

/**
 * @brief  把 24 位 RGB 颜色截取为 RGB565 像素
 */
static uint16_t Wave_Color_To_RGB565(uint32_t color)
{
    return (uint16_t)(((color & 0x00F80000UL) >> 8U) | ((color & 0x0000FC00UL) >> 5U) |
                      ((color & 0x000000F8UL) >> 3U));
}

/* ================================================== */

void Wave_Raster_Render_Channel_Tile(uint16_t *tile_pixels,
                                     uint32_t tile_y,
                                     uint32_t tile_height,
                                     uint32_t tile_width,
                                     const Wave_Frame_Channel_t *channel,
                                     const uint16_t *columns,
                                     uint32_t valid_columns,
                                     uint8_t initial_levels)
{
    const int32_t high_y = channel->high_y - WAVE_TIME_RULER_HEIGHT;
    const int32_t low_y = channel->low_y - WAVE_TIME_RULER_HEIGHT;
    const int32_t tile_bottom = (int32_t)(tile_y + tile_height - 1U);
    const uint16_t color = Wave_Color_To_RGB565(channel->color);
    const uint32_t bit_shift = (uint32_t)channel->source_id * 2U;
    bool level = ((initial_levels >> channel->source_id) & 0x01U) != 0U;
    uint32_t x;

    for (x = 0U; x < valid_columns; x++)
    {
        const uint8_t code = (uint8_t)((columns[x] >> bit_shift) & 0x03U);
        const int32_t level_y = level ? high_y : low_y;

        if (level_y >= (int32_t)tile_y && level_y <= tile_bottom)
        {
            tile_pixels[(uint32_t)(level_y - (int32_t)tile_y) * tile_width + x] = color;
        }
        if (code == 0x01U || code == 0x02U)
        {
            const bool after = code == 0x02U;
            int32_t y1;
            int32_t y2;
            int32_t y;

            if (after == level)
            {
                y1 = high_y < low_y ? high_y : low_y;
                y2 = high_y > low_y ? high_y : low_y;
            }
            else
            {
                const int32_t after_y = after ? high_y : low_y;
                y1 = level_y < after_y ? level_y : after_y;
                y2 = level_y > after_y ? level_y : after_y;
            }
            if (y1 < (int32_t)tile_y)
            {
                y1 = (int32_t)tile_y;
            }
            if (y2 > tile_bottom)
            {
                y2 = tile_bottom;
            }
            for (y = y1; y <= y2; y++)
            {
                tile_pixels[(uint32_t)(y - (int32_t)tile_y) * tile_width + x] = color;
            }
            level = after;
        }
    }
}

/* ================================================== */

/* ASCII 0x20~0x7E，列内低位对应顶部像素 */
static const uint8_t wave_ascii_glyphs[95][5] = {
    {0x00U, 0x00U, 0x00U, 0x00U, 0x00U}, /* 0x20 空格 */
    {0x00U, 0x00U, 0x5FU, 0x00U, 0x00U}, /* 0x21 */
    {0x00U, 0x07U, 0x00U, 0x07U, 0x00U}, /* 0x22 */
    {0x14U, 0x7FU, 0x14U, 0x7FU, 0x14U}, /* 0x23 */
    {0x24U, 0x2AU, 0x7FU, 0x2AU, 0x12U}, /* 0x24 */
    {0x23U, 0x13U, 0x08U, 0x64U, 0x62U}, /* 0x25 */
    {0x36U, 0x49U, 0x55U, 0x22U, 0x50U}, /* 0x26 */
    {0x00U, 0x05U, 0x03U, 0x00U, 0x00U}, /* 0x27 */
    {0x00U, 0x1CU, 0x22U, 0x41U, 0x00U}, /* 0x28 */
    {0x00U, 0x41U, 0x22U, 0x1CU, 0x00U}, /* 0x29 */
    {0x14U, 0x08U, 0x3EU, 0x08U, 0x14U}, /* 0x2A */
    {0x08U, 0x08U, 0x3EU, 0x08U, 0x08U}, /* 0x2B */
    {0x00U, 0x50U, 0x30U, 0x00U, 0x00U}, /* 0x2C */
    {0x08U, 0x08U, 0x08U, 0x08U, 0x08U}, /* 0x2D */
    {0x00U, 0x60U, 0x60U, 0x00U, 0x00U}, /* 0x2E */
    {0x20U, 0x10U, 0x08U, 0x04U, 0x02U}, /* 0x2F */
    {0x3EU, 0x51U, 0x49U, 0x45U, 0x3EU}, /* 0x30 */
    {0x00U, 0x42U, 0x7FU, 0x40U, 0x00U}, /* 0x31 */
    {0x42U, 0x61U, 0x51U, 0x49U, 0x46U}, /* 0x32 */
    {0x21U, 0x41U, 0x45U, 0x4BU, 0x31U}, /* 0x33 */
    {0x18U, 0x14U, 0x12U, 0x7FU, 0x10U}, /* 0x34 */
    {0x27U, 0x45U, 0x45U, 0x45U, 0x39U}, /* 0x35 */
    {0x3CU, 0x4AU, 0x49U, 0x49U, 0x30U}, /* 0x36 */
    {0x01U, 0x71U, 0x09U, 0x05U, 0x03U}, /* 0x37 */
    {0x36U, 0x49U, 0x49U, 0x49U, 0x36U}, /* 0x38 */
    {0x06U, 0x49U, 0x49U, 0x29U, 0x1EU}, /* 0x39 */
    {0x00U, 0x36U, 0x36U, 0x00U, 0x00U}, /* 0x3A */
    {0x00U, 0x56U, 0x36U, 0x00U, 0x00U}, /* 0x3B */
    {0x08U, 0x14U, 0x22U, 0x41U, 0x00U}, /* 0x3C */
    {0x14U, 0x14U, 0x14U, 0x14U, 0x14U}, /* 0x3D */
    {0x00U, 0x41U, 0x22U, 0x14U, 0x08U}, /* 0x3E */
    {0x02U, 0x01U, 0x51U, 0x09U, 0x06U}, /* 0x3F */
    {0x32U, 0x49U, 0x79U, 0x41U, 0x3EU}, /* 0x40 */
    {0x7EU, 0x11U, 0x11U, 0x11U, 0x7EU}, /* 0x41 */
    {0x7FU, 0x49U, 0x49U, 0x49U, 0x36U}, /* 0x42 */
    {0x3EU, 0x41U, 0x41U, 0x41U, 0x22U}, /* 0x43 */
    {0x7FU, 0x41U, 0x41U, 0x22U, 0x1CU}, /* 0x44 */
    {0x7FU, 0x49U, 0x49U, 0x49U, 0x41U}, /* 0x45 */
    {0x7FU, 0x09U, 0x09U, 0x09U, 0x01U}, /* 0x46 */
    {0x3EU, 0x41U, 0x49U, 0x49U, 0x7AU}, /* 0x47 */
    {0x7FU, 0x08U, 0x08U, 0x08U, 0x7FU}, /* 0x48 */
    {0x00U, 0x41U, 0x7FU, 0x41U, 0x00U}, /* 0x49 */
    {0x20U, 0x40U, 0x41U, 0x3FU, 0x01U}, /* 0x4A */
    {0x7FU, 0x08U, 0x14U, 0x22U, 0x41U}, /* 0x4B */
    {0x7FU, 0x40U, 0x40U, 0x40U, 0x40U}, /* 0x4C */
    {0x7FU, 0x02U, 0x0CU, 0x02U, 0x7FU}, /* 0x4D */
    {0x7FU, 0x04U, 0x08U, 0x10U, 0x7FU}, /* 0x4E */
    {0x3EU, 0x41U, 0x41U, 0x41U, 0x3EU}, /* 0x4F */
    {0x7FU, 0x09U, 0x09U, 0x09U, 0x06U}, /* 0x50 */
    {0x3EU, 0x41U, 0x51U, 0x21U, 0x5EU}, /* 0x51 */
    {0x7FU, 0x09U, 0x19U, 0x29U, 0x46U}, /* 0x52 */
    {0x46U, 0x49U, 0x49U, 0x49U, 0x31U}, /* 0x53 保留已有 START 标记字形 */
    {0x01U, 0x01U, 0x7FU, 0x01U, 0x01U}, /* 0x54 */
    {0x3FU, 0x40U, 0x40U, 0x40U, 0x3FU}, /* 0x55 */
    {0x1FU, 0x20U, 0x40U, 0x20U, 0x1FU}, /* 0x56 */
    {0x7FU, 0x20U, 0x18U, 0x20U, 0x7FU}, /* 0x57 */
    {0x63U, 0x14U, 0x08U, 0x14U, 0x63U}, /* 0x58 */
    {0x07U, 0x08U, 0x70U, 0x08U, 0x07U}, /* 0x59 */
    {0x61U, 0x51U, 0x49U, 0x45U, 0x43U}, /* 0x5A */
    {0x00U, 0x7FU, 0x41U, 0x41U, 0x00U}, /* 0x5B 左方括号 */
    {0x02U, 0x04U, 0x08U, 0x10U, 0x20U}, /* 0x5C */
    {0x00U, 0x41U, 0x41U, 0x7FU, 0x00U}, /* 0x5D 右方括号 */
    {0x04U, 0x02U, 0x01U, 0x02U, 0x04U}, /* 0x5E */
    {0x40U, 0x40U, 0x40U, 0x40U, 0x40U}, /* 0x5F */
    {0x00U, 0x01U, 0x02U, 0x04U, 0x00U}, /* 0x60 */
    {0x20U, 0x54U, 0x54U, 0x54U, 0x78U}, /* 0x61 */
    {0x7FU, 0x48U, 0x44U, 0x44U, 0x38U}, /* 0x62 */
    {0x38U, 0x54U, 0x54U, 0x54U, 0x20U}, /* 0x63 */
    {0x38U, 0x44U, 0x44U, 0x48U, 0x7FU}, /* 0x64 */
    {0x38U, 0x54U, 0x54U, 0x54U, 0x18U}, /* 0x65 */
    {0x08U, 0x7EU, 0x09U, 0x01U, 0x02U}, /* 0x66 */
    {0x0CU, 0x52U, 0x52U, 0x52U, 0x3EU}, /* 0x67 */
    {0x7FU, 0x08U, 0x04U, 0x04U, 0x78U}, /* 0x68 */
    {0x00U, 0x44U, 0x7DU, 0x40U, 0x00U}, /* 0x69 */
    {0x20U, 0x40U, 0x44U, 0x3DU, 0x00U}, /* 0x6A */
    {0x7FU, 0x10U, 0x28U, 0x44U, 0x00U}, /* 0x6B */
    {0x00U, 0x41U, 0x7FU, 0x40U, 0x00U}, /* 0x6C */
    {0x7CU, 0x04U, 0x18U, 0x04U, 0x78U}, /* 0x6D */
    {0x7CU, 0x08U, 0x04U, 0x04U, 0x78U}, /* 0x6E */
    {0x38U, 0x54U, 0x54U, 0x54U, 0x38U}, /* 0x6F */
    {0x7CU, 0x14U, 0x14U, 0x14U, 0x08U}, /* 0x70 */
    {0x08U, 0x14U, 0x14U, 0x18U, 0x7CU}, /* 0x71 */
    {0x7CU, 0x08U, 0x04U, 0x04U, 0x08U}, /* 0x72 */
    {0x48U, 0x54U, 0x54U, 0x54U, 0x20U}, /* 0x73 */
    {0x04U, 0x3FU, 0x44U, 0x40U, 0x20U}, /* 0x74 */
    {0x3CU, 0x40U, 0x40U, 0x20U, 0x7CU}, /* 0x75 */
    {0x1CU, 0x20U, 0x40U, 0x20U, 0x1CU}, /* 0x76 */
    {0x3CU, 0x40U, 0x30U, 0x40U, 0x3CU}, /* 0x77 */
    {0x44U, 0x28U, 0x10U, 0x28U, 0x44U}, /* 0x78 */
    {0x0CU, 0x50U, 0x50U, 0x50U, 0x3CU}, /* 0x79 */
    {0x44U, 0x64U, 0x54U, 0x4CU, 0x44U}, /* 0x7A */
    {0x00U, 0x08U, 0x36U, 0x41U, 0x00U}, /* 0x7B */
    {0x00U, 0x00U, 0x7FU, 0x00U, 0x00U}, /* 0x7C */
    {0x00U, 0x41U, 0x36U, 0x08U, 0x00U}, /* 0x7D */
    {0x08U, 0x04U, 0x08U, 0x10U, 0x08U}  /* 0x7E */
};

/* ================================================== */

uint8_t Wave_Raster_Format_Protocol_Value(uint32_t value,
                                          uint8_t digits,
                                          char prefix,
                                          Wave_Protocol_Display_Format_t format,
                                          char text[WAVE_PROTOCOL_VALUE_TEXT_CAPACITY])
{
    static const char hex_digits[] = "0123456789ABCDEF";
    const bool ascii = format == WAVE_PROTOCOL_DISPLAY_ASCII;
    uint8_t length = 0U;
    uint8_t digit;

    if (prefix != '\0')
    {
        text[length++] = prefix == 'R' ? 'R' : 'W';
    }
    if (ascii && value >= 0x20U && value <= 0x7EU)
    {
        text[length++] = (char)value;
        text[length] = '\0';
        return length;
    }

    if (digits == 0U)
    {
        digits = 1U;
    }
    if (digits > 8U)
    {
        digits = 8U;
    }
    if (ascii)
    {
        /* 先判断位数，避免右移 32 位并保留完整协议值 */
        while (digits < 8U && (value >> (digits * 4U)) != 0U)
        {
            digits++;
        }
        text[length++] = '[';
    }
    for (digit = 0U; digit < digits; digit++)
    {
        const uint8_t shift = (uint8_t)((digits - digit - 1U) * 4U);
        text[length++] = hex_digits[(value >> shift) & 0x0FU];
    }
    if (ascii)
    {
        text[length++] = ']';
    }
    text[length] = '\0';
    return length;
}

/* ================================================== */

/**
 * @brief  用固定 5×7 字体在已裁剪协议块中央绘制可打印 ASCII
 */
static void Wave_Draw_Protocol_Text(uint16_t *pixels,
                                    uint32_t stride,
                                    int32_t x,
                                    int32_t y,
                                    int32_t width,
                                    const char *text,
                                    uint8_t length,
                                    uint16_t color)
{
    const int32_t span = (int32_t)length * 6 - 1;
    const int32_t start_x = x + (width - span) / 2;
    const int32_t start_y = y + ((int32_t)WAVE_PROTOCOL_BLOCK_HEIGHT - 7) / 2;
    uint8_t character;

    if (length == 0U || width < span)
    {
        return;
    }
    for (character = 0U; character < length; character++)
    {
        const uint8_t code = (uint8_t)text[character];
        const uint8_t *glyph;
        uint8_t column;

        if (code < 0x20U || code > 0x7EU)
        {
            continue;
        }
        glyph = wave_ascii_glyphs[code - 0x20U];
        for (column = 0U; column < 5U; column++)
        {
            uint8_t row;
            for (row = 0U; row < 7U; row++)
            {
                if ((glyph[column] & (1U << row)) != 0U)
                {
                    pixels[(uint32_t)(start_y + row) * stride +
                           (uint32_t)(start_x + (int32_t)character * 6 + column)] = color;
                }
            }
        }
    }
}

/**
 * @brief  格式化协议数值并在色块中央绘制
 * @note   空间不足时省略前缀，不截断数值
 */
static void Wave_Draw_Value_Text(uint16_t *pixels,
                                 uint32_t stride,
                                 int32_t x,
                                 int32_t y,
                                 int32_t width,
                                 uint32_t value,
                                 uint8_t digits,
                                 char prefix,
                                 Wave_Protocol_Display_Format_t format,
                                 uint16_t color)
{
    char text[WAVE_PROTOCOL_VALUE_TEXT_CAPACITY];
    uint8_t length = Wave_Raster_Format_Protocol_Value(value, digits, prefix, format, text);

    if (prefix != '\0' && width < (int32_t)length * 6 - 1)
    {
        /* memmove 同时移动末尾 NUL，重叠区复制必须保持定义行为 */
        memmove(text, &text[1], length);
        length--;
    }
    Wave_Draw_Protocol_Text(pixels, stride, x, y, width, text, length, color);
}

/**
 * @brief  绘制 START 或 STOP 标记并在窄块内退化为单字母
 */
static void Wave_Draw_Marker_Text(uint16_t *pixels,
                                  uint32_t stride,
                                  int32_t x,
                                  int32_t y,
                                  int32_t width,
                                  const char *text,
                                  char fallback,
                                  uint16_t color)
{
    const char *draw_text = text;
    char fallback_text[2] = {fallback, '\0'};
    uint8_t length = (uint8_t)strlen(text);

    if (width < (int32_t)length * 6 - 1)
    {
        draw_text = fallback_text;
        length = 1U;
    }
    Wave_Draw_Protocol_Text(pixels, stride, x, y, width, draw_text, length, color);
}

/**
 * @brief  把单个协议时间段裁剪到当前窗口后绘制色块及可选文字
 */
static void Wave_Render_Protocol_Block(uint16_t *pixels,
                                       uint32_t width,
                                       uint32_t height,
                                       uint32_t stride,
                                       const Wave_Frame_Snapshot_t *snapshot,
                                       uint8_t row_index,
                                       uint32_t start_cnt,
                                       uint32_t end_cnt,
                                       uint16_t block_color,
                                       uint32_t value,
                                       uint8_t digits,
                                       char prefix,
                                       Wave_Protocol_Display_Format_t format,
                                       const char *marker,
                                       char marker_fallback,
                                       uint16_t text_color)
{
    uint32_t clipped_start;
    uint32_t clipped_end;
    uint32_t y;
    int32_t x1;
    int32_t x2;
    int32_t block_width;
    uint32_t line;

    if (row_index >= WAVE_PROTOCOL_RESULT_ROWS || end_cnt < snapshot->view.start_cnt ||
        start_cnt > snapshot->view.now_cnt || snapshot->view.window_size == 0U)
    {
        return;
    }

    clipped_start = start_cnt < snapshot->view.start_cnt ? snapshot->view.start_cnt : start_cnt;
    clipped_end = end_cnt > snapshot->view.now_cnt ? snapshot->view.now_cnt : end_cnt;
    x1 = (int32_t)(((uint64_t)(clipped_start - snapshot->view.start_cnt) * width) /
                   snapshot->view.window_size);
    x2 = (int32_t)(((uint64_t)(clipped_end - snapshot->view.start_cnt) * width) /
                   snapshot->view.window_size);
    if (x2 <= x1)
    {
        x2 = x1 + 2;
    }
    if (x2 > (int32_t)width)
    {
        x2 = (int32_t)width;
    }

    if ((x2 - x1) > (int32_t)(WAVE_PROTOCOL_BLOCK_GAP + 1U))
    {
        x1 += (int32_t)(WAVE_PROTOCOL_BLOCK_GAP / 2U);
        x2 -= (int32_t)(WAVE_PROTOCOL_BLOCK_GAP - WAVE_PROTOCOL_BLOCK_GAP / 2U);
    }
    block_width = x2 - x1;
    if (block_width <= 0)
    {
        return;
    }

    y = WAVE_PROTOCOL_FIRST_ROW_Y + (uint32_t)row_index * (height / WAVE_CHANNEL_COUNT);
    if (y + WAVE_PROTOCOL_BLOCK_HEIGHT > height)
    {
        return;
    }
    for (line = 0U; line < WAVE_PROTOCOL_BLOCK_HEIGHT; line++)
    {
        uint32_t column;
        uint16_t *destination = &pixels[(y + line) * stride + (uint32_t)x1];
        for (column = 0U; column < (uint32_t)block_width; column++)
        {
            destination[column] = block_color;
        }
    }
    if (marker != NULL)
    {
        Wave_Draw_Marker_Text(pixels,
                              stride,
                              x1,
                              (int32_t)y,
                              block_width,
                              marker,
                              marker_fallback,
                              text_color);
    }
    else if (digits != 0U)
    {
        Wave_Draw_Value_Text(pixels,
                             stride,
                             x1,
                             (int32_t)y,
                             block_width,
                             value,
                             digits,
                             prefix,
                             format,
                             text_color);
    }
}

/* ================================================== */

void Wave_Raster_Render_Protocol_Results(uint16_t *pixels,
                                         uint32_t width,
                                         uint32_t height,
                                         uint32_t stride,
                                         const Wave_Frame_Snapshot_t *snapshot,
                                         const Wave_Protocol_Result_t *records)
{
    static const uint32_t colors[WAVE_PROTOCOL_RESULT_ROWS] = {0xB9A7D8U, 0xB9A7D8U};
    const uint16_t start_color = Wave_Color_To_RGB565(0xE6D58AU);
    const uint16_t stop_color = Wave_Color_To_RGB565(0x91B7D7U);
    const uint16_t iic_address_color = Wave_Color_To_RGB565(0x9BCB6BU);
    const uint16_t iic_write_data_color = Wave_Color_To_RGB565(0x62BED0U);
    const uint16_t iic_read_data_color = Wave_Color_To_RGB565(0x68CFA0U);
    const uint16_t text_color = Wave_Color_To_RGB565(0x20242AU);
    uint32_t row;
    uint16_t index;

    for (row = 0U; row < WAVE_PROTOCOL_RESULT_ROWS; row++)
    {
        const uint32_t y = WAVE_PROTOCOL_FIRST_ROW_Y + row * (height / WAVE_CHANNEL_COUNT);
        uint32_t line;
        if (y + WAVE_PROTOCOL_BLOCK_HEIGHT > height)
        {
            continue;
        }
        for (line = 0U; line < WAVE_PROTOCOL_BLOCK_HEIGHT; line++)
        {
            memset(&pixels[(y + line) * stride], 0, (size_t)width * sizeof(pixels[0]));
        }
    }

    for (index = 0U; index < snapshot->protocol_result_count; index++)
    {
        const Wave_Protocol_Result_t *record = &records[index];
        if (record->row_index >= WAVE_PROTOCOL_RESULT_ROWS)
        {
            continue;
        }
        const uint16_t data_color = Wave_Color_To_RGB565(colors[record->row_index]);
        uint16_t block_color = data_color;

        if (record->iic_address)
        {
            block_color = iic_address_color;
        }
        else if (record->iic_data)
        {
            block_color = record->iic_read_data ? iic_read_data_color : iic_write_data_color;
        }

        if (record->uart_frame)
        {
            const uint32_t parity_bits = record->uart_parity_enabled ? 1U : 0U;
            const uint32_t total_bits =
                1U + record->uart_data_bits + parity_bits + record->uart_stop_bits;
            const uint64_t duration = (uint64_t)record->end_cnt - record->start_cnt;
            const uint32_t start_end = record->start_cnt + (uint32_t)(duration / total_bits);
            const uint32_t stop_start =
                record->start_cnt +
                (uint32_t)((duration * (1U + record->uart_data_bits + parity_bits)) / total_bits);

            Wave_Render_Protocol_Block(pixels,
                                       width,
                                       height,
                                       stride,
                                       snapshot,
                                       record->row_index,
                                       record->start_cnt,
                                       start_end,
                                       start_color,
                                       0U,
                                       0U,
                                       '\0',
                                       snapshot->protocol_display_format,
                                       "START",
                                       'S',
                                       text_color);
            Wave_Render_Protocol_Block(pixels,
                                       width,
                                       height,
                                       stride,
                                       snapshot,
                                       record->row_index,
                                       start_end,
                                       stop_start,
                                       data_color,
                                       record->value,
                                       record->digits,
                                       '\0',
                                       snapshot->protocol_display_format,
                                       NULL,
                                       '\0',
                                       text_color);
            Wave_Render_Protocol_Block(pixels,
                                       width,
                                       height,
                                       stride,
                                       snapshot,
                                       record->row_index,
                                       stop_start,
                                       record->end_cnt,
                                       stop_color,
                                       0U,
                                       0U,
                                       '\0',
                                       snapshot->protocol_display_format,
                                       "STOP",
                                       'T',
                                       text_color);
        }
        else
        {
            /* 地址属于总线寻址字段；寄存器地址等设备约定仍作为普通数据字节显示 */
            const Wave_Protocol_Display_Format_t format =
                record->iic_address ? WAVE_PROTOCOL_DISPLAY_HEX : snapshot->protocol_display_format;

            Wave_Render_Protocol_Block(pixels,
                                       width,
                                       height,
                                       stride,
                                       snapshot,
                                       record->row_index,
                                       record->start_cnt,
                                       record->end_cnt,
                                       block_color,
                                       record->value,
                                       record->compact_block ? 0U : record->digits,
                                       record->prefix,
                                       format,
                                       NULL,
                                       '\0',
                                       text_color);
        }
    }
}
