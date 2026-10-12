/**
 * @file   gui_icons.c
 * @brief  导航矢量图标
 */

/* ================================================== */

#include "gui/pages/gui_icons.h"
#include <stdint.h>

/* ========================= 配置 ========================= */

#define GUI_ICON_SIZE         34
#define GUI_ICON_COLOR        0x8EA0BAU
#define GUI_ICON_ACTIVE_COLOR 0xF4F8FFU

/* ================================================== */

static void GUI_Icon_Draw_Callback(lv_event_t *e);
static void GUI_Icon_Line(lv_layer_t *layer,
                          lv_draw_line_dsc_t *dsc,
                          int32_t x1,
                          int32_t y1,
                          int32_t x2,
                          int32_t y2);
static void GUI_Icon_Settings_Gear(lv_layer_t *layer, const lv_area_t *area, lv_color_t color);

/* ================================================== */

lv_obj_t *GUI_Icon_Create(lv_obj_t *parent, GUI_Icon_t icon)
{
    lv_obj_t *obj = lv_obj_create(parent);

    if (obj == NULL)
    {
        return NULL;
    }

    lv_obj_remove_style_all(obj);
    lv_obj_set_size(obj, GUI_ICON_SIZE, GUI_ICON_SIZE);
    lv_obj_set_clickable(obj, false);
    lv_obj_set_scrollable(obj, false);
    lv_obj_set_event_bubble(obj, true);
    lv_obj_add_event_cb(obj, GUI_Icon_Draw_Callback, LV_EVENT_DRAW_MAIN, (void *)(uintptr_t)icon);
    return obj;
}

/* ================================================== */

/**
 * @brief  在导航图标绘制事件中生成文件、设备、协议或设置齿轮线条
 */
static void GUI_Icon_Draw_Callback(lv_event_t *e)
{
    static const int8_t gear_ring[][2] =
        {{17, 7}, {24, 10}, {27, 17}, {24, 24}, {17, 27}, {10, 24}, {7, 17}, {10, 10}};
    const GUI_Icon_t icon = (GUI_Icon_t)(uintptr_t)lv_event_get_user_data(e);
    const lv_obj_t *obj = lv_event_get_current_target(e);
    lv_draw_line_dsc_t dsc;
    lv_area_t c;
    uint32_t i;

    lv_obj_get_content_coords(obj, &c);
    lv_draw_line_dsc_init(&dsc);
    dsc.color = lv_color_hex(lv_obj_is_checked(lv_obj_get_parent(obj)) ? GUI_ICON_ACTIVE_COLOR :
                                                                         GUI_ICON_COLOR);
    dsc.width = 3U;
    dsc.opa = LV_OPA_COVER;
    dsc.round_start = 1U;
    dsc.round_end = 1U;

    if (icon == GUI_ICON_OPEN_FILE)
    {
        GUI_Icon_Line(lv_event_get_layer(e), &dsc, c.x1 + 4, c.y1 + 11, c.x1 + 14, c.y1 + 11);
        GUI_Icon_Line(lv_event_get_layer(e), &dsc, c.x1 + 14, c.y1 + 11, c.x1 + 17, c.y1 + 15);
        GUI_Icon_Line(lv_event_get_layer(e), &dsc, c.x1 + 17, c.y1 + 15, c.x1 + 30, c.y1 + 15);
        GUI_Icon_Line(lv_event_get_layer(e), &dsc, c.x1 + 4, c.y1 + 11, c.x1 + 4, c.y1 + 27);
        GUI_Icon_Line(lv_event_get_layer(e), &dsc, c.x1 + 4, c.y1 + 27, c.x1 + 27, c.y1 + 27);
        GUI_Icon_Line(lv_event_get_layer(e), &dsc, c.x1 + 27, c.y1 + 27, c.x1 + 30, c.y1 + 15);
    }
    else if (icon == GUI_ICON_SYSTEM_SETTINGS)
    {
        GUI_Icon_Settings_Gear(lv_event_get_layer(e), &c, dsc.color);
    }
    else if (icon == GUI_ICON_DEVICE_CONFIG)
    {
        for (i = 0U; i < 8U; ++i)
        {
            const uint32_t next = (i + 1U) % 8U;
            GUI_Icon_Line(lv_event_get_layer(e),
                          &dsc,
                          c.x1 + gear_ring[i][0],
                          c.y1 + gear_ring[i][1],
                          c.x1 + gear_ring[next][0],
                          c.y1 + gear_ring[next][1]);
        }
        GUI_Icon_Line(lv_event_get_layer(e), &dsc, c.x1 + 17, c.y1 + 3, c.x1 + 17, c.y1 + 7);
        GUI_Icon_Line(lv_event_get_layer(e), &dsc, c.x1 + 31, c.y1 + 17, c.x1 + 27, c.y1 + 17);
        GUI_Icon_Line(lv_event_get_layer(e), &dsc, c.x1 + 17, c.y1 + 31, c.x1 + 17, c.y1 + 27);
        GUI_Icon_Line(lv_event_get_layer(e), &dsc, c.x1 + 3, c.y1 + 17, c.x1 + 7, c.y1 + 17);
        dsc.width = 2U;
        GUI_Icon_Line(lv_event_get_layer(e), &dsc, c.x1 + 14, c.y1 + 17, c.x1 + 20, c.y1 + 17);
        GUI_Icon_Line(lv_event_get_layer(e), &dsc, c.x1 + 17, c.y1 + 14, c.x1 + 17, c.y1 + 20);
    }
    else if (icon == GUI_ICON_PROTOCOL_DECODE)
    {
        GUI_Icon_Line(lv_event_get_layer(e), &dsc, c.x1 + 10, c.y1 + 6, c.x1 + 5, c.y1 + 6);
        GUI_Icon_Line(lv_event_get_layer(e), &dsc, c.x1 + 5, c.y1 + 6, c.x1 + 5, c.y1 + 28);
        GUI_Icon_Line(lv_event_get_layer(e), &dsc, c.x1 + 5, c.y1 + 28, c.x1 + 10, c.y1 + 28);
        GUI_Icon_Line(lv_event_get_layer(e), &dsc, c.x1 + 24, c.y1 + 6, c.x1 + 29, c.y1 + 6);
        GUI_Icon_Line(lv_event_get_layer(e), &dsc, c.x1 + 29, c.y1 + 6, c.x1 + 29, c.y1 + 28);
        GUI_Icon_Line(lv_event_get_layer(e), &dsc, c.x1 + 29, c.y1 + 28, c.x1 + 24, c.y1 + 28);
        GUI_Icon_Line(lv_event_get_layer(e), &dsc, c.x1 + 10, c.y1 + 22, c.x1 + 14, c.y1 + 22);
        GUI_Icon_Line(lv_event_get_layer(e), &dsc, c.x1 + 14, c.y1 + 22, c.x1 + 14, c.y1 + 12);
        GUI_Icon_Line(lv_event_get_layer(e), &dsc, c.x1 + 14, c.y1 + 12, c.x1 + 20, c.y1 + 12);
        GUI_Icon_Line(lv_event_get_layer(e), &dsc, c.x1 + 20, c.y1 + 12, c.x1 + 20, c.y1 + 22);
        GUI_Icon_Line(lv_event_get_layer(e), &dsc, c.x1 + 20, c.y1 + 22, c.x1 + 24, c.y1 + 22);
    }
}

