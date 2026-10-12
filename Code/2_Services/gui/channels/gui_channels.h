/**
 * @file   gui_channels.h
 * @brief  左侧通道控件
 */

#ifndef GUI_CHANNELS_H
#define GUI_CHANNELS_H

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================== */

#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>

/* ================================================== */

typedef struct
{
    const char *pin_text;    /* 彩色徽标中的物理通道名，如 D1 */
    const char *name_text;   /* 通道标题；创建期间 LVGL 会复制文本 */
    const char *detail_text; /* 协议别名或其他说明 */
    uint32_t color;          /* 徽标颜色，0xRRGGBB */
    bool detail_visible;     /* 是否显示说明文本 */
    bool separator_visible;  /* 是否显示标题下方的分隔线 */
} GUI_Channel_Config_t;

typedef struct
{
    lv_obj_t *side_row;     /* 一路左侧信息的父容器 */
    lv_obj_t *badge;        /* 彩色物理通道徽标 */
    lv_obj_t *name_label;   /* 通道名称 */
    lv_obj_t *detail_label; /* 协议别名或说明 */
    lv_obj_t *separator;    /* 分隔线 */
} GUI_Channel_Row_t;

/* ================================================== */

/**
 * @brief  创建一路左侧通道徽标、名称和描述文字
 * @note   GUI 任务调用；控件归 parent 所有，失败后清理父容器
 */
bool GUI_Channel_Create(lv_obj_t *parent,
                        int16_t top,
                        int16_t height,
                        const GUI_Channel_Config_t *config,
                        GUI_Channel_Row_t *row);

/* ================================================== */

#ifdef __cplusplus
}
#endif

#endif /* GUI_CHANNELS_H */
