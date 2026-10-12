/**
 * @file   gui_panel.c
 * @brief  导航与采集状态显示
 */

/* ================================================== */

#include "gui/pages/gui_panel.h"
#include "gui/pages/gui_icons.h"
#include "gui/pages/gui_pages.h"
#include "lvgl/gui_language.h"
#include "log/log_service.h"
#include <stdint.h>
#include <string.h>

/* ========================= 配置 ========================= */

#define GUI_PANEL_PAGE_SEPARATOR_X 600
#define GUI_PANEL_PAGE_X           602
#define GUI_PANEL_PAGE_WIDTH       138
#define GUI_PANEL_TOOLBAR_X        740
#define GUI_PANEL_TOOLBAR_WIDTH    60
#define GUI_PANEL_TOOLBAR_BUTTON   48
#define GUI_PANEL_TOOLBAR_BUTTON_X 5
#define GUI_PANEL_TOOLBAR_FIRST_Y  20
#define GUI_PANEL_TOOLBAR_GAP      16
#define GUI_PANEL_TOOLBAR_NAV_FIRST_Y \
    (GUI_PANEL_TOOLBAR_FIRST_Y + GUI_PANEL_TOOLBAR_BUTTON + GUI_PANEL_TOOLBAR_GAP)
#define GUI_PANEL_CAPTURE_ARC_SIZE 40
#define GUI_PANEL_COLOR_SEPARATOR  0x2B3440U
#define GUI_PANEL_COLOR_SCREEN_BG  0x000000U
#define GUI_PANEL_COLOR_TOOLBAR_BG 0x171D25U
#define GUI_PANEL_COLOR_ACCENT     0x2F80EDU

/* ================================================== */

typedef enum
{
    GUI_PANEL_PAGE_OPEN_FILE = 0,   /* 文件打开页面 */
    GUI_PANEL_PAGE_DEVICE_CONFIG,   /* 设备与通道配置页面 */
    GUI_PANEL_PAGE_PROTOCOL_DECODE, /* 协议解码页面 */
    GUI_PANEL_PAGE_SYSTEM_SETTINGS, /* 界面语言等系统设置页面 */
    GUI_PANEL_PAGE_COUNT            /* 页面数量，仅用于数组长度和检查 */
} GUI_Panel_Page_t;

typedef struct
{
    lv_obj_t *root;                          /* 统一拥有右侧界面全部对象 */
    lv_obj_t *page_host;                     /* x=602~739 的当前页面父对象 */
    lv_obj_t *buttons[GUI_PANEL_PAGE_COUNT]; /* 页面导航按钮 */
    GUI_Panel_Page_t current_page;           /* 语言变化后重建的当前页面 */
    uint32_t language_revision;              /* 已应用到当前页面的语言修订号 */

    lv_obj_t *capture_button;                /* 工具栏最上方的开始/暂停按钮 */
    lv_obj_t *capture_arc;                   /* 围绕按钮符号的整数百分比圆环 */
    lv_obj_t *capture_symbol;                /* 播放或暂停符号标签 */
    GUI_Panel_Capture_State_t capture_state; /* 当前采集控制状态 */
    uint8_t progress_percent;                /* 当前已经显示的整数进度百分比 */
} GUI_Panel_State_t;

/* ================================================== */

static GUI_Panel_State_t gui_panel;
static GUI_Panel_Capture_Callback_t gui_panel_capture_callback;
static void *gui_panel_capture_context;

/* ================================================== */

static void GUI_Panel_Nav_Event_Callback(lv_event_t *e);
static void GUI_Panel_Show_Page(GUI_Panel_Page_t page);
static bool GUI_Panel_Create_Capture_Control(lv_obj_t *toolbar);
static void GUI_Panel_Capture_Event_Callback(lv_event_t *e);
static void GUI_Panel_Capture_Update_Symbol(void);
static void GUI_Panel_Root_Delete_Event_Callback(lv_event_t *event);

