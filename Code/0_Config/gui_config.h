/* gui_config.h：波形搬运、刷新节流与可选诊断配置 */

#ifndef GUI_CONFIG_H
#define GUI_CONFIG_H

/* ========================= 配置 ========================= */

/* 0 使用 CPU 逐行复制，1 使用 DMA2D 异步复制 */

#ifndef WAVE_TRANSFER_USE_DMA2D
    #define WAVE_TRANSFER_USE_DMA2D 0U
#endif

/* 每累计指定数量的 LTDC 场周期发出一次波形准备请求，3 表示每三个场周期更新一次 */

#ifndef WAVE_REFRESH_FRAME_DIVIDER
    #define WAVE_REFRESH_FRAME_DIVIDER 3U
#endif

/* 1 创建显示诊断任务，每 5 s 检查并输出新增 LTDC 错误 */

#ifndef DISPLAY_DIAGNOSTICS_ENABLE
    #define DISPLAY_DIAGNOSTICS_ENABLE 0U
#endif

/* 1 创建 CPU 统计任务，每 1 s 根据 DWT 区间周期输出一行总占用率 */

#ifndef CPU_DIAGNOSTICS_ENABLE
    #define CPU_DIAGNOSTICS_ENABLE 1U
#endif

#if WAVE_REFRESH_FRAME_DIVIDER < 1U
    #error "WAVE_REFRESH_FRAME_DIVIDER must be at least 1"
#endif

#endif /* GUI_CONFIG_H */
