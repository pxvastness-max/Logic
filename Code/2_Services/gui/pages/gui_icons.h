/**
 * @file   gui_icons.h
 * @brief  导航矢量图标
 */

#ifndef GUI_ICONS_H
#define GUI_ICONS_H

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================== */

#include "lvgl.h"

/* ================================================== */

typedef enum
{
    GUI_ICON_OPEN_FILE = 0,
    GUI_ICON_DEVICE_CONFIG,
    GUI_ICON_PROTOCOL_DECODE,
    GUI_ICON_SYSTEM_SETTINGS
} GUI_Icon_t;

/* ================================================== */

/**
 * @brief  创建由私有绘制回调生成的导航矢量图标
 */
lv_obj_t *GUI_Icon_Create(lv_obj_t *parent, GUI_Icon_t icon);

/* ================================================== */

#ifdef __cplusplus
}
#endif

#endif /* GUI_ICONS_H */
