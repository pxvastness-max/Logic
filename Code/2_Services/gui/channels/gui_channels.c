/**
 * @file   gui_channels.c
 * @brief  左侧通道控件
 */

/* ================================================== */

#include "gui/channels/gui_channels.h"

/* ========================= 配置 ========================= */

#define GUI_CHANNEL_LEFT_WIDTH             100
#define GUI_CHANNEL_BADGE_X                6
#define GUI_CHANNEL_BADGE_SIZE             32
#define GUI_CHANNEL_NAME_X                 42
#define GUI_CHANNEL_NAME_Y                 7
#define GUI_CHANNEL_DETAIL_X               42
#define GUI_CHANNEL_DETAIL_Y               31
#define GUI_CHANNEL_SEPARATOR_Y            55
#define GUI_CHANNEL_SEPARATOR_RIGHT_MARGIN 6

/* ================================================== */

static void GUI_Channel_Apply_Style(const GUI_Channel_Config_t *config,
                                    GUI_Channel_Row_t *render,
                                    int16_t height);

/* ================================================== */

bool GUI_Channel_Create(lv_obj_t *parent,
                        int16_t top,
                        int16_t height,
                        const GUI_Channel_Config_t *config,
                        GUI_Channel_Row_t *row)
{
    if (parent == NULL || config == NULL || row == NULL || height <= 0)
    {
        return false;
    }

    row->side_row = lv_obj_create(parent);
    if (row->side_row == NULL)
    {
        return false;
    }

    lv_obj_remove_style_all(row->side_row);
    lv_obj_set_pos(row->side_row, 0, top);
    lv_obj_set_size(row->side_row, GUI_CHANNEL_LEFT_WIDTH, height);
    lv_obj_set_scrollable(row->side_row, false);

    row->badge = lv_label_create(row->side_row);
    row->name_label = lv_label_create(row->side_row);
    row->detail_label = lv_label_create(row->side_row);
    row->separator = lv_obj_create(row->side_row);
    if (row->badge == NULL || row->name_label == NULL || row->detail_label == NULL ||
        row->separator == NULL)
    {
        return false;
    }

    GUI_Channel_Apply_Style(config, row, height);
    return true;
}

/* ================================================== */

/**
 * @brief  将通道配置映射到左侧徽标、名称、说明与分隔线的 LVGL 样式
 */
static void GUI_Channel_Apply_Style(const GUI_Channel_Config_t *config,
                                    GUI_Channel_Row_t *render,
                                    int16_t height)
{

    lv_label_set_text(render->badge, config->pin_text);
    lv_obj_set_pos(render->badge, GUI_CHANNEL_BADGE_X, (height - GUI_CHANNEL_BADGE_SIZE) / 2);
    lv_obj_set_size(render->badge, GUI_CHANNEL_BADGE_SIZE, GUI_CHANNEL_BADGE_SIZE);
    lv_obj_set_style_bg_color(render->badge, lv_color_hex(config->color), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(render->badge, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(render->badge, 10, LV_PART_MAIN);
    lv_obj_set_style_text_color(render->badge, lv_color_hex(0xFFFFFFU), LV_PART_MAIN);
    lv_obj_set_style_text_align(render->badge, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_pad_top(render->badge, 9, LV_PART_MAIN);

    lv_label_set_text(render->name_label, config->name_text);
    lv_obj_set_pos(render->name_label, GUI_CHANNEL_NAME_X, GUI_CHANNEL_NAME_Y);
    lv_obj_set_width(render->name_label, GUI_CHANNEL_LEFT_WIDTH - GUI_CHANNEL_NAME_X - 2);
    lv_label_set_long_mode(render->name_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_color(render->name_label, lv_color_hex(0xE8EDF2U), LV_PART_MAIN);
    lv_obj_set_style_text_letter_space(render->name_label, -1, LV_PART_MAIN);

    lv_label_set_text(render->detail_label, config->detail_text);
    lv_obj_set_pos(render->detail_label, GUI_CHANNEL_DETAIL_X, GUI_CHANNEL_DETAIL_Y);
    lv_obj_set_width(render->detail_label, GUI_CHANNEL_LEFT_WIDTH - GUI_CHANNEL_DETAIL_X - 2);
    lv_label_set_long_mode(render->detail_label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_color(render->detail_label, lv_color_hex(0x91A0B2U), LV_PART_MAIN);
    lv_obj_set_hidden(render->detail_label, !config->detail_visible);

    lv_obj_remove_style_all(render->separator);
    lv_obj_set_pos(render->separator, GUI_CHANNEL_NAME_X, GUI_CHANNEL_SEPARATOR_Y);
    lv_obj_set_size(render->separator,
                    GUI_CHANNEL_LEFT_WIDTH - GUI_CHANNEL_NAME_X -
                        GUI_CHANNEL_SEPARATOR_RIGHT_MARGIN,
                    1);
    lv_obj_set_style_bg_color(render->separator, lv_color_hex(0x65717CU), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(render->separator, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_scrollable(render->separator, false);
    lv_obj_set_hidden(render->separator, !config->separator_visible);
}
