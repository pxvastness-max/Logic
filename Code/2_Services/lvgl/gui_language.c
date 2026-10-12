/**
 * @file   gui_language.c
 * @brief  界面语言与双语文字选择
 */

/* ================================================== */

#include "lvgl/gui_language.h"

/* ================================================== */

static GUI_Language_t gui_language = GUI_LANGUAGE_CHINESE;
static uint32_t gui_language_revision;

/* ================================================== */

GUI_Language_t GUI_Language_Get(void)
{
    return gui_language;
}

bool GUI_Language_Set(GUI_Language_t language)
{
    if ((uint32_t)language >= GUI_LANGUAGE_COUNT)
    {
        return false;
    }
    if (gui_language != language)
    {
        gui_language = language;
        gui_language_revision++;
    }
    return true;
}

uint32_t GUI_Language_Revision(void)
{
    return gui_language_revision;
}

const char *GUI_Language_Text(const char *chinese, const char *english)
{
    return gui_language == GUI_LANGUAGE_ENGLISH ? english : chinese;
}