/**
 * @brief  设置线段端点并提交到当前 LVGL 绘制层
 */
static void GUI_Icon_Line(lv_layer_t *layer,
                          lv_draw_line_dsc_t *dsc,
                          int32_t x1,
                          int32_t y1,
                          int32_t x2,
                          int32_t y2)
{
    dsc->p1.x = x1;
    dsc->p1.y = y1;
    dsc->p2.x = x2;
    dsc->p2.y = y2;
    lv_draw_line(layer, dsc);
}

/**
 * @brief  绘制参考图的六齿实心圆角设置齿轮，中心孔保留父按钮背景
 */
static void GUI_Icon_Settings_Gear(lv_layer_t *layer, const lv_area_t *area, lv_color_t color)
{
    static const int8_t teeth[][4] = {{0, -9, 0, -10},
                                      {8, -5, 10, -6},
                                      {8, 5, 10, 6},
                                      {0, 9, 0, 10},
                                      {-8, 5, -10, 6},
                                      {-8, -5, -10, -6}};
    const int32_t center_x = area->x1 + lv_area_get_width(area) / 2;
    const int32_t center_y = area->y1 + lv_area_get_height(area) / 2;
    lv_draw_line_dsc_t line;
    lv_draw_arc_dsc_t ring;
    uint32_t index;

    lv_draw_line_dsc_init(&line);
    line.color = color;
    line.width = 7U;
    line.opa = LV_OPA_COVER;
    line.round_start = 1U;
    line.round_end = 1U;
    for (index = 0U; index < sizeof(teeth) / sizeof(teeth[0]); index++)
    {
        GUI_Icon_Line(layer,
                      &line,
                      center_x + teeth[index][0],
                      center_y + teeth[index][1],
                      center_x + teeth[index][2],
                      center_y + teeth[index][3]);
    }

    /* 厚圆环连接六个圆角齿，内半径约 4 像素且始终露出真实按钮背景 */
    lv_draw_arc_dsc_init(&ring);
    ring.center.x = center_x;
    ring.center.y = center_y;
    ring.radius = 10U;
    ring.width = 6;
    ring.color = color;
    ring.opa = LV_OPA_COVER;
    ring.start_angle = 0;
    ring.end_angle = 360;
    lv_draw_arc(layer, &ring);
}
