/**
 * @file   gui_device_page.c
 * @brief  设备配置页面
 */

/* ================================================== */

#include "gui/pages/gui_pages.h"
#include "lvgl/resource/resource_font.h"
#include "log/log_service.h"
#include "waveform/wave_service.h"
#include "lvgl/gui_language.h"
#include <string.h>

/* ========================= 配置 ========================= */

#define GUI_DEVICE_PAGE_WIDTH       138
#define GUI_DEVICE_TITLE_COLOR      0x2F80EDU
#define GUI_DEVICE_TEXT_COLOR       0xE6EDF7U
#define GUI_DEVICE_TEXT_MUTED_COLOR 0x91A0B2U
#define GUI_DEVICE_SEPARATOR_COLOR  0x2B3440U
#define GUI_DEVICE_VALUE_BG_COLOR   0x202833U
#define GUI_DEVICE_CHANNEL_SIZE     34

/* UTF-8 字节转义避免 Keil 的源文件代码页改变中文字节 */
#define GUI_DEVICE_TEXT_DEVICE_CONFIG "\xE8\xAE\xBE\xE5\xA4\x87\xE9\x85\x8D\xE7\xBD\xAE"

#define GUI_DEVICE_TEXT_CHANNEL_SELECT "\xE9\x80\x9A\xE9\x81\x93\xE9\x80\x89\xE6\x8B\xA9"
#define GUI_DEVICE_TEXT_CAPTURE_PARAM  "\xE9\x87\x87\xE9\x9B\x86\xE5\x8F\x82\xE6\x95\xB0"
#define GUI_DEVICE_TEXT_SAMPLE_RATE    "\xE9\x87\x87\xE6\xA0\xB7\xE9\xA2\x91\xE7\x8E\x87"
#define GUI_DEVICE_TEXT_SAMPLE_TIME    "\xE9\x87\x87\xE6\xA0\xB7\xE6\x97\xB6\xE9\x97\xB4"
#define GUI_DEVICE_TEXT_CAPTURE_FILTER "\xE9\x87\x87\xE9\x9B\x86\xE6\xBB\xA4\xE6\xB3\xA2"
#define GUI_DEVICE_TEXT_FILTER_OFF     "\xE5\x85\xB3\xE9\x97\xAD"
#define GUI_DEVICE_TEXT_FILTER_APPROX  "\xE7\xBA\xA6"

/* 时间按 240 MHz 定时器内核时钟标注，不随时间戳分频变化 */
#define GUI_DEVICE_TEXT_FILTER_OPTIONS_CN \
    GUI_DEVICE_TEXT_FILTER_OFF \
    "\n" GUI_DEVICE_TEXT_FILTER_APPROX " 8 ns\n" GUI_DEVICE_TEXT_FILTER_APPROX \
    " 33 ns\n" GUI_DEVICE_TEXT_FILTER_APPROX " 67 ns\n" GUI_DEVICE_TEXT_FILTER_APPROX \
    " 200 ns\n" GUI_DEVICE_TEXT_FILTER_APPROX " 1.07 us"

#define GUI_DEVICE_TEXT_FILTER_OPTIONS_EN "Off\n~8 ns\n~33 ns\n~67 ns\n~200 ns\n~1.07 us"

/* 最多 15 个预设时长，另留一项保存从文件等路径得到的非预设实际值 */
#define GUI_DEVICE_CAPTURE_OPTION_CAPACITY 16U

#define GUI_DEVICE_CAPTURE_TEXT_CAPACITY 160U

/* ================================================== */

typedef struct
{
    const char *name;       /* 按钮显示的物理通道编号 */
    uint32_t color;         /* 从 wave 配置读取并缓存的通道颜色 */
    uint32_t channel_index; /* wave 模块使用的物理通道下标 */
    lv_obj_t *button;       /* 当前页面中的通道选择按钮 */
    lv_obj_t *label;        /* 当前按钮内部的编号标签 */
    bool visual_valid;      /* 是否已经按 visual_enabled 设置当前按钮样式 */
    bool visual_enabled;    /* 最近一次同步到按钮的通道显示状态 */
} GUI_Device_Channel_t;

