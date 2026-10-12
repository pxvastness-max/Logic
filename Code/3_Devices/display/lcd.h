/**
 * @file   lcd.h
 * @brief  RGB LCD面板识别及RGB565显示功能接口
 */

#ifndef LCD_H
#define LCD_H

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================== */

#include <stdbool.h>
#include <stdint.h>

/* ========================= 配置 ========================= */

#define LCD_FRAMEBUFFER_MAX_WIDTH 1280U /* 支持的最大水平分辨率 */

/* ================================================== */

typedef enum
{
    LCD_STATUS_OK = 0,
    LCD_STATUS_NO_PANEL,
    LCD_STATUS_UNSUPPORTED_PANEL,
    LCD_STATUS_CLOCK_ERROR,
    LCD_STATUS_INIT_ERROR,
    LCD_STATUS_LAYER_ERROR
} LCD_Status_t;

/* ================================================== */

/**
 * @brief  识别 RGB 面板并按设备参数配置 LTDC 显示层
 */
LCD_Status_t LCD_Init(void);

/**
 * @brief  查询 LCD 设备状态与 BSP 显示硬件状态是否均就绪
 */
bool LCD_Is_Ready(void);

/**
 * @brief  读取板级识别引脚并转换为面板编号
 */
uint16_t LCD_Get_Panel_ID(void);

/**
 * @brief  返回最近一次识别成功的面板水平分辨率
 */
uint16_t LCD_Get_Width(void);

/**
 * @brief  返回最近一次识别成功的面板垂直分辨率
 */
uint16_t LCD_Get_Height(void);

/**
 * @brief  向 LCD 帧缓冲写入一个 RGB565 像素
 * @param  x 像素横坐标，有效范围 0~面板宽度-1
 * @param  y 像素纵坐标，有效范围 0~面板高度-1
 */
void LCD_Draw_Pixel(uint16_t x, uint16_t y, uint16_t color);

/**
 * @brief  用相同 RGB565 颜色填充矩形并裁剪至面板边界
 * @param  width 矩形宽度，0 表示不绘制
 * @param  height 矩形高度，0 表示不绘制
 */
void LCD_Fill_Rect(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint16_t color);

/**
 * @brief  逐行复制连续 RGB565 像素到 LCD 帧缓冲
 * @param  x2 右边界横坐标，必须不小于 x1
 * @param  y2 下边界纵坐标，必须不小于 y1
 */
void LCD_Write_Area(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, const uint16_t *colors);

/**
 * @brief  用指定 RGB565 颜色填充当前面板整个有效显示区域
 */
void LCD_Clear(uint16_t color);

/* ================================================== */

#ifdef __cplusplus
}
#endif

#endif /* LCD_H */
