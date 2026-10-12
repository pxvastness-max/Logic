/**
 * @file   gui_system_page.c
 * @brief  系统设置页面
 */

/* ================================================== */

#include "gui/pages/gui_pages.h"
#include "lvgl/gui_language.h"
#include "lvgl/resource/resource_font.h"

/* ========================= 配置 ========================= */

#define GUI_SYSTEM_TITLE_COLOR  0x2F80EDU
#define GUI_SYSTEM_TEXT_COLOR   0xE6EDF7U
#define GUI_SYSTEM_INPUT_COLOR  0x202833U
#define GUI_SYSTEM_BORDER_COLOR 0x2B3440U

/* ================================================== */

static void GUI_System_Language_Event_Callback(lv_event_t *event);

/* ================================================== */

/**
 * @brief  保存语言选择，界面重建由导航轮询延后执行
 */
static void GUI_System_Language_Event_Callback(lv_event_t *event)
{
    lv_obj_t *dropdown = lv_event_get_target_obj(event);
    (void)GUI_Language_Set((GUI_Language_t)lv_dropdown_get_selected(dropdown));
}

/* ================================================== */

/**
 * @brief  创建只有语言选择项的系统设置页
 */
void GUI_System_Page_Create(lv_obj_t *page_parent)
{
    lv_obj_t *page;
    lv_obj_t *label;
    lv_obj_t *dropdown;
    lv_obj_t *list;

    if (page_parent == NULL)
    {
        return;
    }
    page = lv_obj_create(page_parent);
    if (page == NULL)
    {
        return;
    }
    lv_obj_remove_style_all(page);
    lv_obj_set_size(page, lv_obj_get_width(page_parent), lv_obj_get_height(page_parent));
    lv_obj_set_scrollable(page, false);
    lv_obj_set_style_bg_color(page, lv_color_hex(0x000000U), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(page, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_text_font(page, APP_FONT_CN_14_PTR, LV_PART_MAIN);

    label = lv_label_create(page);
    if (label != NULL)
    {
        lv_label_set_text(label, GUI_TEXT("系统设置", "Settings"));
        lv_obj_set_pos(label, 8, 14);
        lv_obj_set_style_text_color(label, lv_color_hex(GUI_SYSTEM_TITLE_COLOR), LV_PART_MAIN);
    }
    label = lv_label_create(page);
    if (label != NULL)
    {
        lv_label_set_text(label, GUI_TEXT("语言", "Language"));
        lv_obj_set_pos(label, 8, 52);
        lv_obj_set_style_text_color(label, lv_color_hex(GUI_SYSTEM_TEXT_COLOR), LV_PART_MAIN);
    }
    dropdown = lv_dropdown_create(page);
    if (dropdown == NULL)
    {
        return;
    }
    lv_obj_set_pos(dropdown, 8, 78);
    lv_obj_set_size(dropdown, 122, 31);
    /* 语言名称始终使用所属语言，保证任何当前语言下都能辨认目标选项 */
    lv_dropdown_set_options_static(dropdown, "简体中文\nEnglish");
    lv_dropdown_set_selected(dropdown, (uint32_t)GUI_Language_Get());
    lv_obj_set_style_radius(dropdown, 5, LV_PART_MAIN);
    lv_obj_set_style_bg_color(dropdown, lv_color_hex(GUI_SYSTEM_INPUT_COLOR), LV_PART_MAIN);
    lv_obj_set_style_border_color(dropdown, lv_color_hex(GUI_SYSTEM_BORDER_COLOR), LV_PART_MAIN);
    lv_obj_set_style_border_width(dropdown, 1, LV_PART_MAIN);
    lv_obj_set_style_text_color(dropdown, lv_color_hex(GUI_SYSTEM_TEXT_COLOR), LV_PART_MAIN);
    lv_obj_set_style_text_font(dropdown, APP_FONT_CN_14_PTR, LV_PART_MAIN);
    lv_obj_set_style_text_font(dropdown, LV_FONT_DEFAULT, LV_PART_INDICATOR);
    /* 列表独立挂在屏幕上，普通项和高亮项都显式使用含中文的字体 */
    list = lv_dropdown_get_list(dropdown);
    lv_obj_set_style_text_font(list, APP_FONT_CN_14_PTR, LV_PART_MAIN);
    lv_obj_set_style_text_font(list, APP_FONT_CN_14_PTR, LV_PART_SELECTED);
    lv_obj_add_event_cb(dropdown, GUI_System_Language_Event_Callback, LV_EVENT_VALUE_CHANGED, NULL);
}
