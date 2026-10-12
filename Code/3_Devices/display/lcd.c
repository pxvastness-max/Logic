/**
 * @file   lcd.c
 * @brief  正点原子RGB LCD面板设备层实现
 */

/* ================================================== */

#include "display/lcd.h"
#include "display/bsp_ltdc_lcd.h"
#include <string.h>

/* ========================= 配置 ========================= */

#if LCD_FRAMEBUFFER_MAX_WIDTH != BSP_LTDC_FRAMEBUFFER_MAX_WIDTH
    #error "Device and BSP framebuffer capacities must match"
#endif

/* ================================================== */

/* 一种LCD面板的识别值和LTDC时序要求 */
typedef struct
{
    uint16_t id;
    BSP_LTDC_Config_t timing;
} LCD_Panel_t;

/* ================================================== */

/* 工程支持的LCD面板参数表 */
static const LCD_Panel_t lcd_panels[] = {
    {0x4342U, {480U, 272U, 1U, 40U, 5U, 1U, 8U, 8U, 33U, LTDC_PCPOLARITY_IPC}},
    {0x7084U, {800U, 480U, 1U, 46U, 210U, 1U, 23U, 22U, 9U, LTDC_PCPOLARITY_IPC}},
    {0x7016U, {1024U, 600U, 20U, 140U, 160U, 3U, 20U, 12U, 6U, LTDC_PCPOLARITY_IPC}},
    {0x4384U, {800U, 480U, 48U, 88U, 40U, 3U, 32U, 13U, 9U, LTDC_PCPOLARITY_IPC}},
    {0x1018U, {1280U, 800U, 10U, 140U, 10U, 3U, 10U, 10U, 6U, LTDC_PCPOLARITY_IIPC}}};

static const LCD_Panel_t *lcd_panel; /* 当前识别到的面板参数 */
static bool lcd_ready;               /* LCD设备是否初始化成功 */

/* ================================================== */

/**
 * @brief  将三位板级面板识别值转换为设备编号
 */
static uint16_t LCD_Decode_Panel_ID(uint8_t id_bits)
{
    switch (id_bits)
    {
        case 0U:
            return 0x4342U;
        case 1U:
            return 0x7084U;
        case 2U:
            return 0x7016U;
        case 3U:
            return 0x7018U;
        case 4U:
            return 0x4384U;
        case 5U:
            return 0x1018U;
        default:
            return 0U;
    }
}

/**
 * @brief  在支持列表中查找面板时序
 */
static const LCD_Panel_t *LCD_Find_Panel(uint16_t panel_id)
{
    uint32_t i;
    for (i = 0U; i < (sizeof(lcd_panels) / sizeof(lcd_panels[0])); ++i)
    {
        if (lcd_panels[i].id == panel_id)
        {
            return &lcd_panels[i];
        }
    }
    return NULL;
}

/**
 * @brief  识别 RGB 面板并按设备参数配置 LTDC 显示层
 */
LCD_Status_t LCD_Init(void)
{
    BSP_LTDC_Status_t bsp_status;
    BSP_LTDC_Config_t timing;
    uint16_t panel_id;

    lcd_ready = false;
    lcd_panel = NULL;
    panel_id = LCD_Get_Panel_ID();
    if (panel_id == 0U)
    {
        return LCD_STATUS_NO_PANEL;
    }
    lcd_panel = LCD_Find_Panel(panel_id);
    if (lcd_panel == NULL)
    {
        return LCD_STATUS_UNSUPPORTED_PANEL;
    }

    timing = lcd_panel->timing;

    bsp_status = BSP_LTDC_LCD_Init(&timing);
    if (bsp_status == BSP_LTDC_STATUS_CLOCK_ERROR)
    {
        return LCD_STATUS_CLOCK_ERROR;
    }
    if (bsp_status == BSP_LTDC_STATUS_LAYER_ERROR)
    {
        return LCD_STATUS_LAYER_ERROR;
    }
    if (bsp_status != BSP_LTDC_STATUS_OK)
    {
        return LCD_STATUS_INIT_ERROR;
    }

    lcd_ready = true;
    LCD_Clear(0x0000U);
    return LCD_STATUS_OK;
}

