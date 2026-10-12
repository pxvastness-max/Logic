/**
 * @file   gui_language.h
 * @brief  界面语言与双语文字选择
 */

#ifndef GUI_LANGUAGE_H
#define GUI_LANGUAGE_H

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================== */

#include <stdbool.h>
#include <stdint.h>

/* ================================================== */

typedef enum
{
    GUI_LANGUAGE_CHINESE = 0, /* 默认简体中文 */
    GUI_LANGUAGE_ENGLISH,     /* 英文 */
    GUI_LANGUAGE_COUNT        /* 语言数量，不是有效选择 */
} GUI_Language_t;

/* ================================================== */

/**
 * @brief  读取当前 GUI 语言
 */
GUI_Language_t GUI_Language_Get(void);

/**
 * @brief  保存界面语言，并在实际变化时更新修订号
 * @note   GUI 任务调用，复位后恢复中文
 */
bool GUI_Language_Set(GUI_Language_t language);

/**
 * @brief  读取语言变化修订号，供界面判断是否需要刷新文字
 * @note   只比较是否相等，不依赖修订号大小
 */
uint32_t GUI_Language_Revision(void);

/**
 * @brief  按当前语言选择一份长期有效的双语文字
 * @note   返回借用指针，文字须覆盖控件生命周期
 */
const char *GUI_Language_Text(const char *chinese, const char *english);

#define GUI_TEXT(chinese, english) GUI_Language_Text((chinese), (english))

/* ================================================== */

#ifdef __cplusplus
}
#endif

#endif /* GUI_LANGUAGE_H */
