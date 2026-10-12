/**
 * @file   resource_font.h
 * @brief  界面共用的 14 px 中文子集字库声明和访问宏
 */

#ifndef RESOURCE_FONT_H
#define RESOURCE_FONT_H

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================== */

#include "lvgl.h"

/* ================================================== */

/* 由 lv_font_conv 从 Noto Sans SC 生成的 14 px 中文子集字库 */
LV_FONT_DECLARE(app_font_cn_14)

#define APP_FONT_CN_14_PTR (&app_font_cn_14)

/* ================================================== */

#ifdef __cplusplus
}
#endif

#endif /* RESOURCE_FONT_H */
