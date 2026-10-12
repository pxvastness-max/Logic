/**
 * @file   display_diagnostics.h
 * @brief  显示统计与 LTDC 诊断
 */

#ifndef DISPLAY_DIAGNOSTICS_H
#define DISPLAY_DIAGNOSTICS_H

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================== */

#include <stdbool.h>
#include <stdint.h>

/* ================================================== */

typedef struct
{
    uint64_t lvgl_handler_cycles;     /* 所有 LVGL handler 调用累计 DWT 周期数 */
    uint32_t lvgl_handler_calls;      /* handler 调用次数 */
    uint64_t rendered_handler_cycles; /* 产生显示 flush 的 handler 周期数 */
    uint32_t rendered_handler_calls;  /* 产生显示 flush 的 handler 次数 */
    uint64_t flush_pixels;            /* 实际写入显示帧缓冲的累计像素数 */
    uint32_t flush_calls;             /* 统计窗口内的 flush 次数 */
} Display_Render_Stats_t;

/* ================================================== */

/**
 * @brief  按配置创建可选显示诊断任务
 * @note   调度器启动前调用，关闭任务时保留统计接口
 */
bool Display_Diagnostics_Init(void);

/**
 * @brief  读取自启动以来完成的显示刷新总次数
 */
uint32_t Display_Diagnostics_Get_Flush_Count(void);

/**
 * @brief  记录一次 LVGL 定时处理耗时与是否真正渲染
 * @param  cycles : DWT 周期差
 * @param  rendered : 本次是否产生 flush
 * @note   GUI 任务记录，临界区保护 64 位累计值
 */
void Display_Diagnostics_Record_LVGL_Handler(uint32_t cycles, bool rendered);

/**
 * @brief  记录一次实际复制到 LCD 的显示刷新
 * @note   仅统计实际写入 LCD 的像素
 */
void Display_Diagnostics_Record_Flush(uint32_t pixels);

/**
 * @brief  原子取得当前渲染统计窗口并清零窗口累计值
 * @param  stats : 输出统计，NULL 时忽略
 * @note   原子复制并清零窗口，保留 flush 总次数
 */
void Display_Diagnostics_Get_And_Reset_Render_Stats(Display_Render_Stats_t *stats);

/* ================================================== */

#ifdef __cplusplus
}
#endif

#endif /* DISPLAY_DIAGNOSTICS_H */