/**
 * @brief  查询 LCD 设备状态与 BSP 显示硬件状态是否均就绪
 */
bool LCD_Is_Ready(void)
{
    return lcd_ready && BSP_LTDC_LCD_Is_Ready();
}

/**
 * @brief  读取板级识别引脚并转换为面板编号
 */
uint16_t LCD_Get_Panel_ID(void)
{
    return LCD_Decode_Panel_ID(BSP_LTDC_LCD_Read_ID_Bits());
}

/**
 * @brief  返回最近一次识别成功的面板水平分辨率
 */
uint16_t LCD_Get_Width(void)
{
    return (lcd_panel == NULL) ? 0U : lcd_panel->timing.width;
}

/**
 * @brief  返回最近一次识别成功的面板垂直分辨率
 */
uint16_t LCD_Get_Height(void)
{
    return (lcd_panel == NULL) ? 0U : lcd_panel->timing.height;
}

/**
 * @brief  向 LCD 帧缓冲写入一个 RGB565 像素
 */
void LCD_Draw_Pixel(uint16_t x, uint16_t y, uint16_t color)
{
    uint16_t *framebuffer;
    if (!LCD_Is_Ready() || x >= LCD_Get_Width() || y >= LCD_Get_Height())
    {
        return;
    }
    framebuffer = BSP_LTDC_LCD_Get_Framebuffer();
    framebuffer[(uint32_t)y * LCD_Get_Width() + x] = color;
}

/**
 * @brief  用相同 RGB565 颜色填充矩形并裁剪至面板边界
 */
void LCD_Fill_Rect(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint16_t color)
{
    uint16_t *framebuffer;
    uint32_t row;
    uint32_t col;
    uint32_t x_end;
    uint32_t y_end;

    if (!LCD_Is_Ready() || x >= LCD_Get_Width() || y >= LCD_Get_Height() || width == 0U ||
        height == 0U)
    {
        return;
    }
    x_end = (uint32_t)x + width;
    y_end = (uint32_t)y + height;
    if (x_end > LCD_Get_Width())
    {
        x_end = LCD_Get_Width();
    }
    if (y_end > LCD_Get_Height())
    {
        y_end = LCD_Get_Height();
    }

    framebuffer = BSP_LTDC_LCD_Get_Framebuffer();
    for (row = y; row < y_end; ++row)
    {
        uint16_t *pixel = &framebuffer[row * LCD_Get_Width() + x];
        for (col = x; col < x_end; ++col)
        {
            *pixel++ = color;
        }
    }
}

/**
 * @brief  逐行复制连续 RGB565 像素到 LCD 帧缓冲
 */
void LCD_Write_Area(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, const uint16_t *colors)
{
    uint16_t *framebuffer;
    uint32_t source_width;
    uint32_t copy_width;
    uint32_t row;
    uint32_t x_end;
    uint32_t y_end;

    if (!LCD_Is_Ready() || colors == NULL || x1 > x2 || y1 > y2 || x1 >= LCD_Get_Width() ||
        y1 >= LCD_Get_Height())
    {
        return;
    }
    source_width = (uint32_t)x2 - x1 + 1U;
    x_end = x2;
    y_end = y2;
    if (x_end >= LCD_Get_Width())
    {
        x_end = LCD_Get_Width() - 1U;
    }
    if (y_end >= LCD_Get_Height())
    {
        y_end = LCD_Get_Height() - 1U;
    }
    copy_width = x_end - x1 + 1U;

    framebuffer = BSP_LTDC_LCD_Get_Framebuffer();
    for (row = y1; row <= y_end; ++row)
    {
        uint16_t *destination = &framebuffer[row * LCD_Get_Width() + x1];
        memcpy(destination, colors, copy_width * sizeof(uint16_t));
        colors += source_width;
    }
}

/**
 * @brief  用指定 RGB565 颜色填充当前面板整个有效显示区域
 */
void LCD_Clear(uint16_t color)
{
    if (LCD_Is_Ready())
    {
        LCD_Fill_Rect(0U, 0U, LCD_Get_Width(), LCD_Get_Height(), color);
    }
}