/* ================================================== */

/**
 * @brief  创建右侧导航、页面容器和采集控制按钮
 */
bool GUI_Panel_Create(lv_obj_t *parent)
{
    static const GUI_Icon_t icons[GUI_PANEL_PAGE_COUNT] = {GUI_ICON_OPEN_FILE,
                                                           GUI_ICON_DEVICE_CONFIG,
                                                           GUI_ICON_PROTOCOL_DECODE,
                                                           GUI_ICON_SYSTEM_SETTINGS};
    lv_obj_t *separator;
    lv_obj_t *toolbar;
    lv_display_t *display;
    int32_t display_height;
    uint32_t i;

    if (parent == NULL)
    {
        (void)Log_Printf("ui: side panel parent is null\r\n");
        return false;
    }
    if (gui_panel.page_host != NULL)
    {
        return true;
    }

    /* 直接读取显示器分辨率，不依赖尚未执行布局的屏幕对象尺寸 */
    display = lv_obj_get_display(parent);
    if (display == NULL)
    {
        (void)Log_Printf("ui: side panel display is null\r\n");
        return false;
    }
    display_height = lv_display_get_vertical_resolution(display);
    if (display_height <= 0 || lv_display_get_horizontal_resolution(display) <
                                   GUI_PANEL_TOOLBAR_X + GUI_PANEL_TOOLBAR_WIDTH)
    {
        (void)Log_Printf("ui: side panel invalid display height=%ld\r\n", (long)display_height);
        return false;
    }

    gui_panel.root = lv_obj_create(parent);
    if (gui_panel.root == NULL)
    {
        return false;
    }
    lv_obj_remove_style_all(gui_panel.root);
    lv_obj_set_pos(gui_panel.root, GUI_PANEL_PAGE_SEPARATOR_X, 0);
    lv_obj_set_size(gui_panel.root,
                    GUI_PANEL_TOOLBAR_X + GUI_PANEL_TOOLBAR_WIDTH - GUI_PANEL_PAGE_SEPARATOR_X,
                    display_height);
    lv_obj_set_scrollable(gui_panel.root, false);
    lv_obj_add_event_cb(gui_panel.root,
                        GUI_Panel_Root_Delete_Event_Callback,
                        LV_EVENT_DELETE,
                        NULL);

    /* x=600 的竖线隔开波形区和后续页面区；x=600..739 暂时保持为空 */
    separator = lv_obj_create(gui_panel.root);
    if (separator == NULL)
    {
        goto create_failed;
    }
    lv_obj_remove_style_all(separator);
    lv_obj_set_pos(separator, 0, 0);
    lv_obj_set_size(separator, 2, display_height);
    lv_obj_set_style_bg_color(separator, lv_color_hex(GUI_PANEL_COLOR_SEPARATOR), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(separator, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_scrollable(separator, false);

    gui_panel.page_host = lv_obj_create(gui_panel.root);
    if (gui_panel.page_host == NULL)
    {
        (void)Log_Printf("ui: page host allocation failed\r\n");
        goto create_failed;
    }
    lv_obj_remove_style_all(gui_panel.page_host);
    lv_obj_set_pos(gui_panel.page_host, GUI_PANEL_PAGE_X - GUI_PANEL_PAGE_SEPARATOR_X, 0);
    lv_obj_set_size(gui_panel.page_host, GUI_PANEL_PAGE_WIDTH, display_height);
    lv_obj_set_style_bg_color(gui_panel.page_host,
                              lv_color_hex(GUI_PANEL_COLOR_SCREEN_BG),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_opa(gui_panel.page_host, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_scrollable(gui_panel.page_host, false);

    toolbar = lv_obj_create(gui_panel.root);
    if (toolbar == NULL)
    {
        (void)Log_Printf("ui: toolbar allocation failed\r\n");
        goto create_failed;
    }
    lv_obj_remove_style_all(toolbar);
    lv_obj_set_pos(toolbar, GUI_PANEL_TOOLBAR_X - GUI_PANEL_PAGE_SEPARATOR_X, 0);
    lv_obj_set_size(toolbar, GUI_PANEL_TOOLBAR_WIDTH, display_height);
    lv_obj_set_style_bg_color(toolbar, lv_color_hex(GUI_PANEL_COLOR_TOOLBAR_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(toolbar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_scrollable(toolbar, false);

    if (!GUI_Panel_Create_Capture_Control(toolbar))
    {
        (void)Log_Printf("ui: capture control allocation failed\r\n");
        goto create_failed;
    }

    for (i = 0U; i < GUI_PANEL_PAGE_COUNT; ++i)
    {
        lv_obj_t *button = lv_button_create(toolbar);
        lv_obj_t *icon;

        if (button == NULL)
        {
            (void)Log_Printf("ui: nav button %lu allocation failed\r\n", (unsigned long)i);
            goto create_failed;
        }

        gui_panel.buttons[i] = button;
        lv_obj_set_pos(button,
                       GUI_PANEL_TOOLBAR_BUTTON_X,
                       GUI_PANEL_TOOLBAR_NAV_FIRST_Y +
                           (int32_t)i * (GUI_PANEL_TOOLBAR_BUTTON + GUI_PANEL_TOOLBAR_GAP));
        lv_obj_set_size(button, GUI_PANEL_TOOLBAR_BUTTON, GUI_PANEL_TOOLBAR_BUTTON);
        /* 图标在完整按钮内居中，避免默认内边距形成负内容坐标或滚动偏移 */
        lv_obj_set_style_pad_all(button, 0, LV_PART_MAIN);
        lv_obj_set_scrollable(button, false);
        lv_obj_set_style_radius(button, 12, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(button, 0, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN);
        lv_obj_set_style_bg_color(button,
                                  lv_color_hex(GUI_PANEL_COLOR_ACCENT),
                                  LV_PART_MAIN | LV_STATE_CHECKED);
        lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(button, lv_color_hex(0x25364FU), LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_add_event_cb(button,
                            GUI_Panel_Nav_Event_Callback,
                            LV_EVENT_CLICKED,
                            (void *)(uintptr_t)i);

        icon = GUI_Icon_Create(button, icons[i]);
        if (icon != NULL)
        {
            lv_obj_center(icon);
        }
        else
        {
            goto create_failed;
        }
    }

    /* 页面创建需要读取 page_host 的实际尺寸，先完成 LVGL 延迟布局 */
    lv_obj_update_layout(parent);

    /* 首屏固定显示设备配置 */
    GUI_Panel_Show_Page(GUI_PANEL_PAGE_DEVICE_CONFIG);
    return true;

create_failed:
    lv_obj_delete(gui_panel.root);
    return false;
}

/* ================================================== */

/**
 * @brief  删除右侧根容器时清空控件句柄和采集按钮状态
 */
static void GUI_Panel_Root_Delete_Event_Callback(lv_event_t *event)
{
    (void)event;
    memset(&gui_panel, 0, sizeof(gui_panel));
}

/* ================================================== */

/**
 * @brief  绑定采集按钮请求的处理接口
 */
void GUI_Panel_Bind_Capture(GUI_Panel_Capture_Callback_t callback, void *context)
{
    gui_panel_capture_callback = callback;
    gui_panel_capture_context = context;
}

/**
 * @brief  用应用提供的采集状态刷新按钮和进度圆环
 * @note   GUI 任务调用；控件未创建时只保存状态
 */
void GUI_Panel_Set_Capture_Status(GUI_Panel_Capture_State_t state, uint8_t progress_percent)
{
    const bool state_changed = gui_panel.capture_state != state;
    const uint8_t progress = progress_percent > 100U ? 100U : progress_percent;

    if (gui_panel.capture_arc != NULL && gui_panel.progress_percent != progress)
    {
        lv_arc_set_value(gui_panel.capture_arc, progress);
    }
    gui_panel.capture_state = state;
    gui_panel.progress_percent = progress;
    if (state_changed)
    {
        GUI_Panel_Capture_Update_Symbol();
    }
}

/**
 * @brief  同步当前页面的语言和配置控件
 * @note   延后重建页面，避免删除当前事件的祖先对象
 */
void GUI_Panel_Process(void)
{
    if (gui_panel.page_host != NULL && gui_panel.language_revision != GUI_Language_Revision())
    {
        GUI_Panel_Show_Page(gui_panel.current_page);
    }
    if (gui_panel.page_host != NULL && gui_panel.current_page == GUI_PANEL_PAGE_DEVICE_CONFIG)
    {
        GUI_Device_Page_Process();
    }
}

/* ================================================== */

/**
 * @brief  将采集按钮点击提交给应用
 */
static void GUI_Panel_Capture_Event_Callback(lv_event_t *event)
{
    (void)event;
    if (gui_panel_capture_callback != NULL)
    {
        gui_panel_capture_callback(gui_panel_capture_context);
    }
}

/**
 * @brief  根据点击按钮保存的页面编号切换右侧内容页
 */
static void GUI_Panel_Nav_Event_Callback(lv_event_t *e)
{
    const GUI_Panel_Page_t page = (GUI_Panel_Page_t)(uintptr_t)lv_event_get_user_data(e);

    GUI_Panel_Show_Page(page);
}

/**
 * @brief  清理旧页面并创建目标页同时更新唯一导航选中状态
 */
static void GUI_Panel_Show_Page(GUI_Panel_Page_t page)
{
    uint32_t i;

    if (gui_panel.page_host == NULL || page >= GUI_PANEL_PAGE_COUNT)
    {
        (void)Log_Printf("ui: show page rejected host=%p page=%lu\r\n",
                         (void *)gui_panel.page_host,
                         (unsigned long)page);
        return;
    }

    for (i = 0U; i < GUI_PANEL_PAGE_COUNT; ++i)
    {
        if (i == (uint32_t)page)
        {
            lv_obj_add_state(gui_panel.buttons[i], LV_STATE_CHECKED);
        }
        else
        {
            lv_obj_clear_state(gui_panel.buttons[i], LV_STATE_CHECKED);
        }
        lv_obj_invalidate(gui_panel.buttons[i]);
    }

    /* 删除当前页面根对象，再调用目标页面文件中的显示任务重建界面 */
    GUI_Protocol_Close_Dialog();
    gui_panel.current_page = page;
    gui_panel.language_revision = GUI_Language_Revision();
    lv_obj_clean(gui_panel.page_host);
    switch (page)
    {
        case GUI_PANEL_PAGE_OPEN_FILE:
            GUI_File_Page_Create(gui_panel.page_host);
            break;
        case GUI_PANEL_PAGE_DEVICE_CONFIG:
            GUI_Device_Page_Create(gui_panel.page_host);
            break;
        case GUI_PANEL_PAGE_PROTOCOL_DECODE:
            GUI_Protocol_Page_Create(gui_panel.page_host);
            break;
        case GUI_PANEL_PAGE_SYSTEM_SETTINGS:
            GUI_System_Page_Create(gui_panel.page_host);
            break;
        default:
            break;
    }
}

/**
 * @brief  创建采集按钮、进度圆环和播放暂停符号
 */
static bool GUI_Panel_Create_Capture_Control(lv_obj_t *toolbar)
{
    gui_panel.capture_button = lv_button_create(toolbar);
    if (gui_panel.capture_button == NULL)
    {
        return false;
    }

    lv_obj_set_pos(gui_panel.capture_button, GUI_PANEL_TOOLBAR_BUTTON_X, GUI_PANEL_TOOLBAR_FIRST_Y);
    lv_obj_set_size(gui_panel.capture_button, GUI_PANEL_TOOLBAR_BUTTON, GUI_PANEL_TOOLBAR_BUTTON);
    lv_obj_set_style_radius(gui_panel.capture_button, 12, LV_PART_MAIN);
    lv_obj_set_style_bg_color(gui_panel.capture_button,
                              lv_color_hex(GUI_PANEL_COLOR_ACCENT),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_opa(gui_panel.capture_button, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(gui_panel.capture_button, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(gui_panel.capture_button, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(gui_panel.capture_button, 0, LV_PART_MAIN);
    gui_panel.capture_arc = lv_arc_create(gui_panel.capture_button);
    if (gui_panel.capture_arc == NULL)
    {
        lv_obj_delete(gui_panel.capture_button);
        gui_panel.capture_button = NULL;
        return false;
    }
    lv_obj_set_size(gui_panel.capture_arc, GUI_PANEL_CAPTURE_ARC_SIZE, GUI_PANEL_CAPTURE_ARC_SIZE);
    lv_obj_center(gui_panel.capture_arc);
    lv_arc_set_range(gui_panel.capture_arc, 0, 100);
    lv_arc_set_rotation(gui_panel.capture_arc, 270);
    lv_arc_set_bg_angles(gui_panel.capture_arc, 0, 360);
    lv_arc_set_value(gui_panel.capture_arc, 0);
    lv_obj_remove_style(gui_panel.capture_arc, NULL, LV_PART_KNOB);
    lv_obj_set_style_arc_width(gui_panel.capture_arc, 2, LV_PART_MAIN);
    lv_obj_set_style_arc_color(gui_panel.capture_arc, lv_color_hex(0x7FB5FFU), LV_PART_MAIN);
    lv_obj_set_style_arc_opa(gui_panel.capture_arc, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_arc_width(gui_panel.capture_arc, 2, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(gui_panel.capture_arc, lv_color_hex(0xFFFFFFU), LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(gui_panel.capture_arc, true, LV_PART_INDICATOR);
    lv_obj_set_clickable(gui_panel.capture_arc, false);

    gui_panel.capture_symbol = lv_label_create(gui_panel.capture_button);
    if (gui_panel.capture_symbol == NULL)
    {
        lv_obj_delete(gui_panel.capture_button);
        gui_panel.capture_button = NULL;
        gui_panel.capture_arc = NULL;
        return false;
    }
    lv_obj_set_style_text_color(gui_panel.capture_symbol, lv_color_hex(0xFFFFFFU), LV_PART_MAIN);
    lv_label_set_text_static(gui_panel.capture_symbol, LV_SYMBOL_PLAY);
    lv_obj_center(gui_panel.capture_symbol);
    lv_obj_set_clickable(gui_panel.capture_symbol, false);

    gui_panel.capture_state = GUI_PANEL_CAPTURE_IDLE;
    gui_panel.progress_percent = 0U;
    lv_obj_add_event_cb(gui_panel.capture_button,
                        GUI_Panel_Capture_Event_Callback,
                        LV_EVENT_CLICKED,
                        NULL);
    return true;
}

/**
 * @brief  按空闲、待启动和运行态更新按钮符号
 */
static void GUI_Panel_Capture_Update_Symbol(void)
{
    if (gui_panel.capture_symbol == NULL)
    {
        return;
    }

    if (gui_panel.capture_state == GUI_PANEL_CAPTURE_RUNNING)
    {
        lv_label_set_text_static(gui_panel.capture_symbol, LV_SYMBOL_PAUSE);
    }
    else if (gui_panel.capture_state == GUI_PANEL_CAPTURE_STARTING)
    {
        lv_label_set_text_static(gui_panel.capture_symbol, LV_SYMBOL_REFRESH);
    }
    else
    {
        lv_label_set_text_static(gui_panel.capture_symbol, LV_SYMBOL_PLAY);
    }
    lv_obj_center(gui_panel.capture_symbol);
}