typedef struct
{
    uint32_t value;   /* 频率表单位为 Hz，时长表单位为微秒 */
    const char *text; /* 对应菜单的长期有效显示文字 */
} GUI_Device_Capture_Option_t;

typedef struct
{
    const GUI_Device_Capture_Option_t *options; /* 按真实数值递增排列的只读预设表 */
    uint32_t option_count;                      /* 预设表项数，不包含临时实际值 */
    bool frequency_menu;                        /* true 表示频率菜单，false 表示微秒时长菜单 */
    lv_obj_t *dropdown;                         /* 当前页面的下拉框，随页面父容器删除 */
    uint32_t values[GUI_DEVICE_CAPTURE_OPTION_CAPACITY]; /* 过滤后每个菜单下标对应的真实值 */
    uint32_t count;                                      /* values 中有效映射项数 */
    char text[GUI_DEVICE_CAPTURE_TEXT_CAPACITY];         /* 长期有效的选项文字，供 LVGL 静态借用 */
    char current_text[24]; /* 当前组合无效时仍显示实际参数，不把菜单首项当作已保存值 */
} GUI_Device_Capture_Menu_t;

/* ================================================== */

static const GUI_Device_Capture_Option_t gui_device_sample_rates[] = {{1000000U, "1 MHz"},
                                                                      {2000000U, "2 MHz"},
                                                                      {5000000U, "5 MHz"},
                                                                      {10000000U, "10 MHz"},
                                                                      {20000000U, "20 MHz"},
                                                                      {24000000U, "24 MHz"},
                                                                      {40000000U, "40 MHz"},
                                                                      {48000000U, "48 MHz"},
                                                                      {80000000U, "80 MHz"},
                                                                      {120000000U, "120 MHz"},
                                                                      {240000000U, "240 MHz"}};

static const GUI_Device_Capture_Option_t gui_device_capture_times[] = {{10000U, "10 ms"},
                                                                       {20000U, "20 ms"},
                                                                       {50000U, "50 ms"},
                                                                       {100000U, "100 ms"},
                                                                       {200000U, "200 ms"},
                                                                       {500000U, "500 ms"},
                                                                       {1000000U, "1 s"},
                                                                       {2000000U, "2 s"},
                                                                       {5000000U, "5 s"},
                                                                       {10000000U, "10 s"},
                                                                       {20000000U, "20 s"},
                                                                       {50000000U, "50 s"},
                                                                       {60000000U, "1 min"},
                                                                       {120000000U, "2 min"},
                                                                       {300000000U, "5 min"}};

static GUI_Device_Capture_Menu_t gui_device_rate_menu = {
    .options = gui_device_sample_rates,
    .option_count = sizeof(gui_device_sample_rates) / sizeof(gui_device_sample_rates[0]),
    .frequency_menu = true};

static GUI_Device_Capture_Menu_t gui_device_time_menu = {
    .options = gui_device_capture_times,
    .option_count = sizeof(gui_device_capture_times) / sizeof(gui_device_capture_times[0]),
    .frequency_menu = false};

static bool gui_device_capture_options_updating; /* 重建菜单期间忽略程序设置触发的嵌套变更事件 */
static const uint8_t gui_device_capture_filters[] = {0U, 1U, 3U, 5U, 8U, 15U};
static lv_obj_t *gui_device_filter_dropdown;    /* 当前页面拥有的滤波下拉框，重建时重新赋值 */
static bool gui_device_filter_options_updating; /* 同步实际滤波值期间忽略嵌套变更事件 */
static char gui_device_filter_current_text[16]; /* 非菜单档位的实际值，供 LVGL 静态借用 */

static GUI_Device_Channel_t gui_device_channels[] = {{"1", 0U, 0U, NULL, NULL, false, false},
                                                     {"2", 0U, 1U, NULL, NULL, false, false},
                                                     {"3", 0U, 2U, NULL, NULL, false, false},
                                                     {"4", 0U, 3U, NULL, NULL, false, false},
                                                     {"5", 0U, 4U, NULL, NULL, false, false},
                                                     {"6", 0U, 5U, NULL, NULL, false, false}};

/* ================================================== */

static lv_obj_t *GUI_Device_Label(lv_obj_t *parent,
                                  const char *text,
                                  int32_t x,
                                  int32_t y,
                                  uint32_t color);
