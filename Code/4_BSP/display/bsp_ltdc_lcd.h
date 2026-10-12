/**
 * @file   bsp_ltdc_lcd.h
 * @brief  阿波罗 H743 板级 LTDC、RGB 接口和帧缓冲支持
 */

#ifndef BSP_LTDC_LCD_H
#define BSP_LTDC_LCD_H

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================== */

#include "stm32h7xx_hal.h"
#include "memory/bsp_fmc_sdram.h"
#include <stdbool.h>
#include <stdint.h>

/* ========================= 配置 ========================= */

#define BSP_LTDC_FRAMEBUFFER_MAX_WIDTH  1280U             /* 板级显示接口支持的最大宽度 */
#define BSP_LTDC_FRAMEBUFFER_MAX_HEIGHT 800U              /* 板级显示接口支持的最大高度 */
#define BSP_LTDC_PIXEL_SIZE_BYTES       2U                /* RGB565单像素字节数 */
#define BSP_LTDC_SDRAM_SIZE_BYTES (8UL * 1024UL * 1024UL) /* 链接脚本为 UI_DATA 预留的 SDRAM 容量 */

/* ================================================== */

/* Device层传给BSP的LTDC时序配置 */
typedef struct
{
    uint16_t width;                /* 有效显示宽度，单位为像素 */
    uint16_t height;               /* 有效显示高度，单位为像素 */
    uint16_t hsw;                  /* 水平同步宽度，单位为像素时钟 */
    uint16_t hbp;                  /* 水平后沿，单位为像素时钟 */
    uint16_t hfp;                  /* 水平前沿，单位为像素时钟 */
    uint16_t vsw;                  /* 垂直同步宽度，单位为行 */
    uint16_t vbp;                  /* 垂直后沿，单位为行 */
    uint16_t vfp;                  /* 垂直前沿，单位为行 */
    uint16_t pll3_r;               /* PLL3 R分频值 */
    uint32_t pixel_clock_polarity; /* 面板要求的像素时钟极性 */
} BSP_LTDC_Config_t;

/* BSP LTDC初始化结果 */
typedef enum
{
    BSP_LTDC_STATUS_OK = 0,
    BSP_LTDC_STATUS_CLOCK_ERROR,
    BSP_LTDC_STATUS_INIT_ERROR,
    BSP_LTDC_STATUS_LAYER_ERROR
} BSP_LTDC_Status_t;

/**
 * @brief  LTDC 每帧起始回调
 * @note   回调在 LTDC 中断上下文执行
 */
typedef void (*BSP_LTDC_Frame_Callback_t)(void *context);

/* 一次读取到的 Layer 1 缓冲状态和 LTDC 寄存器值 */
typedef struct
{
    uint32_t software_front_address;  /* 软件记录的当前扫描缓冲地址 */
    uint32_t software_back_address;   /* 软件记录的待绘制缓冲地址 */
    uint32_t hardware_frame_address;  /* 叠加图像层 CFBAR 寄存器的当前帧地址 */
    uint32_t frame_buffer_length;     /* CFBLR 寄存器，包含有效行长与物理行距 */
    uint32_t frame_buffer_line_count; /* CFBLNR 寄存器，包含缓冲行数 */
    uint32_t horizontal_window;       /* WHPCR 寄存器，包含水平窗口起止位置 */
    uint32_t vertical_window;         /* WVPCR 寄存器，包含垂直窗口起止位置 */
    uint32_t control;                 /* 叠加图像层使能和色键控制位 */
    uint32_t pixel_format;            /* 当前像素格式寄存器值 */
    uint32_t constant_alpha;          /* 当前常量透明度寄存器值 */
    uint32_t blending;                /* 前景/背景混合因子寄存器值 */
    uint32_t color_key;               /* 透明色键寄存器值 */
    uint32_t present_count;           /* 已完成的垂直消隐换帧次数 */
    bool swap_pending;                /* 已提交地址重载，仍等待硬件换帧确认 */
} BSP_LTDC_Overlay_Diagnostic_t;

/* 保存 LTDC 错误和换帧状态；错误位置在 HAL 清除标志前读取 */
typedef struct
{
    uint32_t fifo_underrun_count;       /* HAL 已上报的 FIFO 欠载累计次数 */
    uint32_t transfer_error_count;      /* HAL 已上报的总线传输错误累计次数 */
    uint32_t error_irq_count;           /* 独立 LTDC 错误 IRQ 的进入次数 */
    uint32_t line_event_count;          /* 每帧起始行事件的累计次数 */
    uint32_t reload_event_count;        /* 硬件重载完成事件累计次数，含非换帧重载 */
    uint32_t present_count;             /* 叠加图像双缓冲实际完成交换的次数 */
    uint32_t hal_error_code;            /* HAL 句柄持续锁存的错误位图 */
    uint32_t interrupt_flags;           /* 读取诊断时的 ISR 寄存器 */
    uint32_t interrupt_enable;          /* 读取诊断时的 IER 寄存器 */
    uint32_t last_error_flags;          /* 最近错误 IRQ 清标志前保存的有效错误位 */
    uint32_t last_error_position;       /* 最近错误时的当前像素位置寄存器 */
    uint32_t last_error_display_status; /* 最近错误时的同步/数据使能状态寄存器 */
    uint32_t last_error_frame_count;    /* 最近错误发生时的软件帧计数 */
    bool swap_pending;                  /* 是否正在等待叠加图像层地址重载完成 */
} BSP_LTDC_Error_Diagnostic_t;

