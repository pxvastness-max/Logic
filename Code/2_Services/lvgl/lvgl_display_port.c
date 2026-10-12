/**
 * @file   lvgl_display_port.c
 * @brief  LVGL 显示适配
 */

/* ================================================== */

#include "lvgl/lvgl_display_port.h"
#include "display/lcd.h"
#include "diagnostics/display_diagnostics.h"

/* ========================= 配置 ========================= */

#define LVGL_DRAW_BUFFER_LINES 40U

/* ================================================== */

static lv_display_t *display_handle;
static bool display_flush_enabled = true;

LV_ATTRIBUTE_MEM_ALIGN
static uint16_t display_draw_buffer[LCD_FRAMEBUFFER_MAX_WIDTH * LVGL_DRAW_BUFFER_LINES]
    __attribute__((aligned(4)));

/* ================================================== */

static void LVGL_Display_Port_Flush(lv_display_t *display, const lv_area_t *area, uint8_t *pixels);

/* ================================================== */

/**
 * @brief  同步复制一次 LVGL 局部渲染结果并释放绘制缓冲
 */
static void LVGL_Display_Port_Flush(lv_display_t *display, const lv_area_t *area, uint8_t *pixels)
{
    if (display_flush_enabled && (area->x1 >= 0) && (area->y1 >= 0))
    {
        uint32_t pixel_count =
            (uint32_t)(area->x2 - area->x1 + 1) * (uint32_t)(area->y2 - area->y1 + 1);

        LCD_Write_Area((uint16_t)area->x1,
                       (uint16_t)area->y1,
                       (uint16_t)area->x2,
                       (uint16_t)area->y2,
                       (const uint16_t *)pixels);
        Display_Diagnostics_Record_Flush(pixel_count);
    }
    lv_display_flush_ready(display);
}

/* ================================================== */

bool LVGL_Display_Port_Init(void)
{
    uint16_t width;
    uint16_t height;
    uint32_t buffer_bytes;

    if (display_handle != NULL)
    {
        return true;
    }
    if (!LCD_Is_Ready())
    {
        return false;
    }
    width = LCD_Get_Width();
    height = LCD_Get_Height();
    if ((width == 0U) || (height == 0U) || (width > LCD_FRAMEBUFFER_MAX_WIDTH))
    {
        return false;
    }
    display_handle = lv_display_create(width, height);
    if (display_handle == NULL)
    {
        return false;
    }
    lv_obj_set_size(lv_display_get_screen_active(display_handle), width, height);
    lv_display_set_color_format(display_handle, LV_COLOR_FORMAT_RGB565);
    lv_display_set_flush_cb(display_handle, LVGL_Display_Port_Flush);

    /* 40 行单缓冲限制片内 RAM 占用；只有实际屏宽参与 LVGL 行布局 */
    buffer_bytes = (uint32_t)width * LVGL_DRAW_BUFFER_LINES * sizeof(uint16_t);
    lv_display_set_buffers(display_handle,
                           display_draw_buffer,
                           NULL,
                           buffer_bytes,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    return true;
}

void LVGL_Display_Port_Enable_Update(void)
{
    display_flush_enabled = true;
}

void LVGL_Display_Port_Disable_Update(void)
{
    display_flush_enabled = false;
}