static void GUI_Device_Channel_Button(lv_obj_t *parent, uint32_t index, int32_t x, int32_t y);
static void GUI_Device_Channel_Set_Visual(GUI_Device_Channel_t *channel, bool enabled);
static void GUI_Device_Channel_Event_Callback(lv_event_t *event);
static void GUI_Device_Separator(lv_obj_t *parent, int32_t y);
static lv_obj_t *GUI_Device_Dropdown_Row(lv_obj_t *parent,
                                         const char *name,
                                         const char *options,
                                         int32_t y);
static void GUI_Device_Rate_Event_Callback(lv_event_t *event);
static void GUI_Device_Time_Event_Callback(lv_event_t *event);
static void GUI_Device_Filter_Event_Callback(lv_event_t *event);
static void GUI_Device_Refresh_Capture_Filter(void);
static bool GUI_Device_Capture_Pair_Is_Valid(uint32_t rate_hz, uint32_t duration_us);
static bool GUI_Device_Append_Capture_Option(GUI_Device_Capture_Menu_t *menu,
                                             uint32_t value,
                                             const char *text);
static bool GUI_Device_Build_Capture_Menu(GUI_Device_Capture_Menu_t *menu,
                                          uint32_t other_value,
                                          uint32_t selected_value);
static void GUI_Device_Refresh_Capture_Options(void);
static void GUI_Device_Apply_Capture_Parameters(GUI_Device_Capture_Menu_t *menu);
static lv_obj_t *GUI_Device_Page_Create_Root(lv_obj_t *page_parent);

/* ================================================== */

/**
 * @brief  创建通道显示选择、时间戳频率、采集时长和统一输入滤波设置页
 */
void GUI_Device_Page_Create(lv_obj_t *page_parent)
{
    lv_obj_t *page;
    uint32_t i;

    gui_device_filter_dropdown = NULL;
    page = GUI_Device_Page_Create_Root(page_parent);
    if (page == NULL)
    {
        (void)Log_Printf("ui: device config root allocation failed\r\n");
        return;
    }

    /* 页面根对象继承中文字体；所有后续控件均可直接显示 UTF-8 中文 */
    lv_obj_set_style_text_font(page, APP_FONT_CN_14_PTR, LV_PART_MAIN);
    (void)GUI_Device_Label(page,
                           GUI_TEXT(GUI_DEVICE_TEXT_DEVICE_CONFIG, "Device"),
                           8,
                           14,
                           GUI_DEVICE_TITLE_COLOR);
    (void)GUI_Device_Label(page,
                           GUI_TEXT(GUI_DEVICE_TEXT_CHANNEL_SELECT, "Channels"),
                           8,
                           46,
                           GUI_DEVICE_TEXT_COLOR);

    /* 六路通道固定为 2 行 × 3 列，不显示 All / Clear 等操作按钮 */
    for (i = 0U; i < (sizeof(gui_device_channels) / sizeof(gui_device_channels[0])); ++i)
    {
        const int32_t column = (int32_t)(i % 3U);
        const int32_t row = (int32_t)(i / 3U);
        GUI_Device_Channel_Button(page, i, 10 + column * 42, 72 + row * 40);
    }

    GUI_Device_Separator(page, 158);
    (void)GUI_Device_Label(page,
                           GUI_TEXT(GUI_DEVICE_TEXT_CAPTURE_PARAM, "Capture"),
                           8,
                           172,
                           GUI_DEVICE_TEXT_COLOR);
    gui_device_rate_menu.dropdown =
        GUI_Device_Dropdown_Row(page, GUI_TEXT(GUI_DEVICE_TEXT_SAMPLE_RATE, "Rate"), "", 202);
    gui_device_time_menu.dropdown =
        GUI_Device_Dropdown_Row(page, GUI_TEXT(GUI_DEVICE_TEXT_SAMPLE_TIME, "Time"), "", 272);
    gui_device_filter_dropdown = GUI_Device_Dropdown_Row(
        page,
        GUI_TEXT(GUI_DEVICE_TEXT_CAPTURE_FILTER, "Capture filter"),
        GUI_TEXT(GUI_DEVICE_TEXT_FILTER_OPTIONS_CN, GUI_DEVICE_TEXT_FILTER_OPTIONS_EN),
        342);
    /* 读取实际保存的组合后双向筛选，不用第一个预设覆盖原选择 */
    GUI_Device_Refresh_Capture_Options();
    GUI_Device_Refresh_Capture_Filter();
    GUI_Device_Page_Process();
    if (gui_device_rate_menu.dropdown != NULL)
    {
        lv_obj_add_event_cb(gui_device_rate_menu.dropdown,
                            GUI_Device_Rate_Event_Callback,
                            LV_EVENT_VALUE_CHANGED,
                            NULL);
    }
    if (gui_device_time_menu.dropdown != NULL)
    {
        lv_obj_add_event_cb(gui_device_time_menu.dropdown,
                            GUI_Device_Time_Event_Callback,
                            LV_EVENT_VALUE_CHANGED,
                            NULL);
    }
    if (gui_device_filter_dropdown != NULL)
    {
        lv_obj_add_event_cb(gui_device_filter_dropdown,
                            GUI_Device_Filter_Event_Callback,
                            LV_EVENT_VALUE_CHANGED,
                            NULL);
    }
}