/* ================================================== */

extern LTDC_HandleTypeDef bsp_ltdc_handle; /* HAL LTDC外设句柄，供中断入口使用 */

/* ================================================== */

/**
 * @brief  在 RGB 复用配置前读取面板 ID 的三位 GPIO 编码
 */
uint8_t BSP_LTDC_LCD_Read_ID_Bits(void);

/**
 * @brief  配置面板像素时钟、LTDC 时序及整屏 RGB565 图层，成功后开启背光
 * @param  config 设备层提供的完整面板参数，尺寸不能超过板级最大值
 */
BSP_LTDC_Status_t BSP_LTDC_LCD_Init(const BSP_LTDC_Config_t *config);

/**
 * @brief  查询LTDC就绪状态
 */
bool BSP_LTDC_LCD_Is_Ready(void);

/**
 * @brief  取得整屏RGB565帧缓冲
 * @note   只在SDRAM初始化成功后访问
 */
uint16_t *BSP_LTDC_LCD_Get_Framebuffer(void);

/**
 * @brief  返回初始化后累计的行事件帧计数，数值按 32 位无符号整数自然回绕
 */
uint32_t BSP_LTDC_LCD_Get_Frame_Count(void);

/**
 * @brief  返回 HAL 锁存的 LTDC 错误位
 */
uint32_t BSP_LTDC_LCD_Get_Error_Code(void);

/**
 * @brief  LTDC运行期间累计发生的FIFO欠载次数
 */
uint32_t BSP_LTDC_LCD_Get_Fifo_Underrun_Count(void);

/**
 * @brief  LTDC运行期间累计发生的传输错误次数
 */
uint32_t BSP_LTDC_LCD_Get_Transfer_Error_Count(void);

/* 在LTDC错误IRQ入口、HAL清除标志前保存硬件现场 */
void BSP_LTDC_LCD_Capture_Error_IRQ(void);

/**
 * @brief  在保存/恢复 PRIMASK 的临界区内复制错误、IRQ 和换帧状态
 * @param  diagnostic 调用者提供的输出结构，不能为 NULL
 */
bool BSP_LTDC_LCD_Get_Error_Diagnostic(BSP_LTDC_Error_Diagnostic_t *diagnostic);

/* 注册每帧起始回调；传入 NULL 可取消注册 */
void BSP_LTDC_LCD_Set_Frame_Callback(BSP_LTDC_Frame_Callback_t callback, void *context);

/**
 * @brief  初始化RGB565叠加层双缓冲
 * @note   黑色色键透明，首帧在present后显示
 */
bool BSP_LTDC_LCD_Overlay_Layer_Init(uint16_t x,
                                     uint16_t y,
                                     uint16_t width,
                                     uint16_t height,
                                     uint16_t background_color);

/**
 * @brief  取得叠加层后缓冲
 * @note   等待换帧期间返回NULL
 */
uint16_t *BSP_LTDC_LCD_Overlay_Layer_Get_Back_Buffer(void);

/**
 * @brief  叠加图像 Layer 1 的一行像素数量；Layer 未初始化时返回 0
 */
uint16_t BSP_LTDC_LCD_Overlay_Layer_Get_Width(void);

/**
 * @brief  叠加图像 Layer 1 的行数；Layer 未初始化时返回 0
 */
uint16_t BSP_LTDC_LCD_Overlay_Layer_Get_Height(void);

/**
 * @brief  叠加图像帧缓冲相邻两行的像素步长，包含行尾对齐填充
 */
uint16_t BSP_LTDC_LCD_Overlay_Layer_Get_Stride(void);

/**
 * @brief  在垂直消隐期临时显示或隐藏叠加图像硬件层
 */
bool BSP_LTDC_LCD_Overlay_Layer_Set_Visible(bool visible);

/**
 * @brief  提交叠加层后缓冲
 * @note   在下一个垂直消隐期换帧
 */
bool BSP_LTDC_LCD_Overlay_Layer_Present(void);

/**
 * @brief  Layer 1 已在垂直消隐期完成的实际换帧次数
 */
uint32_t BSP_LTDC_LCD_Overlay_Layer_Get_Present_Count(void);

/* 读取Layer 1地址、行距、窗口和混合寄存器，仅供调试 */
bool BSP_LTDC_LCD_Overlay_Layer_Get_Diagnostic(BSP_LTDC_Overlay_Diagnostic_t *diagnostic);

/* ================================================== */

#ifdef __cplusplus
}
#endif

#endif /* BSP_LTDC_LCD_H */