/**
 * @brief  在当前设备配置页同步通道按钮与滤波下拉框状态
 * @note   只在当前页面有效时调用
 */
void GUI_Device_Page_Process(void)
{
    uint32_t index;

    for (index = 0U; index < sizeof(gui_device_channels) / sizeof(gui_device_channels[0]); index++)
    {
        Wave_Channel_Config_t config;
        GUI_Device_Channel_t *channel = &gui_device_channels[index];

        if (channel->button != NULL && Wave_Get_Channel_Config(channel->channel_index, &config))
        {
            GUI_Device_Channel_Set_Visual(channel, config.enabled);
        }
    }

    if (gui_device_filter_dropdown != NULL)
    {
        const bool configurable = Wave_Capture_Filter_Is_Configurable();

        if (!configurable && lv_dropdown_is_open(gui_device_filter_dropdown))
        {
            /* 采集刚启动时关闭已展开列表，避免独立列表继续接受点击 */
            lv_dropdown_close(gui_device_filter_dropdown);
        }
        lv_obj_set_disabled(gui_device_filter_dropdown, !configurable);
    }
}

/* ================================================== */

/**
 * @brief  创建无滚动的设备配置页根容器
 */
static lv_obj_t *GUI_Device_Page_Create_Root(lv_obj_t *page_parent)
{
    lv_obj_t *page;

    if (page_parent == NULL)
    {
        return NULL;
    }

    page = lv_obj_create(page_parent);
    if (page == NULL)
    {
        return NULL;
    }

    lv_obj_remove_style_all(page);
    lv_obj_set_pos(page, 0, 0);
    lv_obj_set_size(page, lv_obj_get_width(page_parent), lv_obj_get_height(page_parent));
    lv_obj_set_style_bg_color(page, lv_color_hex(0x000000U), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(page, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_scrollable(page, false);
    return page;
}

/**
 * @brief  创建并定位使用中文字体的单行标签
 */
static lv_obj_t *GUI_Device_Label(lv_obj_t *parent,
                                  const char *text,
                                  int32_t x,
                                  int32_t y,
                                  uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);

    if (label == NULL)
    {
        (void)Log_Printf("ui: device config label allocation failed\r\n");
        return NULL;
    }

    lv_label_set_text(label, text);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_width(label, GUI_DEVICE_PAGE_WIDTH - x - 4);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_text_font(label, APP_FONT_CN_14_PTR, LV_PART_MAIN);
    return label;
}

/**
 * @brief  创建物理通道按钮并从波形配置同步当前显示选择
 */
static void GUI_Device_Channel_Button(lv_obj_t *parent, uint32_t index, int32_t x, int32_t y)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_t *label;
    Wave_Channel_Config_t config;

    if (button == NULL || index >= (sizeof(gui_device_channels) / sizeof(gui_device_channels[0])))
    {
        (void)Log_Printf("ui: channel button %lu allocation failed\r\n", (unsigned long)index);
        return;
    }

    if (Wave_Get_Channel_Config(gui_device_channels[index].channel_index, &config))
    {
        gui_device_channels[index].color = config.color;
    }
    else
    {
        /* 初始化次序异常时使用中性灰，按钮仍然保持可见 */
        gui_device_channels[index].color = GUI_DEVICE_TEXT_MUTED_COLOR;
        config.enabled = false;
    }

    lv_obj_set_pos(button, x, y);
    lv_obj_set_size(button, GUI_DEVICE_CHANNEL_SIZE, GUI_DEVICE_CHANNEL_SIZE);
    lv_obj_set_style_radius(button, 5, LV_PART_MAIN);
    lv_obj_set_style_bg_color(button, lv_color_hex(gui_device_channels[index].color), LV_PART_MAIN);
    lv_obj_set_style_border_color(button,
                                  lv_color_hex(gui_device_channels[index].color),
                                  LV_PART_MAIN);
    lv_obj_set_style_border_width(button, 1, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(button, 0, LV_PART_MAIN);

    label = lv_label_create(button);
    if (label != NULL)
    {
        lv_label_set_text(label, gui_device_channels[index].name);
        lv_obj_set_style_text_font(label, APP_FONT_CN_14_PTR, LV_PART_MAIN);
        lv_obj_center(label);
    }

    gui_device_channels[index].button = button;
    gui_device_channels[index].label = label;
    gui_device_channels[index].visual_valid = false;
    lv_obj_add_event_cb(button,
                        GUI_Device_Channel_Event_Callback,
                        LV_EVENT_CLICKED,
                        &gui_device_channels[index]);

    GUI_Device_Channel_Set_Visual(&gui_device_channels[index], config.enabled);
}

/**
 * @brief  根据显示选择更新通道按钮的填充和文字颜色
 */
static void GUI_Device_Channel_Set_Visual(GUI_Device_Channel_t *channel, bool enabled)
{
    if (channel == NULL || channel->button == NULL)
    {
        return;
    }
    if (channel->visual_valid && channel->visual_enabled == enabled)
    {
        return;
    }

    lv_obj_set_style_bg_opa(channel->button, enabled ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_MAIN);
    if (channel->label != NULL)
    {
        lv_obj_set_style_text_color(channel->label,
                                    lv_color_hex(enabled ? 0xFFFFFFU : channel->color),
                                    LV_PART_MAIN);
    }
    channel->visual_enabled = enabled;
    channel->visual_valid = true;
}

/**
 * @brief  尝试切换一路显示选择并重建静态波形布局
 * @note   不可关闭最后一路，布局失败时恢复原配置
 */
static void GUI_Device_Channel_Event_Callback(lv_event_t *event)
{
    GUI_Device_Channel_t *channel = lv_event_get_user_data(event);
    Wave_Channel_Config_t config;
    bool new_enabled;

    if (channel == NULL || !Wave_Get_Channel_Config(channel->channel_index, &config))
    {
        return;
    }

    new_enabled = !config.enabled;
    if (!Wave_Set_Channel_Enabled(channel->channel_index, new_enabled))
    {
        /* 例如最后一路通道不能关闭时，不重建界面，按钮保持原状态 */
        GUI_Device_Channel_Set_Visual(channel, config.enabled);
        return;
    }

    if (!Wave_Apply_Configuration())
    {
        /* 恢复旧选择，保证按钮、配置与当前界面不会长期处于不同状态 */
        (void)Wave_Set_Channel_Enabled(channel->channel_index, config.enabled);
        (void)Wave_Apply_Configuration();
        GUI_Device_Channel_Set_Visual(channel, config.enabled);
        (void)Log_Printf("ui: channel %lu selection failed\r\n",
                         (unsigned long)(channel->channel_index + 1U));
        return;
    }

    GUI_Device_Channel_Set_Visual(channel, new_enabled);
}

/**
 * @brief  创建一条单像素的横向分隔线
 */
static void GUI_Device_Separator(lv_obj_t *parent, int32_t y)
{
    lv_obj_t *line = lv_obj_create(parent);

    if (line == NULL)
    {
        (void)Log_Printf("ui: device config separator allocation failed\r\n");
        return;
    }

    lv_obj_remove_style_all(line);
    lv_obj_set_pos(line, 8, y);
    lv_obj_set_size(line, GUI_DEVICE_PAGE_WIDTH - 16, 1);
    lv_obj_set_style_bg_color(line, lv_color_hex(GUI_DEVICE_SEPARATOR_COLOR), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(line, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_scrollable(line, false);
}

/**
 * @brief  创建上下排列的参数名称标签和下拉选择框
 */
static lv_obj_t *GUI_Device_Dropdown_Row(lv_obj_t *parent,
                                         const char *name,
                                         const char *options,
                                         int32_t y)
{
    lv_obj_t *dropdown;
    lv_obj_t *list;

    (void)GUI_Device_Label(parent, name, 8, y, GUI_DEVICE_TEXT_MUTED_COLOR);
    dropdown = lv_dropdown_create(parent);
    if (dropdown == NULL)
    {
        return NULL;
    }
    lv_obj_set_pos(dropdown, 8, y + 24);
    lv_obj_set_size(dropdown, GUI_DEVICE_PAGE_WIDTH - 12, 30);
    lv_dropdown_set_options(dropdown, options);
    lv_obj_set_style_radius(dropdown, 5, LV_PART_MAIN);
    lv_obj_set_style_bg_color(dropdown, lv_color_hex(GUI_DEVICE_VALUE_BG_COLOR), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(dropdown, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(dropdown, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(dropdown, lv_color_hex(GUI_DEVICE_SEPARATOR_COLOR), LV_PART_MAIN);
    lv_obj_set_style_text_color(dropdown, lv_color_hex(GUI_DEVICE_TEXT_COLOR), LV_PART_MAIN);
    lv_obj_set_style_text_font(dropdown, APP_FONT_CN_14_PTR, LV_PART_MAIN);
    lv_obj_set_style_text_font(dropdown, LV_FONT_DEFAULT, LV_PART_INDICATOR);
    lv_obj_set_style_pad_left(dropdown, 5, LV_PART_MAIN);
    lv_obj_set_style_pad_right(dropdown, 16, LV_PART_MAIN);
    /* 展开列表独立挂在屏幕上，须同时指定普通选项与高亮项的中文字体 */
    list = lv_dropdown_get_list(dropdown);
    lv_obj_set_style_text_font(list, APP_FONT_CN_14_PTR, LV_PART_MAIN);
    lv_obj_set_style_text_font(list, APP_FONT_CN_14_PTR, LV_PART_SELECTED);
    return dropdown;
}

/**
 * @brief  检查频率和时长是否对应合法的 32 位采集 CNT 范围
 * @note   64 位中间值避免 Hz 与微秒乘积溢出
 */
static bool GUI_Device_Capture_Pair_Is_Valid(uint32_t rate_hz, uint32_t duration_us)
{
    const uint64_t ticks = ((uint64_t)rate_hz * duration_us) / 1000000ULL;

    return rate_hz != 0U && duration_us != 0U && ticks >= 1U && ticks <= UINT32_MAX;
}

/**
 * @brief  为菜单同步追加一项显示文字和真实值映射
 * @note   容量不足时保持菜单原值
 */
static bool GUI_Device_Append_Capture_Option(GUI_Device_Capture_Menu_t *menu,
                                             uint32_t value,
                                             const char *text)
{
    const size_t offset = strlen(menu->text);
    const size_t length = strlen(text);
    const size_t separator_length = menu->count == 0U ? 0U : 1U;

    if (menu->count >= GUI_DEVICE_CAPTURE_OPTION_CAPACITY ||
        offset + separator_length + length >= sizeof(menu->text))
    {
        return false;
    }
    if (separator_length != 0U)
    {
        menu->text[offset] = '\n';
    }
    memcpy(&menu->text[offset + separator_length], text, length + 1U);
    menu->values[menu->count++] = value;
    return true;
}

/**
 * @brief  根据另一项当前实际值构造合法菜单并保留本项当前显示
 * @note   保留实际值，菜单文字由静态缓冲持有
 */
static bool GUI_Device_Build_Capture_Menu(GUI_Device_Capture_Menu_t *menu,
                                          uint32_t other_value,
                                          uint32_t selected_value)
{
    const uint32_t rate_hz = menu->frequency_menu ? selected_value : other_value;
    const uint32_t duration_us = menu->frequency_menu ? other_value : selected_value;
    const bool current_pair_valid = GUI_Device_Capture_Pair_Is_Valid(rate_hz, duration_us);
    uint32_t selected_index = UINT32_MAX;
    uint32_t index;

    menu->count = 0U;
    menu->text[0] = '\0';
    (void)lv_snprintf(menu->current_text,
                      sizeof(menu->current_text),
                      menu->frequency_menu ? "%lu Hz" : "%lu us",
                      (unsigned long)selected_value);
    for (index = 0U; index < menu->option_count; index++)
    {
        const GUI_Device_Capture_Option_t *option = &menu->options[index];
        const uint32_t option_rate_hz = menu->frequency_menu ? option->value : other_value;
        const uint32_t option_duration_us = menu->frequency_menu ? other_value : option->value;

        /* 在首个更大的预设之前插入合法的非预设实际值，保持菜单数值顺序 */
        if (current_pair_valid && selected_index == UINT32_MAX && selected_value < option->value)
        {
            selected_index = menu->count;
            if (!GUI_Device_Append_Capture_Option(menu, selected_value, menu->current_text))
            {
                goto build_failed;
            }
        }
        if (option->value == selected_value)
        {
            /* 无效组合也尽量沿用预设单位显示当前值，但不把该值加入可选列表 */
            (void)lv_snprintf(menu->current_text, sizeof(menu->current_text), "%s", option->text);
        }
        if (!GUI_Device_Capture_Pair_Is_Valid(option_rate_hz, option_duration_us))
        {
            continue;
        }
        if (option->value == selected_value)
        {
            selected_index = menu->count;
        }
        if (!GUI_Device_Append_Capture_Option(menu, option->value, option->text))
        {
            goto build_failed;
        }
    }
    if (current_pair_valid && selected_index == UINT32_MAX)
    {
        selected_index = menu->count;
        if (!GUI_Device_Append_Capture_Option(menu, selected_value, menu->current_text))
        {
            goto build_failed;
        }
    }
    if (menu->count == 0U)
    {
        goto build_failed;
    }
    if (menu->dropdown != NULL)
    {
        lv_dropdown_set_options_static(menu->dropdown, menu->text);
        lv_dropdown_set_selected(menu->dropdown, current_pair_valid ? selected_index : 0U);
        lv_dropdown_set_text_static(menu->dropdown, current_pair_valid ? NULL : menu->current_text);
        lv_dropdown_set_selected_highlight(menu->dropdown, current_pair_valid);
        lv_obj_set_disabled(menu->dropdown, false);
    }
    return true;

build_failed:
    menu->count = 0U;
    menu->text[0] = '\0';
    if (menu->dropdown != NULL)
    {
        lv_dropdown_set_options_static(menu->dropdown, menu->text);
        lv_dropdown_set_text_static(menu->dropdown, menu->current_text);
        lv_dropdown_set_selected_highlight(menu->dropdown, false);
        lv_obj_set_disabled(menu->dropdown, true);
    }
    return false;
}

/**
 * @brief  依据波形控制器已保存的实际组合重建两个菜单
 * @note   保留无效导入组合，重建回调不提交参数
 */
static void GUI_Device_Refresh_Capture_Options(void)
{
    const uint32_t rate_hz = Wave_Get_Capture_Timestamp_Hz();
    const uint32_t duration_us = Wave_Get_Capture_Duration_Us();
    bool rate_ready;
    bool time_ready;

    gui_device_capture_options_updating = true;
    rate_ready = GUI_Device_Build_Capture_Menu(&gui_device_rate_menu, duration_us, rate_hz);
    time_ready = GUI_Device_Build_Capture_Menu(&gui_device_time_menu, rate_hz, duration_us);
    gui_device_capture_options_updating = false;
    if (!rate_ready || !time_ready)
    {
        (void)Log_Printf("ui: capture menu unavailable rate=%lu duration_us=%lu\r\n",
                         (unsigned long)rate_hz,
                         (unsigned long)duration_us);
    }
}

/**
 * @brief  通过变更菜单的真实值映射提交选择并恢复实际保存状态
 * @note   只提交本次变更项，另一项保留实际配置
 */
static void GUI_Device_Apply_Capture_Parameters(GUI_Device_Capture_Menu_t *menu)
{
    uint32_t selected_index;
    uint32_t rate_hz;
    uint32_t duration_us;

    if (gui_device_capture_options_updating || gui_device_rate_menu.dropdown == NULL ||
        gui_device_time_menu.dropdown == NULL)
    {
        return;
    }
    selected_index = lv_dropdown_get_selected(menu->dropdown);
    if (selected_index >= menu->count)
    {
        GUI_Device_Refresh_Capture_Options();
        return;
    }
    rate_hz = menu->frequency_menu ? menu->values[selected_index] : Wave_Get_Capture_Timestamp_Hz();
    duration_us =
        menu->frequency_menu ? Wave_Get_Capture_Duration_Us() : menu->values[selected_index];
    if (!GUI_Device_Capture_Pair_Is_Valid(rate_hz, duration_us))
    {
        (void)Log_Printf("ui: invalid capture parameters rate=%lu duration_us=%lu\r\n",
                         (unsigned long)rate_hz,
                         (unsigned long)duration_us);
    }
    else
    {
        /* 采集状态变化导致配置被拒绝时恢复实际值，不输出正常状态拒绝 */
        (void)Wave_Set_Capture_Parameters(rate_hz, duration_us);
    }
    GUI_Device_Refresh_Capture_Options();
}

/**
 * @brief  用户选择兼容当前时长的新频率后提交并双向刷新菜单
 */
static void GUI_Device_Rate_Event_Callback(lv_event_t *event)
{
    (void)event;
    GUI_Device_Apply_Capture_Parameters(&gui_device_rate_menu);
}

/**
 * @brief  用户选择兼容当前频率的新时长后提交并双向刷新菜单
 */
static void GUI_Device_Time_Event_Callback(lv_event_t *event)
{
    (void)event;
    GUI_Device_Apply_Capture_Parameters(&gui_device_time_menu);
}

/**
 * @brief  将滤波菜单同步为波形控制器已经保存的真实档位
 * @note   非预设档位保留实际编号，不自动保存首项
 */
static void GUI_Device_Refresh_Capture_Filter(void)
{
    const uint8_t input_filter = Wave_Get_Capture_Filter();
    uint32_t index;

    if (gui_device_filter_dropdown == NULL)
    {
        return;
    }
    gui_device_filter_options_updating = true;
    for (index = 0U;
         index < sizeof(gui_device_capture_filters) / sizeof(gui_device_capture_filters[0]);
         index++)
    {
        if (gui_device_capture_filters[index] == input_filter)
        {
            lv_dropdown_set_selected(gui_device_filter_dropdown, index);
            lv_dropdown_set_text_static(gui_device_filter_dropdown, NULL);
            lv_dropdown_set_selected_highlight(gui_device_filter_dropdown, true);
            gui_device_filter_options_updating = false;
            return;
        }
    }
    (void)lv_snprintf(gui_device_filter_current_text,
                      sizeof(gui_device_filter_current_text),
                      "ICxF %u",
                      (unsigned int)input_filter);
    lv_dropdown_set_selected(gui_device_filter_dropdown, 0U);
    lv_dropdown_set_text_static(gui_device_filter_dropdown, gui_device_filter_current_text);
    lv_dropdown_set_selected_highlight(gui_device_filter_dropdown, false);
    gui_device_filter_options_updating = false;
}

/**
 * @brief  按滤波菜单下标映射提交硬件档位并恢复实际保存状态
 * @note   提交失败时恢复已保存档位
 */
static void GUI_Device_Filter_Event_Callback(lv_event_t *event)
{
    uint32_t selected_index;

    (void)event;
    if (gui_device_filter_options_updating || gui_device_filter_dropdown == NULL)
    {
        return;
    }
    selected_index = lv_dropdown_get_selected(gui_device_filter_dropdown);
    if (selected_index < sizeof(gui_device_capture_filters) / sizeof(gui_device_capture_filters[0]))
    {
        (void)Wave_Set_Capture_Filter(gui_device_capture_filters[selected_index]);
    }
    GUI_Device_Refresh_Capture_Filter();
}
