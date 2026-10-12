/**
 * @file   gui_protocol_page.c
 * @brief  协议设置和结果显示
 */

/* ================================================== */

#include "gui/pages/gui_pages.h"
#include "display/bsp_ltdc_lcd.h"
#include "waveform/wave_service.h"
#include "log/log_service.h"
#include "lvgl/resource/resource_font.h"
#include "lvgl/gui_language.h"
#include "protocol/protocol_service.h"
#include <stdint.h>
#include <string.h>

/* ========================= 配置 ========================= */

#define GUI_PROTOCOL_PAGE_TITLE_COLOR     0x2F80EDU
#define GUI_PROTOCOL_TEXT_COLOR           0xE6EDF7U
#define GUI_PROTOCOL_MUTED_COLOR          0xA9B9D0U
#define GUI_PROTOCOL_PANEL_COLOR          0x000000U
#define GUI_PROTOCOL_HEADER_COLOR         0x202020U
#define GUI_PROTOCOL_INPUT_COLOR          0x181818U
#define GUI_PROTOCOL_LINE_COLOR           0x3A3A40U
#define GUI_PROTOCOL_ACCENT_COLOR         0x2F80EDU
#define GUI_PROTOCOL_PAGE_WIDTH           138
#define GUI_PROTOCOL_DIALOG_PADDING       14
#define GUI_PROTOCOL_DIALOG_HEADER_HEIGHT 50
#define GUI_PROTOCOL_DIALOG_FOOTER_HEIGHT 54
#define GUI_PROTOCOL_DIALOG_VALUE_WIDTH   172
#define GUI_PROTOCOL_DIALOG_ROW_HEIGHT    39
#define GUI_PROTOCOL_DISPLAY_RECORD_LIMIT 512U
#define GUI_PROTOCOL_DISPLAY_ROWS         2U

/* ================================================== */

typedef enum
{
    GUI_PROTOCOL_KIND_UART = 0,
    GUI_PROTOCOL_KIND_I2C,
    GUI_PROTOCOL_KIND_SPI,
    GUI_PROTOCOL_KIND_COUNT
} GUI_Protocol_Kind_t;

typedef struct
{
    lv_obj_t *clear_root;             /* 先于对话框显示的Layer0不透明黑色清屏层 */
    lv_obj_t *root;                   /* 覆盖波形区的Layer0对话框根对象 */
    lv_obj_t *enabled_switch;         /* 当前协议的启用开关 */
    lv_obj_t *display_dropdown;       /* 当前协议的 HEX 或 ASCII 显示类型 */
    lv_obj_t *error_label;            /* 参数不兼容时保留窗口，并在页脚提示原因 */
    lv_timer_t *open_timer;           /* 清屏帧完成后创建设置窗的一次性定时器 */
    lv_obj_t *uart_rx_dropdown;       /* UART RX通道选择，0表示未选择 */
    lv_obj_t *uart_tx_dropdown;       /* UART TX通道选择，0表示未选择 */
    lv_obj_t *channel_dropdowns[4];   /* I2C/SPI的物理通道选择框 */
    lv_obj_t *parameter_dropdowns[5]; /* 当前协议参数选择框 */
    uint8_t uart_rx_channel;          /* 0-未选择，1~6-D1~D6 */
    uint8_t uart_tx_channel;          /* 0-未选择，1~6-D1~D6 */
    GUI_Protocol_Kind_t kind;         /* 当前协议 */
} GUI_Protocol_Dialog_t;

typedef struct
{
    bool rendered_range_valid;
    uint32_t session_id;
    uint32_t rendered_start_cnt;
    uint32_t rendered_end_cnt;
} GUI_Protocol_View_t;

/* ================================================== */

static GUI_Protocol_Dialog_t gui_protocol_dialog;
static Wave_Protocol_Display_Format_t gui_protocol_display_formats[GUI_PROTOCOL_KIND_COUNT];
static GUI_Protocol_View_t gui_protocol_view;
static GUI_Protocol_Get_Config_Callback_t gui_protocol_get_config_callback;
static GUI_Protocol_Set_Config_Callback_t gui_protocol_set_config_callback;
static void *gui_protocol_config_context;
static Protocol_Record_t gui_protocol_read_buffer[PROTOCOL_RECORD_READ_LIMIT];
static uint8_t gui_protocol_pixel_coverage[GUI_PROTOCOL_DISPLAY_ROWS][WAVE_AREA_WIDTH];

/* 首项为“-”（不使用该方向）；其余项为六路逻辑分析通道 */
static const char *const gui_protocol_uart_channel_options[7] = {
    "-\nD1: 通道1\nD2: 通道2\nD3: 通道3\nD4: 通道4\nD5: 通道5\nD6: 通道6",
    "-\nD2: 通道2\nD3: 通道3\nD4: 通道4\nD5: 通道5\nD6: 通道6",
    "-\nD1: 通道1\nD3: 通道3\nD4: 通道4\nD5: 通道5\nD6: 通道6",
    "-\nD1: 通道1\nD2: 通道2\nD4: 通道4\nD5: 通道5\nD6: 通道6",
    "-\nD1: 通道1\nD2: 通道2\nD3: 通道3\nD5: 通道5\nD6: 通道6",
    "-\nD1: 通道1\nD2: 通道2\nD3: 通道3\nD4: 通道4\nD6: 通道6",
    "-\nD1: 通道1\nD2: 通道2\nD3: 通道3\nD4: 通道4\nD5: 通道5"};

static const char *const gui_protocol_uart_channel_options_en[7] = {
    "-\nD1: Channel 1\nD2: Channel 2\nD3: Channel 3\nD4: Channel 4\nD5: Channel 5\nD6: Channel 6",
    "-\nD2: Channel 2\nD3: Channel 3\nD4: Channel 4\nD5: Channel 5\nD6: Channel 6",
    "-\nD1: Channel 1\nD3: Channel 3\nD4: Channel 4\nD5: Channel 5\nD6: Channel 6",
    "-\nD1: Channel 1\nD2: Channel 2\nD4: Channel 4\nD5: Channel 5\nD6: Channel 6",
    "-\nD1: Channel 1\nD2: Channel 2\nD3: Channel 3\nD5: Channel 5\nD6: Channel 6",
    "-\nD1: Channel 1\nD2: Channel 2\nD3: Channel 3\nD4: Channel 4\nD6: Channel 6",
    "-\nD1: Channel 1\nD2: Channel 2\nD3: Channel 3\nD4: Channel 4\nD5: Channel 5"};

/* 菜单文字由数值表生成，避免新增波特率后显示顺序与实际参数错位 */
static const uint32_t gui_protocol_uart_baud_rates[] = {110U,
                                                        300U,
                                                        600U,
                                                        1200U,
                                                        2400U,
                                                        4800U,
                                                        9600U,
                                                        14400U,
                                                        19200U,
                                                        38400U,
                                                        57600U,
                                                        74880U,
                                                        115200U,
                                                        128000U,
                                                        230400U,
                                                        256000U,
                                                        460800U,
                                                        500000U,
                                                        512000U,
                                                        600000U,
                                                        921600U,
                                                        1000000U,
                                                        1500000U,
                                                        2000000U};

static char gui_protocol_uart_baud_options[sizeof(gui_protocol_uart_baud_rates) /
                                           sizeof(gui_protocol_uart_baud_rates[0]) * 9U];

/* ================================================== */

static void GUI_Protocol_Page_Button_Event_Callback(lv_event_t *event);
static void GUI_Protocol_Dialog_Close_Event_Callback(lv_event_t *event);
static void GUI_Protocol_Dialog_Confirm_Event_Callback(lv_event_t *event);
static void GUI_Protocol_Dialog_Open(GUI_Protocol_Kind_t kind);
static void GUI_Protocol_Dialog_Prepare_Clear(GUI_Protocol_Kind_t kind);
static void GUI_Protocol_Dialog_Open_Timer_Callback(lv_timer_t *timer);
static void GUI_Protocol_Dialog_Close(void);
static void GUI_Protocol_Dialog_UART_Channel_Event_Callback(lv_event_t *event);
static void GUI_Protocol_Dialog_UART_Update_Channel_Options(void);
static void GUI_Protocol_Dialog_Build_UART(lv_obj_t *parent, int32_t first_y);
static void GUI_Protocol_Dialog_Build_I2C(lv_obj_t *parent, int32_t first_y);
static void GUI_Protocol_Dialog_Build_SPI(lv_obj_t *parent, int32_t first_y);
static void GUI_Protocol_Dialog_Restore_Selection(void);
static uint8_t GUI_Protocol_Dialog_Channel_Dropdown_Value(const lv_obj_t *dropdown);
static bool GUI_Protocol_Render_Records(const Protocol_Info_t *info, const Wave_Cnt_Range_t *range);
static bool GUI_Protocol_Count_Visible(const Protocol_Info_t *info,
                                       const Wave_Cnt_Range_t *range,
                                       uint32_t *visible_count);
static bool GUI_Protocol_Render_Compact(const Protocol_Info_t *info, const Wave_Cnt_Range_t *range);
static uint8_t GUI_Protocol_Record_Row_Mask(const Protocol_Info_t *info,
                                            const Protocol_Record_t *record);
static uint8_t GUI_Protocol_Row_For_Channel(LA_Service_Channel_t channel,
                                            LA_Service_Channel_t first,
                                            LA_Service_Channel_t second);
static lv_obj_t *GUI_Protocol_Dialog_Dropdown(lv_obj_t *parent,
                                              int32_t y,
                                              const char *options,
                                              uint32_t selected);
static lv_obj_t *GUI_Protocol_Dialog_Row(lv_obj_t *parent,
                                         const char *name,
                                         int32_t y,
                                         const char *options,
                                         uint32_t selected);
static uint8_t GUI_Protocol_Dialog_UART_Dropdown_To_Channel(uint32_t selected,
                                                            uint8_t excluded_channel);
static uint32_t GUI_Protocol_Dialog_UART_Channel_To_Dropdown(uint8_t channel,
                                                             uint8_t excluded_channel);

/* ================================================== */

/**
 * @brief  创建 UART、I2C 和 SPI 解码设置入口页
 */
void GUI_Protocol_Page_Create(lv_obj_t *page_parent)
{
    static const char *const names[GUI_PROTOCOL_KIND_COUNT] = {"UART", "I2C", "SPI"};
    static const int32_t button_x[GUI_PROTOCOL_KIND_COUNT] = {8, 57, 98};
    static const int32_t button_width[GUI_PROTOCOL_KIND_COUNT] = {43, 35, 34};
    lv_obj_t *page;
    lv_obj_t *title;
    lv_obj_t *subtitle;
    uint32_t index;

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
    lv_obj_set_pos(page, 0, 0);
    lv_obj_set_size(page, lv_obj_get_width(page_parent), lv_obj_get_height(page_parent));
    lv_obj_set_style_bg_color(page, lv_color_hex(GUI_PROTOCOL_PANEL_COLOR), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(page, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_scrollable(page, false);
    lv_obj_set_style_text_font(page, APP_FONT_CN_14_PTR, LV_PART_MAIN);

    title = lv_label_create(page);
    if (title != NULL)
    {
        lv_label_set_text(title, GUI_TEXT("协议解析", "Protocols"));
        lv_obj_set_pos(title, 8, 16);
        lv_obj_set_style_text_font(title, APP_FONT_CN_14_PTR, LV_PART_MAIN);
        lv_obj_set_style_text_color(title,
                                    lv_color_hex(GUI_PROTOCOL_PAGE_TITLE_COLOR),
                                    LV_PART_MAIN);
    }
    subtitle = lv_label_create(page);
    if (subtitle != NULL)
    {
        lv_label_set_text(subtitle, GUI_TEXT("协议选择", "Decode"));
        lv_obj_set_pos(subtitle, 8, 53);
        lv_obj_set_style_text_font(subtitle, APP_FONT_CN_14_PTR, LV_PART_MAIN);
        lv_obj_set_style_text_color(subtitle, lv_color_hex(GUI_PROTOCOL_TEXT_COLOR), LV_PART_MAIN);
    }

    for (index = 0U; index < GUI_PROTOCOL_KIND_COUNT; index++)
    {
        lv_obj_t *button = lv_button_create(page);
        lv_obj_t *label;
        const int32_t width = button_width[index];
        const int32_t x = button_x[index];

        if (button == NULL)
        {
            continue;
        }
        lv_obj_set_pos(button, x, 83);
        lv_obj_set_size(button, width, 29);
        lv_obj_set_style_radius(button, 15, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(button, 1, LV_PART_MAIN);
        lv_obj_set_style_border_color(button, lv_color_hex(GUI_PROTOCOL_MUTED_COLOR), LV_PART_MAIN);
        lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(button, 0, LV_PART_MAIN);
        lv_obj_add_event_cb(button,
                            GUI_Protocol_Page_Button_Event_Callback,
                            LV_EVENT_CLICKED,
                            (void *)(uintptr_t)index);

        label = lv_label_create(button);
        if (label != NULL)
        {
            lv_label_set_text(label, names[index]);
            lv_obj_set_style_text_font(label, APP_FONT_CN_14_PTR, LV_PART_MAIN);
            lv_obj_set_style_text_color(label,
                                        lv_color_hex(GUI_PROTOCOL_MUTED_COLOR),
                                        LV_PART_MAIN);
            lv_obj_center(label);
        }
    }
}

/**
 * @brief  导航切页时撤销未确认的协议设置窗口
 */
void GUI_Protocol_Close_Dialog(void)
{
    GUI_Protocol_Dialog_Close();
}

/* ================================================== */

/**
 * @brief  确定某物理数据线在顶部两行结果区的行号
 */
static uint8_t GUI_Protocol_Row_For_Channel(LA_Service_Channel_t channel,
                                            LA_Service_Channel_t first,
                                            LA_Service_Channel_t second)
{
    if (first == PROTOCOL_CHANNEL_UNUSED)
    {
        return 0U;
    }
    if (second == PROTOCOL_CHANNEL_UNUSED)
    {
        return 0U;
    }
    return channel == ((uint32_t)first < (uint32_t)second ? first : second) ? 0U : 1U;
}

/**
 * @brief  计算一条协议记录应覆盖的结果行位图
 */
static uint8_t GUI_Protocol_Record_Row_Mask(const Protocol_Info_t *info,
                                            const Protocol_Record_t *record)
{
    if (info == NULL || record == NULL)
    {
        return 0U;
    }

    if (info->protocol == PROTOCOL_TYPE_UART && record->type == PROTOCOL_UART_RECORD_DATA)
    {
        const LA_Service_Channel_t channel = (record->flags & PROTOCOL_UART_FLAG_TX) != 0U ?
                                                 info->config.decoder.uart.tx_channel :
                                                 info->config.decoder.uart.rx_channel;
        const uint8_t row = GUI_Protocol_Row_For_Channel(channel,
                                                         info->config.decoder.uart.tx_channel,
                                                         info->config.decoder.uart.rx_channel);
        return (uint8_t)(1U << row);
    }
    if (info->protocol == PROTOCOL_TYPE_IIC &&
        (record->type == PROTOCOL_IIC_RECORD_ADDRESS || record->type == PROTOCOL_IIC_RECORD_DATA))
    {
        return 0x01U;
    }
    if (info->protocol == PROTOCOL_TYPE_SPI && record->type == PROTOCOL_SPI_RECORD_WORD)
    {
        const Protocol_SPI_Config_t *spi = &info->config.decoder.spi;
        uint8_t mask = 0U;

        if (spi->miso_channel != PROTOCOL_CHANNEL_UNUSED)
        {
            mask |= (uint8_t)(1U << GUI_Protocol_Row_For_Channel(spi->miso_channel,
                                                                 spi->miso_channel,
                                                                 spi->mosi_channel));
        }
        if (spi->mosi_channel != PROTOCOL_CHANNEL_UNUSED)
        {
            mask |= (uint8_t)(1U << GUI_Protocol_Row_For_Channel(spi->mosi_channel,
                                                                 spi->miso_channel,
                                                                 spi->mosi_channel));
        }
        return mask;
    }
    return 0U;
}

/**
 * @brief  按当前 CNT 窗口统计实际需要显示的协议块数量
 * @note   读取绑定 session_id，防止混入其他会话
 */
static bool GUI_Protocol_Count_Visible(const Protocol_Info_t *info,
                                       const Wave_Cnt_Range_t *range,
                                       uint32_t *visible_count)
{
    uint32_t read_index;
    bool complete = false;

    if (info == NULL || range == NULL || visible_count == NULL || range->cnt_start > range->cnt_end)
    {
        return false;
    }
    *visible_count = 0U;
    if (Protocol_Service_Find_First_Record_For_Session(info->session_id,
                                                       range->cnt_start,
                                                       &read_index) != PROTOCOL_OK)
    {
        return false;
    }

    while (read_index < info->result_count && !complete)
    {
        uint16_t count = 0U;
        uint16_t index;

        if (Protocol_Service_Get_Records_For_Session(info->session_id,
                                                     read_index,
                                                     gui_protocol_read_buffer,
                                                     PROTOCOL_RECORD_READ_LIMIT,
                                                     &count) != PROTOCOL_OK ||
            count == 0U)
        {
            return false;
        }
        for (index = 0U; index < count; index++)
        {
            const Protocol_Record_t *record = &gui_protocol_read_buffer[index];
            uint8_t rows;

            if (record->start_tick > range->cnt_end)
            {
                complete = true;
                break;
            }
            if (record->end_tick < range->cnt_start)
            {
                continue;
            }
            rows = GUI_Protocol_Record_Row_Mask(info, record);
            *visible_count += (uint32_t)(rows & 0x01U) + (uint32_t)((rows >> 1U) & 0x01U);
            if (*visible_count > GUI_PROTOCOL_DISPLAY_RECORD_LIMIT)
            {
                return true;
            }
        }
        read_index += count;
    }
    return true;
}

/**
 * @brief  把当前时间范围内的协议记录画成两行色块
 * @note   每行最多 500 列，相邻记录占用同一列时合并显示
 */
static bool GUI_Protocol_Render_Compact(const Protocol_Info_t *info, const Wave_Cnt_Range_t *range)
{
    uint32_t read_index;
    uint32_t window_cnt;
    bool complete = false;
    uint8_t row;

    if (info == NULL || range == NULL || range->cnt_start > range->cnt_end)
    {
        return false;
    }
    window_cnt = range->cnt_end - range->cnt_start;
    if (window_cnt == 0U)
    {
        window_cnt = 1U;
    }
    memset(gui_protocol_pixel_coverage, 0, sizeof(gui_protocol_pixel_coverage));
    if (Protocol_Service_Find_First_Record_For_Session(info->session_id,
                                                       range->cnt_start,
                                                       &read_index) != PROTOCOL_OK)
    {
        return false;
    }

    while (read_index < info->result_count && !complete)
    {
        uint16_t count = 0U;
        uint16_t index;

        if (Protocol_Service_Get_Records_For_Session(info->session_id,
                                                     read_index,
                                                     gui_protocol_read_buffer,
                                                     PROTOCOL_RECORD_READ_LIMIT,
                                                     &count) != PROTOCOL_OK ||
            count == 0U)
        {
            return false;
        }
        for (index = 0U; index < count; index++)
        {
            const Protocol_Record_t *record = &gui_protocol_read_buffer[index];
            uint32_t clipped_start;
            uint32_t clipped_end;
            uint32_t first_column;
            uint32_t last_column;
            uint8_t rows;

            if (record->start_tick > range->cnt_end)
            {
                complete = true;
                break;
            }
            if (record->end_tick < range->cnt_start)
            {
                continue;
            }
            rows = GUI_Protocol_Record_Row_Mask(info, record);
            if (rows == 0U)
            {
                continue;
            }
            clipped_start =
                record->start_tick < range->cnt_start ? range->cnt_start : record->start_tick;
            clipped_end = record->end_tick > range->cnt_end ? range->cnt_end : record->end_tick;
            first_column =
                (uint32_t)(((uint64_t)(clipped_start - range->cnt_start) * WAVE_AREA_WIDTH) /
                           window_cnt);
            last_column =
                (uint32_t)(((uint64_t)(clipped_end - range->cnt_start) * WAVE_AREA_WIDTH) /
                           window_cnt);
            if (first_column >= WAVE_AREA_WIDTH)
            {
                first_column = WAVE_AREA_WIDTH - 1U;
            }
            if (last_column >= WAVE_AREA_WIDTH)
            {
                last_column = WAVE_AREA_WIDTH - 1U;
            }
            for (row = 0U; row < GUI_PROTOCOL_DISPLAY_ROWS; row++)
            {
                uint32_t column;
                if ((rows & (uint8_t)(1U << row)) == 0U)
                {
                    continue;
                }
                for (column = first_column; column <= last_column; column++)
                {
                    gui_protocol_pixel_coverage[row][column] = 1U;
                }
            }
        }
        read_index += count;
    }

    Wave_Protocol_Results_Clear();
    for (row = 0U; row < GUI_PROTOCOL_DISPLAY_ROWS; row++)
    {
        uint32_t column = 0U;
        while (column < WAVE_AREA_WIDTH)
        {
            uint32_t first_column;
            uint32_t after_column;
            uint32_t start_cnt;
            uint32_t end_cnt;

            while (column < WAVE_AREA_WIDTH && gui_protocol_pixel_coverage[row][column] == 0U)
            {
                column++;
            }
            if (column >= WAVE_AREA_WIDTH)
            {
                break;
            }
            first_column = column;
            while (column < WAVE_AREA_WIDTH && gui_protocol_pixel_coverage[row][column] != 0U)
            {
                column++;
            }
            after_column = column;
            start_cnt = range->cnt_start +
                        (uint32_t)(((uint64_t)first_column * window_cnt) / WAVE_AREA_WIDTH);
            end_cnt = range->cnt_start +
                      (uint32_t)((((uint64_t)after_column * window_cnt) + WAVE_AREA_WIDTH - 1U) /
                                 WAVE_AREA_WIDTH);
            if (end_cnt > range->cnt_end)
            {
                end_cnt = range->cnt_end;
            }
            if (end_cnt < start_cnt)
            {
                end_cnt = start_cnt;
            }
            if (!Wave_Protocol_Compact_Block_Add(row, start_cnt, end_cnt))
            {
                return false;
            }
        }
    }
    return true;
}

/**
 * @brief  按当前时间窗口把协议服务记录复制到波形绘制缓存
 * @note   超过 512 块时改用像素覆盖图
 */
static bool GUI_Protocol_Render_Records(const Protocol_Info_t *info, const Wave_Cnt_Range_t *range)
{
    uint32_t read_index;
    uint32_t visible_count;
    bool complete = false;
    bool display_full = false;

    if (info == NULL || range == NULL || range->cnt_start > range->cnt_end)
    {
        return false;
    }
    if (!GUI_Protocol_Count_Visible(info, range, &visible_count))
    {
        return false;
    }
    if (visible_count > GUI_PROTOCOL_DISPLAY_RECORD_LIMIT)
    {
        return GUI_Protocol_Render_Compact(info, range);
    }
    if (Protocol_Service_Find_First_Record_For_Session(info->session_id,
                                                       range->cnt_start,
                                                       &read_index) != PROTOCOL_OK)
    {
        return false;
    }
    Wave_Protocol_Results_Clear();
    while (read_index < info->result_count && !complete && !display_full)
    {
        uint16_t count = 0U;
        uint16_t index;

        if (Protocol_Service_Get_Records_For_Session(info->session_id,
                                                     read_index,
                                                     gui_protocol_read_buffer,
                                                     PROTOCOL_RECORD_READ_LIMIT,
                                                     &count) != PROTOCOL_OK ||
            count == 0U)
        {
            return false;
        }
        for (index = 0U; index < count; index++)
        {
            const Protocol_Record_t *record = &gui_protocol_read_buffer[index];
            bool added = true;

            if (record->start_tick > range->cnt_end)
            {
                complete = true;
                break;
            }
            if (record->end_tick < range->cnt_start)
            {
                continue;
            }

            if (info->protocol == PROTOCOL_TYPE_UART && record->type == PROTOCOL_UART_RECORD_DATA)
            {
                const LA_Service_Channel_t channel = (record->flags & PROTOCOL_UART_FLAG_TX) != 0U ?
                                                         info->config.decoder.uart.tx_channel :
                                                         info->config.decoder.uart.rx_channel;
                const uint8_t row =
                    GUI_Protocol_Row_For_Channel(channel,
                                                 info->config.decoder.uart.tx_channel,
                                                 info->config.decoder.uart.rx_channel);
                added = Wave_Protocol_UART_Result_Add(row,
                                                      record->start_tick,
                                                      record->end_tick,
                                                      record->value,
                                                      info->config.decoder.uart.data_bits,
                                                      info->config.decoder.uart.parity !=
                                                          PROTOCOL_UART_PARITY_NONE,
                                                      info->config.decoder.uart.stop_bits);
            }
            else if (info->protocol == PROTOCOL_TYPE_IIC &&
                     record->type == PROTOCOL_IIC_RECORD_ADDRESS)
            {
                added =
                    Wave_Protocol_IIC_Address_Add(0U,
                                                  record->start_tick,
                                                  record->end_tick,
                                                  record->value,
                                                  (uint8_t)record->aux,
                                                  (record->flags & PROTOCOL_IIC_FLAG_READ) != 0U);
            }
            else if (info->protocol == PROTOCOL_TYPE_IIC &&
                     record->type == PROTOCOL_IIC_RECORD_DATA)
            {
                added = Wave_Protocol_IIC_Data_Add(0U,
                                                   record->start_tick,
                                                   record->end_tick,
                                                   record->value,
                                                   (record->flags & PROTOCOL_IIC_FLAG_READ) != 0U);
            }
            else if (info->protocol == PROTOCOL_TYPE_SPI &&
                     record->type == PROTOCOL_SPI_RECORD_WORD)
            {
                const Protocol_SPI_Config_t *spi = &info->config.decoder.spi;
                const uint8_t digits = (uint8_t)((spi->word_bits + 3U) / 4U);
                if (spi->miso_channel != PROTOCOL_CHANNEL_UNUSED)
                {
                    added =
                        Wave_Protocol_Result_Add(GUI_Protocol_Row_For_Channel(spi->miso_channel,
                                                                              spi->miso_channel,
                                                                              spi->mosi_channel),
                                                 record->start_tick,
                                                 record->end_tick,
                                                 record->value >> 16U,
                                                 digits);
                }
                if (added && spi->mosi_channel != PROTOCOL_CHANNEL_UNUSED)
                {
                    added =
                        Wave_Protocol_Result_Add(GUI_Protocol_Row_For_Channel(spi->mosi_channel,
                                                                              spi->miso_channel,
                                                                              spi->mosi_channel),
                                                 record->start_tick,
                                                 record->end_tick,
                                                 record->value & 0xFFFFU,
                                                 digits);
                }
            }
            if (!added)
            {
                display_full = true;
                break;
            }
        }
        read_index += count;
    }
    return true;
}

/* ================================================== */

/**
 * @brief  绑定协议配置读取与确认请求
 */
void GUI_Protocol_Bind_Config(GUI_Protocol_Get_Config_Callback_t get_callback,
                              GUI_Protocol_Set_Config_Callback_t set_callback,
                              void *context)
{
    gui_protocol_get_config_callback = get_callback;
    gui_protocol_set_config_callback = set_callback;
    gui_protocol_config_context = context;
}

/**
 * @brief  清空已绘制的协议结果
 */
void GUI_Protocol_Clear_Results(void)
{
    gui_protocol_view.rendered_range_valid = false;
    Wave_Protocol_Results_Clear();
}

/**
 * @brief  在当前可见时间范围内显示选中的协议解析结果
 */
void GUI_Protocol_Update_Results(const Protocol_Info_t *info)
{
    Wave_Cnt_Range_t range;

    if (info == NULL || !Wave_Protocol_Layout_Is_Active() || !Wave_Get_View_Range(&range) ||
        (info->state != PROTOCOL_STATE_COMPLETE && info->state != PROTOCOL_STATE_TRUNCATED))
    {
        return;
    }
    if (gui_protocol_view.session_id != info->session_id)
    {
        gui_protocol_view.session_id = info->session_id;
        gui_protocol_view.rendered_range_valid = false;
    }
    if (gui_protocol_view.rendered_range_valid &&
        range.cnt_start == gui_protocol_view.rendered_start_cnt &&
        range.cnt_end == gui_protocol_view.rendered_end_cnt)
    {
        return;
    }
    if (GUI_Protocol_Render_Records(info, &range))
    {
        gui_protocol_view.rendered_start_cnt = range.cnt_start;
        gui_protocol_view.rendered_end_cnt = range.cnt_end;
        gui_protocol_view.rendered_range_valid = true;
    }
}

/* ================================================== */

/**
 * @brief  根据协议入口按钮的编号安排打开设置弹窗
 */
static void GUI_Protocol_Page_Button_Event_Callback(lv_event_t *event)
{
    GUI_Protocol_Dialog_Prepare_Clear(
        (GUI_Protocol_Kind_t)(uintptr_t)lv_event_get_user_data(event));
}

/**
 * @brief  创建深色协议参数下拉框并设置当前选项
 */
static lv_obj_t *GUI_Protocol_Dialog_Dropdown(lv_obj_t *parent,
                                              int32_t y,
                                              const char *options,
                                              uint32_t selected)
{
    lv_obj_t *dropdown = lv_dropdown_create(parent);
    lv_obj_t *list;

    if (dropdown == NULL)
    {
        return NULL;
    }
    lv_obj_set_pos(dropdown,
                   WAVE_AREA_WIDTH - GUI_PROTOCOL_DIALOG_PADDING - GUI_PROTOCOL_DIALOG_VALUE_WIDTH,
                   y);
    lv_obj_set_size(dropdown, GUI_PROTOCOL_DIALOG_VALUE_WIDTH, 31);
    lv_dropdown_set_options_static(dropdown, options);
    lv_dropdown_set_selected(dropdown, selected);
    lv_dropdown_set_symbol(dropdown, LV_SYMBOL_DOWN);
    lv_obj_set_style_bg_color(dropdown, lv_color_hex(GUI_PROTOCOL_INPUT_COLOR), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(dropdown, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(dropdown, 0, LV_PART_MAIN);
    lv_obj_set_style_text_color(dropdown, lv_color_hex(GUI_PROTOCOL_TEXT_COLOR), LV_PART_MAIN);
    lv_obj_set_style_text_font(dropdown, APP_FONT_CN_14_PTR, LV_PART_MAIN);
    /* INDICATOR 使用含 LV_SYMBOL_DOWN 的默认字体 */
    lv_obj_set_style_text_font(dropdown, LV_FONT_DEFAULT, LV_PART_INDICATOR);
    lv_obj_set_style_text_color(dropdown, lv_color_hex(GUI_PROTOCOL_TEXT_COLOR), LV_PART_INDICATOR);
    /* 展开列表不继承弹窗字体，需单独设置中文字体 */
    list = lv_dropdown_get_list(dropdown);
    lv_obj_set_style_text_font(list, APP_FONT_CN_14_PTR, LV_PART_MAIN);
    lv_obj_set_style_text_font(list, APP_FONT_CN_14_PTR, LV_PART_SELECTED);
    return dropdown;
}

/**
 * @brief  创建参数标签与右侧下拉框
 */
static lv_obj_t *GUI_Protocol_Dialog_Row(lv_obj_t *parent,
                                         const char *name,
                                         int32_t y,
                                         const char *options,
                                         uint32_t selected)
{
    lv_obj_t *label = lv_label_create(parent);

    if (label != NULL)
    {
        lv_label_set_text(label, name);
        lv_obj_set_pos(label, GUI_PROTOCOL_DIALOG_PADDING, y + 6);
        lv_obj_set_width(label, WAVE_AREA_WIDTH - GUI_PROTOCOL_DIALOG_VALUE_WIDTH - 42);
        lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_font(label, APP_FONT_CN_14_PTR, LV_PART_MAIN);
        lv_obj_set_style_text_color(label, lv_color_hex(GUI_PROTOCOL_TEXT_COLOR), LV_PART_MAIN);
    }
    return GUI_Protocol_Dialog_Dropdown(parent, y, options, selected);
}

/**
 * @brief  把排除另一路通道的下拉索引映射为物理通道号
 * @note   调用者保证索引与排除列表匹配
 */
static uint8_t GUI_Protocol_Dialog_UART_Dropdown_To_Channel(uint32_t selected,
                                                            uint8_t excluded_channel)
{
    if (selected == 0U)
    {
        return 0U;
    }
    if (excluded_channel == 0U || selected < excluded_channel)
    {
        return (uint8_t)selected;
    }
    return (uint8_t)(selected + 1U);
}

/**
 * @brief  把物理通道号映射为排除另一路通道后的菜单索引
 * @note   调用者保证目标通道未被另一方向占用
 */
static uint32_t GUI_Protocol_Dialog_UART_Channel_To_Dropdown(uint8_t channel,
                                                             uint8_t excluded_channel)
{
    if (channel == 0U)
    {
        return 0U;
    }
    if (excluded_channel == 0U || channel < excluded_channel)
    {
        return channel;
    }
    return (uint32_t)channel - 1U;
}

/**
 * @brief  读取普通物理通道菜单当前通道号
 */
static uint8_t GUI_Protocol_Dialog_Channel_Dropdown_Value(const lv_obj_t *dropdown)
{
    if (dropdown == NULL)
    {
        return 0U;
    }
    return (uint8_t)lv_dropdown_get_selected(dropdown);
}

/**
 * @brief  按当前 RX/TX 选择相互排除对方已占用的通道
 */
static void GUI_Protocol_Dialog_UART_Update_Channel_Options(void)
{
    if (gui_protocol_dialog.uart_rx_dropdown != NULL)
    {
        lv_dropdown_set_options_static(
            gui_protocol_dialog.uart_rx_dropdown,
            GUI_TEXT(gui_protocol_uart_channel_options[gui_protocol_dialog.uart_tx_channel],
                     gui_protocol_uart_channel_options_en[gui_protocol_dialog.uart_tx_channel]));
        lv_dropdown_set_selected(
            gui_protocol_dialog.uart_rx_dropdown,
            GUI_Protocol_Dialog_UART_Channel_To_Dropdown(gui_protocol_dialog.uart_rx_channel,
                                                         gui_protocol_dialog.uart_tx_channel));
    }
    if (gui_protocol_dialog.uart_tx_dropdown != NULL)
    {
        lv_dropdown_set_options_static(
            gui_protocol_dialog.uart_tx_dropdown,
            GUI_TEXT(gui_protocol_uart_channel_options[gui_protocol_dialog.uart_rx_channel],
                     gui_protocol_uart_channel_options_en[gui_protocol_dialog.uart_rx_channel]));
        lv_dropdown_set_selected(
            gui_protocol_dialog.uart_tx_dropdown,
            GUI_Protocol_Dialog_UART_Channel_To_Dropdown(gui_protocol_dialog.uart_tx_channel,
                                                         gui_protocol_dialog.uart_rx_channel));
    }
}

/**
 * @brief  保存 UART 某方向的通道选择并更新另一方向菜单
 */
static void GUI_Protocol_Dialog_UART_Channel_Event_Callback(lv_event_t *event)
{
    const uint8_t is_tx = (uint8_t)(uintptr_t)lv_event_get_user_data(event);
    const uint8_t excluded_channel =
        is_tx ? gui_protocol_dialog.uart_rx_channel : gui_protocol_dialog.uart_tx_channel;
    const uint8_t channel = GUI_Protocol_Dialog_UART_Dropdown_To_Channel(
        lv_dropdown_get_selected(lv_event_get_target(event)),
        excluded_channel);

    if (is_tx != 0U)
    {
        gui_protocol_dialog.uart_tx_channel = channel;
    }
    else
    {
        gui_protocol_dialog.uart_rx_channel = channel;
    }
    GUI_Protocol_Dialog_UART_Update_Channel_Options();
}

/**
 * @brief  创建 UART 参数字段并缓存本次弹窗的控件句柄
 */
static void GUI_Protocol_Dialog_Build_UART(lv_obj_t *parent, int32_t first_y)
{
    uint32_t baud_index;
    uint32_t baud_offset = 0U;
    uint32_t selected_baud = 0U;

    for (baud_index = 0U; baud_index < sizeof(gui_protocol_uart_baud_rates) /
                                           sizeof(gui_protocol_uart_baud_rates[0]);
         baud_index++)
    {
        const int written = lv_snprintf(gui_protocol_uart_baud_options + baud_offset,
                                        sizeof(gui_protocol_uart_baud_options) - baud_offset,
                                        "%s%lu",
                                        baud_index == 0U ? "" : "\n",
                                        (unsigned long)gui_protocol_uart_baud_rates[baud_index]);
        if (written <= 0 ||
            (uint32_t)written >= sizeof(gui_protocol_uart_baud_options) - baud_offset)
        {
            (void)Log_Printf("ui: UART baud option buffer exhausted\r\n");
            return;
        }
        baud_offset += (uint32_t)written;
        if (gui_protocol_uart_baud_rates[baud_index] == 115200U)
        {
            selected_baud = baud_index;
        }
    }
    gui_protocol_dialog.uart_rx_channel = 0U;
    gui_protocol_dialog.uart_tx_channel = 0U;
    memset(gui_protocol_dialog.channel_dropdowns, 0, sizeof(gui_protocol_dialog.channel_dropdowns));
    memset(gui_protocol_dialog.parameter_dropdowns,
           0,
           sizeof(gui_protocol_dialog.parameter_dropdowns));
    gui_protocol_dialog.uart_rx_dropdown = GUI_Protocol_Dialog_Row(
        parent,
        GUI_TEXT("RX（UART接收线）", "RX (UART receive)"),
        first_y + 0 * GUI_PROTOCOL_DIALOG_ROW_HEIGHT,
        GUI_TEXT(gui_protocol_uart_channel_options[0], gui_protocol_uart_channel_options_en[0]),
        0U);
    gui_protocol_dialog.uart_tx_dropdown = GUI_Protocol_Dialog_Row(
        parent,
        GUI_TEXT("TX（UART发送线）", "TX (UART transmit)"),
        first_y + 1 * GUI_PROTOCOL_DIALOG_ROW_HEIGHT,
        GUI_TEXT(gui_protocol_uart_channel_options[0], gui_protocol_uart_channel_options_en[0]),
        0U);
    if (gui_protocol_dialog.uart_rx_dropdown != NULL)
    {
        lv_obj_add_event_cb(gui_protocol_dialog.uart_rx_dropdown,
                            GUI_Protocol_Dialog_UART_Channel_Event_Callback,
                            LV_EVENT_VALUE_CHANGED,
                            (void *)(uintptr_t)0U);
    }
    if (gui_protocol_dialog.uart_tx_dropdown != NULL)
    {
        lv_obj_add_event_cb(gui_protocol_dialog.uart_tx_dropdown,
                            GUI_Protocol_Dialog_UART_Channel_Event_Callback,
                            LV_EVENT_VALUE_CHANGED,
                            (void *)(uintptr_t)1U);
    }
    gui_protocol_dialog.parameter_dropdowns[0] =
        GUI_Protocol_Dialog_Row(parent,
                                GUI_TEXT("波特率", "Baud rate"),
                                first_y + 2 * GUI_PROTOCOL_DIALOG_ROW_HEIGHT,
                                gui_protocol_uart_baud_options,
                                selected_baud);
    gui_protocol_dialog.parameter_dropdowns[1] =
        GUI_Protocol_Dialog_Row(parent,
                                GUI_TEXT("数据位", "Data bits"),
                                first_y + 3 * GUI_PROTOCOL_DIALOG_ROW_HEIGHT,
                                "5\n6\n7\n8\n9",
                                3U);
    gui_protocol_dialog.parameter_dropdowns[2] =
        GUI_Protocol_Dialog_Row(parent,
                                GUI_TEXT("校验位", "Parity"),
                                first_y + 4 * GUI_PROTOCOL_DIALOG_ROW_HEIGHT,
                                GUI_TEXT("无\n偶校验\n奇校验", "None\nEven\nOdd"),
                                0U);
    gui_protocol_dialog.parameter_dropdowns[3] =
        GUI_Protocol_Dialog_Row(parent,
                                GUI_TEXT("停止位", "Stop bits"),
                                first_y + 5 * GUI_PROTOCOL_DIALOG_ROW_HEIGHT,
                                "1\n2",
                                0U);
    gui_protocol_dialog.parameter_dropdowns[4] = GUI_Protocol_Dialog_Row(
        parent,
        GUI_TEXT("信号极性", "Signal polarity"),
        first_y + 6 * GUI_PROTOCOL_DIALOG_ROW_HEIGHT,
        GUI_TEXT("正常（空闲高）\n反相（空闲低）", "Normal (idle high)\nInverted (idle low)"),
        0U);
}

/**
 * @brief  创建 I2C 参数字段并缓存本次弹窗的控件句柄
 */
static void GUI_Protocol_Dialog_Build_I2C(lv_obj_t *parent, int32_t first_y)
{
    gui_protocol_dialog.channel_dropdowns[0] = GUI_Protocol_Dialog_Row(
        parent,
        GUI_TEXT("SCL（串行时钟线）*", "SCL (serial clock)*"),
        first_y + 0 * GUI_PROTOCOL_DIALOG_ROW_HEIGHT,
        GUI_TEXT("-\nD1: 通道1\nD2: 通道2\nD3: 通道3\nD4: 通道4\nD5: 通道5\nD6: 通道6",
                 "-\nD1: Channel 1\nD2: Channel 2\nD3: Channel 3\nD4: Channel 4\nD5: Channel "
                 "5\nD6: Channel 6"),
        0U);
    gui_protocol_dialog.channel_dropdowns[1] = GUI_Protocol_Dialog_Row(
        parent,
        GUI_TEXT("SDA（串行数据线）*", "SDA (serial data)*"),
        first_y + 1 * GUI_PROTOCOL_DIALOG_ROW_HEIGHT,
        GUI_TEXT("-\nD1: 通道1\nD2: 通道2\nD3: 通道3\nD4: 通道4\nD5: 通道5\nD6: 通道6",
                 "-\nD1: Channel 1\nD2: Channel 2\nD3: Channel 3\nD4: Channel 4\nD5: Channel "
                 "5\nD6: Channel 6"),
        0U);
    gui_protocol_dialog.parameter_dropdowns[0] =
        GUI_Protocol_Dialog_Row(parent,
                                GUI_TEXT("地址位数", "Address bits"),
                                first_y + 2 * GUI_PROTOCOL_DIALOG_ROW_HEIGHT,
                                "7\n10",
                                0U);
}

/**
 * @brief  创建 SPI 参数字段并缓存本次弹窗的控件句柄
 */
static void GUI_Protocol_Dialog_Build_SPI(lv_obj_t *parent, int32_t first_y)
{
    gui_protocol_dialog.channel_dropdowns[0] = GUI_Protocol_Dialog_Row(
        parent,
        GUI_TEXT("CLK（时钟）*", "CLK (clock)*"),
        first_y + 0 * GUI_PROTOCOL_DIALOG_ROW_HEIGHT,
        GUI_TEXT("-\nD1: 通道1\nD2: 通道2\nD3: 通道3\nD4: 通道4\nD5: 通道5\nD6: 通道6",
                 "-\nD1: Channel 1\nD2: Channel 2\nD3: Channel 3\nD4: Channel 4\nD5: Channel "
                 "5\nD6: Channel 6"),
        0U);
    gui_protocol_dialog.channel_dropdowns[1] = GUI_Protocol_Dialog_Row(
        parent,
        GUI_TEXT("MISO（主入从出）", "MISO (master in, slave out)"),
        first_y + 1 * GUI_PROTOCOL_DIALOG_ROW_HEIGHT,
        GUI_TEXT("-\nD1: 通道1\nD2: 通道2\nD3: 通道3\nD4: 通道4\nD5: 通道5\nD6: 通道6",
                 "-\nD1: Channel 1\nD2: Channel 2\nD3: Channel 3\nD4: Channel 4\nD5: Channel "
                 "5\nD6: Channel 6"),
        0U);
    gui_protocol_dialog.channel_dropdowns[2] = GUI_Protocol_Dialog_Row(
        parent,
        GUI_TEXT("MOSI（主出从入）", "MOSI (master out, slave in)"),
        first_y + 2 * GUI_PROTOCOL_DIALOG_ROW_HEIGHT,
        GUI_TEXT("-\nD1: 通道1\nD2: 通道2\nD3: 通道3\nD4: 通道4\nD5: 通道5\nD6: 通道6",
                 "-\nD1: Channel 1\nD2: Channel 2\nD3: Channel 3\nD4: Channel 4\nD5: Channel "
                 "5\nD6: Channel 6"),
        0U);
    gui_protocol_dialog.channel_dropdowns[3] = GUI_Protocol_Dialog_Row(
        parent,
        GUI_TEXT("CS#（片选）", "CS# (chip select)"),
        first_y + 3 * GUI_PROTOCOL_DIALOG_ROW_HEIGHT,
        GUI_TEXT("-\nD1: 通道1\nD2: 通道2\nD3: 通道3\nD4: 通道4\nD5: 通道5\nD6: 通道6",
                 "-\nD1: Channel 1\nD2: Channel 2\nD3: Channel 3\nD4: Channel 4\nD5: Channel "
                 "5\nD6: Channel 6"),
        0U);
    gui_protocol_dialog.parameter_dropdowns[0] =
        GUI_Protocol_Dialog_Row(parent,
                                GUI_TEXT("CS#极性", "CS# polarity"),
                                first_y + 4 * GUI_PROTOCOL_DIALOG_ROW_HEIGHT,
                                GUI_TEXT("低有效\n高有效", "Active low\nActive high"),
                                0U);
    gui_protocol_dialog.parameter_dropdowns[1] =
        GUI_Protocol_Dialog_Row(parent,
                                GUI_TEXT("时钟极性", "Clock polarity"),
                                first_y + 5 * GUI_PROTOCOL_DIALOG_ROW_HEIGHT,
                                "0\n1",
                                0U);
    gui_protocol_dialog.parameter_dropdowns[2] =
        GUI_Protocol_Dialog_Row(parent,
                                GUI_TEXT("时钟相位", "Clock phase"),
                                first_y + 6 * GUI_PROTOCOL_DIALOG_ROW_HEIGHT,
                                "0\n1",
                                0U);
    gui_protocol_dialog.parameter_dropdowns[3] =
        GUI_Protocol_Dialog_Row(parent,
                                GUI_TEXT("位顺序", "Bit order"),
                                first_y + 7 * GUI_PROTOCOL_DIALOG_ROW_HEIGHT,
                                GUI_TEXT("MSB优先\nLSB优先", "MSB first\nLSB first"),
                                0U);
    gui_protocol_dialog.parameter_dropdowns[4] =
        GUI_Protocol_Dialog_Row(parent,
                                GUI_TEXT("字长", "Word bits"),
                                first_y + 8 * GUI_PROTOCOL_DIALOG_ROW_HEIGHT,
                                "1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n11\n12\n13\n14\n15\n16",
                                7U);
}

/**
 * @brief  同一协议重新打开设置时恢复最近确认的通道与参数
 */
static void GUI_Protocol_Dialog_Restore_Selection(void)
{
    static const Protocol_Type_t protocols[GUI_PROTOCOL_KIND_COUNT] = {PROTOCOL_TYPE_UART,
                                                                       PROTOCOL_TYPE_IIC,
                                                                       PROTOCOL_TYPE_SPI};
    Protocol_Config_t saved_config;
    const Protocol_Config_t *config = &saved_config;
    uint32_t index;

    if (gui_protocol_get_config_callback == NULL ||
        !gui_protocol_get_config_callback(&saved_config, gui_protocol_config_context) ||
        config->protocol != protocols[gui_protocol_dialog.kind])
    {
        return;
    }
    for (index = 0U; index < (gui_protocol_dialog.kind == GUI_PROTOCOL_KIND_I2C ? 1U : 5U); index++)
    {
        if (gui_protocol_dialog.parameter_dropdowns[index] == NULL)
        {
            return;
        }
    }
    if (gui_protocol_dialog.kind == GUI_PROTOCOL_KIND_UART)
    {
        const Protocol_UART_Config_t *uart = &config->decoder.uart;

        gui_protocol_dialog.uart_rx_channel =
            uart->rx_channel == PROTOCOL_CHANNEL_UNUSED ? 0U : (uint8_t)(uart->rx_channel + 1U);
        gui_protocol_dialog.uart_tx_channel =
            uart->tx_channel == PROTOCOL_CHANNEL_UNUSED ? 0U : (uint8_t)(uart->tx_channel + 1U);
        GUI_Protocol_Dialog_UART_Update_Channel_Options();
        for (index = 0U;
             index < sizeof(gui_protocol_uart_baud_rates) / sizeof(gui_protocol_uart_baud_rates[0]);
             index++)
        {
            if (gui_protocol_uart_baud_rates[index] == uart->baud_rate)
            {
                lv_dropdown_set_selected(gui_protocol_dialog.parameter_dropdowns[0], index);
                break;
            }
        }
        lv_dropdown_set_selected(gui_protocol_dialog.parameter_dropdowns[1], uart->data_bits - 5U);
        lv_dropdown_set_selected(gui_protocol_dialog.parameter_dropdowns[2],
                                 (uint32_t)uart->parity);
        lv_dropdown_set_selected(gui_protocol_dialog.parameter_dropdowns[3], uart->stop_bits - 1U);
        lv_dropdown_set_selected(gui_protocol_dialog.parameter_dropdowns[4], uart->inverted);
        return;
    }
    for (index = 0U; index < (gui_protocol_dialog.kind == GUI_PROTOCOL_KIND_I2C ? 2U : 4U); index++)
    {
        if (gui_protocol_dialog.channel_dropdowns[index] == NULL)
        {
            return;
        }
    }
    if (gui_protocol_dialog.kind == GUI_PROTOCOL_KIND_I2C)
    {
        lv_dropdown_set_selected(gui_protocol_dialog.channel_dropdowns[0],
                                 config->decoder.iic.scl_channel + 1U);
        lv_dropdown_set_selected(gui_protocol_dialog.channel_dropdowns[1],
                                 config->decoder.iic.sda_channel + 1U);
        lv_dropdown_set_selected(gui_protocol_dialog.parameter_dropdowns[0],
                                 config->decoder.iic.address_bits == 10U ? 1U : 0U);
    }
    else
    {
        const Protocol_SPI_Config_t *spi = &config->decoder.spi;
        const LA_Service_Channel_t channels[] = {spi->clk_channel,
                                                 spi->miso_channel,
                                                 spi->mosi_channel,
                                                 spi->cs_channel};

        for (index = 0U; index < 4U; index++)
        {
            lv_dropdown_set_selected(
                gui_protocol_dialog.channel_dropdowns[index],
                channels[index] == PROTOCOL_CHANNEL_UNUSED ? 0U : channels[index] + 1U);
        }
        lv_dropdown_set_selected(gui_protocol_dialog.parameter_dropdowns[0], spi->cs_active_level);
        lv_dropdown_set_selected(gui_protocol_dialog.parameter_dropdowns[1], spi->clock_mode >> 1U);
        lv_dropdown_set_selected(gui_protocol_dialog.parameter_dropdowns[2], spi->clock_mode & 1U);
        lv_dropdown_set_selected(gui_protocol_dialog.parameter_dropdowns[3],
                                 (uint32_t)spi->bit_order);
        lv_dropdown_set_selected(gui_protocol_dialog.parameter_dropdowns[4], spi->word_bits - 1U);
    }
}

/**
 * @brief  隐藏波形硬件层并先显示黑色覆盖层再安排打开弹窗
 * @note   先隐藏波形层，清屏帧完成后打开弹窗
 */
static void GUI_Protocol_Dialog_Prepare_Clear(GUI_Protocol_Kind_t kind)
{
    lv_display_t *display;
    int32_t height;

    if (kind >= GUI_PROTOCOL_KIND_COUNT || lv_display_get_default() == NULL)
    {
        return;
    }
    GUI_Protocol_Dialog_Close();

    /* 波形硬件层位于LVGL Layer0上方；先在VBlank隐藏它才能完成真正清屏 */
    if (!BSP_LTDC_LCD_Overlay_Layer_Set_Visible(false))
    {
        (void)Log_Printf("ui: protocol wave layer hide failed\r\n");
    }

    display = lv_display_get_default();
    height = lv_display_get_vertical_resolution(display);
    if (height <= 0)
    {
        return;
    }

    gui_protocol_dialog.clear_root = lv_obj_create(lv_screen_active());
    if (gui_protocol_dialog.clear_root == NULL)
    {
        return;
    }
    lv_obj_remove_style_all(gui_protocol_dialog.clear_root);
    lv_obj_set_pos(gui_protocol_dialog.clear_root, WAVE_AREA_X, 0);
    lv_obj_set_size(gui_protocol_dialog.clear_root, WAVE_AREA_WIDTH, height);
    lv_obj_set_style_bg_color(gui_protocol_dialog.clear_root,
                              lv_color_hex(0x000000U),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_opa(gui_protocol_dialog.clear_root, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_scrollable(gui_protocol_dialog.clear_root, false);
    lv_obj_move_foreground(gui_protocol_dialog.clear_root);
    lv_obj_invalidate(gui_protocol_dialog.clear_root);

    /* 给下一次LVGL刷新留出一个完整的黑色清屏帧 */
    gui_protocol_dialog.open_timer =
        lv_timer_create(GUI_Protocol_Dialog_Open_Timer_Callback, 20U, (void *)(uintptr_t)kind);
    if (gui_protocol_dialog.open_timer != NULL)
    {
        lv_timer_set_repeat_count(gui_protocol_dialog.open_timer, 1);
    }
}

/**
 * @brief  结束一次性延时并创建对应协议设置窗口
 */
static void GUI_Protocol_Dialog_Open_Timer_Callback(lv_timer_t *timer)
{
    const GUI_Protocol_Kind_t kind = (GUI_Protocol_Kind_t)(uintptr_t)lv_timer_get_user_data(timer);

    gui_protocol_dialog.open_timer = NULL;
    lv_timer_delete(timer);
    GUI_Protocol_Dialog_Open(kind);
}

/**
 * @brief  创建覆盖波形区域的协议设置弹窗
 */
static void GUI_Protocol_Dialog_Open(GUI_Protocol_Kind_t kind)
{
    static const char *const titles[GUI_PROTOCOL_KIND_COUNT] = {"协议设置（UART）",
                                                                "协议设置（I2C）",
                                                                "协议设置（SPI）"};
    static const char *const titles_en[GUI_PROTOCOL_KIND_COUNT] = {"UART settings",
                                                                   "I2C settings",
                                                                   "SPI settings"};
    lv_obj_t *header;
    lv_obj_t *title;
    lv_obj_t *close_button;
    lv_obj_t *close_label;
    lv_obj_t *separator;
    lv_obj_t *body;
    lv_obj_t *footer;
    lv_obj_t *cancel_button;
    lv_obj_t *confirm_button;
    lv_obj_t *label;
    lv_display_t *display;
    int32_t height;
    int32_t first_y;

    if (kind >= GUI_PROTOCOL_KIND_COUNT || lv_display_get_default() == NULL)
    {
        return;
    }
    if (gui_protocol_dialog.root != NULL)
    {
        lv_obj_delete_async(gui_protocol_dialog.root);
        gui_protocol_dialog.root = NULL;
        gui_protocol_dialog.enabled_switch = NULL;
    }
    gui_protocol_dialog.uart_rx_dropdown = NULL;
    gui_protocol_dialog.uart_tx_dropdown = NULL;
    gui_protocol_dialog.display_dropdown = NULL;
    gui_protocol_dialog.error_label = NULL;
    gui_protocol_dialog.uart_rx_channel = 0U;
    gui_protocol_dialog.uart_tx_channel = 0U;
    memset(gui_protocol_dialog.channel_dropdowns, 0, sizeof(gui_protocol_dialog.channel_dropdowns));
    memset(gui_protocol_dialog.parameter_dropdowns,
           0,
           sizeof(gui_protocol_dialog.parameter_dropdowns));

    display = lv_display_get_default();
    height = lv_display_get_vertical_resolution(display);
    if (height <= GUI_PROTOCOL_DIALOG_HEADER_HEIGHT + GUI_PROTOCOL_DIALOG_FOOTER_HEIGHT)
    {
        return;
    }

    gui_protocol_dialog.root = lv_obj_create(lv_screen_active());
    if (gui_protocol_dialog.root == NULL)
    {
        return;
    }
    gui_protocol_dialog.kind = kind;
    lv_obj_remove_style_all(gui_protocol_dialog.root);
    lv_obj_set_pos(gui_protocol_dialog.root, WAVE_AREA_X, 0);
    lv_obj_set_size(gui_protocol_dialog.root, WAVE_AREA_WIDTH, height);
    lv_obj_set_style_bg_color(gui_protocol_dialog.root,
                              lv_color_hex(GUI_PROTOCOL_PANEL_COLOR),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_opa(gui_protocol_dialog.root, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_scrollable(gui_protocol_dialog.root, false);
    lv_obj_set_style_text_font(gui_protocol_dialog.root, APP_FONT_CN_14_PTR, LV_PART_MAIN);
    lv_obj_move_foreground(gui_protocol_dialog.root);

    header = lv_obj_create(gui_protocol_dialog.root);
    if (header == NULL)
    {
        GUI_Protocol_Dialog_Close();
        return;
    }
    lv_obj_remove_style_all(header);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_size(header, WAVE_AREA_WIDTH, GUI_PROTOCOL_DIALOG_HEADER_HEIGHT);
    lv_obj_set_style_bg_color(header, lv_color_hex(GUI_PROTOCOL_HEADER_COLOR), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(header, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_scrollable(header, false);

    title = lv_label_create(header);
    if (title != NULL)
    {
        lv_label_set_text(title, GUI_TEXT(titles[kind], titles_en[kind]));
        lv_obj_set_pos(title, GUI_PROTOCOL_DIALOG_PADDING, 14);
        lv_obj_set_style_text_font(title, APP_FONT_CN_14_PTR, LV_PART_MAIN);
        lv_obj_set_style_text_color(title, lv_color_hex(GUI_PROTOCOL_TEXT_COLOR), LV_PART_MAIN);
    }

    gui_protocol_dialog.enabled_switch = lv_switch_create(header);
    if (gui_protocol_dialog.enabled_switch != NULL)
    {
        lv_obj_set_pos(gui_protocol_dialog.enabled_switch, 167, 12);
        lv_obj_set_size(gui_protocol_dialog.enabled_switch, 48, 27);
        lv_obj_add_state(gui_protocol_dialog.enabled_switch, LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(gui_protocol_dialog.enabled_switch,
                                  lv_color_hex(GUI_PROTOCOL_ACCENT_COLOR),
                                  LV_PART_INDICATOR | LV_STATE_CHECKED);
    }

    close_button = lv_button_create(header);
    if (close_button != NULL)
    {
        lv_obj_set_pos(close_button, WAVE_AREA_WIDTH - 47, 5);
        lv_obj_set_size(close_button, 42, 40);
        lv_obj_set_style_bg_opa(close_button, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(close_button, 0, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(close_button, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(close_button, 0, LV_PART_MAIN);
        lv_obj_add_event_cb(close_button,
                            GUI_Protocol_Dialog_Close_Event_Callback,
                            LV_EVENT_CLICKED,
                            NULL);
        close_label = lv_label_create(close_button);
        if (close_label != NULL)
        {
            lv_label_set_text(close_label, LV_SYMBOL_CLOSE);
            lv_obj_set_style_text_font(close_label, LV_FONT_DEFAULT, LV_PART_MAIN);
            lv_obj_set_style_text_color(close_label,
                                        lv_color_hex(GUI_PROTOCOL_MUTED_COLOR),
                                        LV_PART_MAIN);
            lv_obj_center(close_label);
        }
    }

    /* 参数区独立纵向滚动，新增显示类型后 SPI 的全部字段仍可访问，标题与确认按钮固定 */
    body = lv_obj_create(gui_protocol_dialog.root);
    if (body == NULL)
    {
        GUI_Protocol_Dialog_Close();
        return;
    }
    lv_obj_remove_style_all(body);
    lv_obj_set_pos(body, 0, GUI_PROTOCOL_DIALOG_HEADER_HEIGHT);
    lv_obj_set_size(body,
                    WAVE_AREA_WIDTH,
                    height - GUI_PROTOCOL_DIALOG_HEADER_HEIGHT - GUI_PROTOCOL_DIALOG_FOOTER_HEIGHT);
    lv_obj_set_scroll_dir(body, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(body, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_pad_bottom(body, 14, LV_PART_MAIN);
    lv_obj_set_style_bg_color(body, lv_color_hex(GUI_PROTOCOL_MUTED_COLOR), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(body, LV_OPA_60, LV_PART_SCROLLBAR);
    lv_obj_set_style_width(body, 3, LV_PART_SCROLLBAR);
    gui_protocol_dialog.display_dropdown =
        GUI_Protocol_Dialog_Row(body,
                                GUI_TEXT("显示类型", "Display format"),
                                16,
                                "HEX\nASCII",
                                (uint32_t)gui_protocol_display_formats[kind]);
    first_y = 16 + GUI_PROTOCOL_DIALOG_ROW_HEIGHT;
    if (kind == GUI_PROTOCOL_KIND_UART)
    {
        GUI_Protocol_Dialog_Build_UART(body, first_y);
    }
    else if (kind == GUI_PROTOCOL_KIND_I2C)
    {
        GUI_Protocol_Dialog_Build_I2C(body, first_y);
    }
    else
    {
        GUI_Protocol_Dialog_Build_SPI(body, first_y);
    }
    GUI_Protocol_Dialog_Restore_Selection();

    separator = lv_obj_create(gui_protocol_dialog.root);
    if (separator != NULL)
    {
        lv_obj_remove_style_all(separator);
        lv_obj_set_pos(separator, 0, height - GUI_PROTOCOL_DIALOG_FOOTER_HEIGHT);
        lv_obj_set_size(separator, WAVE_AREA_WIDTH, 1);
        lv_obj_set_style_bg_color(separator, lv_color_hex(GUI_PROTOCOL_LINE_COLOR), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(separator, LV_OPA_COVER, LV_PART_MAIN);
    }
    footer = lv_obj_create(gui_protocol_dialog.root);
    if (footer == NULL)
    {
        return;
    }
    lv_obj_remove_style_all(footer);
    lv_obj_set_pos(footer, 0, height - GUI_PROTOCOL_DIALOG_FOOTER_HEIGHT + 1);
    lv_obj_set_size(footer, WAVE_AREA_WIDTH, GUI_PROTOCOL_DIALOG_FOOTER_HEIGHT - 1);
    lv_obj_set_style_bg_color(footer, lv_color_hex(GUI_PROTOCOL_HEADER_COLOR), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(footer, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_scrollable(footer, false);

    gui_protocol_dialog.error_label = lv_label_create(footer);
    if (gui_protocol_dialog.error_label != NULL)
    {
        lv_label_set_text(gui_protocol_dialog.error_label, "");
        lv_obj_set_pos(gui_protocol_dialog.error_label, GUI_PROTOCOL_DIALOG_PADDING, 8);
        lv_obj_set_width(gui_protocol_dialog.error_label, WAVE_AREA_WIDTH - 170);
        lv_obj_set_style_text_color(gui_protocol_dialog.error_label,
                                    lv_color_hex(GUI_PROTOCOL_TEXT_COLOR),
                                    LV_PART_MAIN);
    }

    cancel_button = lv_button_create(footer);
    if (cancel_button != NULL)
    {
        lv_obj_set_pos(cancel_button, WAVE_AREA_WIDTH - 139, 8);
        lv_obj_set_size(cancel_button, 62, 37);
        lv_obj_set_style_bg_opa(cancel_button, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_border_width(cancel_button, 0, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(cancel_button, 0, LV_PART_MAIN);
        lv_obj_add_event_cb(cancel_button,
                            GUI_Protocol_Dialog_Close_Event_Callback,
                            LV_EVENT_CLICKED,
                            NULL);
        label = lv_label_create(cancel_button);
        if (label != NULL)
        {
            lv_label_set_text(label, GUI_TEXT("取消", "Cancel"));
            lv_obj_center(label);
        }
    }
    confirm_button = lv_button_create(footer);
    if (confirm_button != NULL)
    {
        lv_obj_set_pos(confirm_button, WAVE_AREA_WIDTH - 70, 8);
        lv_obj_set_size(confirm_button, 56, 37);
        lv_obj_set_style_bg_color(confirm_button,
                                  lv_color_hex(GUI_PROTOCOL_ACCENT_COLOR),
                                  LV_PART_MAIN);
        lv_obj_set_style_bg_opa(confirm_button, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(confirm_button, 0, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(confirm_button, 0, LV_PART_MAIN);
        lv_obj_add_event_cb(confirm_button,
                            GUI_Protocol_Dialog_Confirm_Event_Callback,
                            LV_EVENT_CLICKED,
                            NULL);
        label = lv_label_create(confirm_button);
        if (label != NULL)
        {
            lv_label_set_text(label, GUI_TEXT("确定", "OK"));
            lv_obj_center(label);
        }
    }
}

/**
 * @brief  关闭设置窗口并恢复当前波形的可见性
 */
static void GUI_Protocol_Dialog_Close_Event_Callback(lv_event_t *event)
{
    (void)event;
    GUI_Protocol_Dialog_Close();
}

/**
 * @brief  验证通道和参数后保存协议配置并请求解析布局
 */
static void GUI_Protocol_Dialog_Confirm_Event_Callback(lv_event_t *event)
{
    const char *roles[WAVE_CHANNEL_COUNT] = {NULL};
    bool result_channels[WAVE_CHANNEL_COUNT] = {false};
    const char *protocol_name = NULL;
    Protocol_Config_t config; /* 保存的有效解析配置，按值复制而不借用弹窗控件 */
    Wave_Protocol_Display_Format_t display_format;
    uint32_t index;

    (void)event;
    if (gui_protocol_dialog.enabled_switch == NULL ||
        !lv_obj_has_state(gui_protocol_dialog.enabled_switch, LV_STATE_CHECKED))
    {
        GUI_Protocol_Dialog_Close();
        return;
    }
    memset(&config, 0, sizeof(config));
    config.start_tick = 0U;
    config.end_tick = UINT32_MAX;
    if (gui_protocol_dialog.display_dropdown == NULL)
    {
        return;
    }
    display_format = (Wave_Protocol_Display_Format_t)lv_dropdown_get_selected(
        gui_protocol_dialog.display_dropdown);
    if (gui_protocol_dialog.error_label != NULL)
    {
        lv_label_set_text(gui_protocol_dialog.error_label, "");
    }
    for (index = 0U; index < (gui_protocol_dialog.kind == GUI_PROTOCOL_KIND_I2C ? 1U : 5U); index++)
    {
        if (gui_protocol_dialog.parameter_dropdowns[index] == NULL)
        {
            return;
        }
    }

    if (gui_protocol_dialog.kind == GUI_PROTOCOL_KIND_UART)
    {
        const uint32_t baud_index =
            lv_dropdown_get_selected(gui_protocol_dialog.parameter_dropdowns[0]);

        protocol_name = "UART";
        if ((gui_protocol_dialog.uart_rx_channel == 0U &&
             gui_protocol_dialog.uart_tx_channel == 0U) ||
            baud_index >=
                sizeof(gui_protocol_uart_baud_rates) / sizeof(gui_protocol_uart_baud_rates[0]))
        {
            return;
        }
        config.protocol = PROTOCOL_TYPE_UART;
        config.decoder.uart.rx_channel =
            gui_protocol_dialog.uart_rx_channel == 0U ?
                PROTOCOL_CHANNEL_UNUSED :
                (LA_Service_Channel_t)(gui_protocol_dialog.uart_rx_channel - 1U);
        config.decoder.uart.tx_channel =
            gui_protocol_dialog.uart_tx_channel == 0U ?
                PROTOCOL_CHANNEL_UNUSED :
                (LA_Service_Channel_t)(gui_protocol_dialog.uart_tx_channel - 1U);
        config.decoder.uart.baud_rate = gui_protocol_uart_baud_rates[baud_index];
        /* 与实际解码器的三倍时基下限一致，低频长时采集不能提交不可解析的高波特率 */
        if ((uint64_t)config.decoder.uart.baud_rate * 3U > Wave_Get_Capture_Timestamp_Hz())
        {
            if (gui_protocol_dialog.error_label != NULL)
            {
                lv_label_set_text(
                    gui_protocol_dialog.error_label,
                    GUI_TEXT("波特率高于采样频率 / 3", "Baud rate exceeds capture Hz / 3"));
            }
            return;
        }
        config.decoder.uart.data_bits =
            (uint8_t)(5U + lv_dropdown_get_selected(gui_protocol_dialog.parameter_dropdowns[1]));
        config.decoder.uart.parity = (Protocol_UART_Parity_t)lv_dropdown_get_selected(
            gui_protocol_dialog.parameter_dropdowns[2]);
        config.decoder.uart.stop_bits =
            (uint8_t)(1U + lv_dropdown_get_selected(gui_protocol_dialog.parameter_dropdowns[3]));
        config.decoder.uart.inverted =
            (uint8_t)lv_dropdown_get_selected(gui_protocol_dialog.parameter_dropdowns[4]);
        if (gui_protocol_dialog.uart_rx_channel != 0U)
        {
            roles[gui_protocol_dialog.uart_rx_channel - 1U] = "RX";
            result_channels[gui_protocol_dialog.uart_rx_channel - 1U] = true;
        }
        if (gui_protocol_dialog.uart_tx_channel != 0U)
        {
            roles[gui_protocol_dialog.uart_tx_channel - 1U] = "TX";
            result_channels[gui_protocol_dialog.uart_tx_channel - 1U] = true;
        }
    }
    else if (gui_protocol_dialog.kind == GUI_PROTOCOL_KIND_I2C)
    {
        const uint8_t scl =
            GUI_Protocol_Dialog_Channel_Dropdown_Value(gui_protocol_dialog.channel_dropdowns[0]);
        const uint8_t sda =
            GUI_Protocol_Dialog_Channel_Dropdown_Value(gui_protocol_dialog.channel_dropdowns[1]);
        protocol_name = "I2C";
        if (scl == 0U || sda == 0U || scl == sda)
        {
            return;
        }
        config.protocol = PROTOCOL_TYPE_IIC;
        config.decoder.iic.scl_channel = (LA_Service_Channel_t)(scl - 1U);
        config.decoder.iic.sda_channel = (LA_Service_Channel_t)(sda - 1U);
        config.decoder.iic.address_bits =
            lv_dropdown_get_selected(gui_protocol_dialog.parameter_dropdowns[0]) == 0U ? 7U : 10U;
        roles[scl - 1U] = "SCL";
        roles[sda - 1U] = "SDA";
        result_channels[sda - 1U] = true;
    }
    else
    {
        const uint8_t clk =
            GUI_Protocol_Dialog_Channel_Dropdown_Value(gui_protocol_dialog.channel_dropdowns[0]);
        const uint8_t miso =
            GUI_Protocol_Dialog_Channel_Dropdown_Value(gui_protocol_dialog.channel_dropdowns[1]);
        const uint8_t mosi =
            GUI_Protocol_Dialog_Channel_Dropdown_Value(gui_protocol_dialog.channel_dropdowns[2]);
        const uint8_t cs =
            GUI_Protocol_Dialog_Channel_Dropdown_Value(gui_protocol_dialog.channel_dropdowns[3]);
        protocol_name = "SPI";
        if (clk == 0U || (miso == 0U && mosi == 0U) || clk == miso || clk == mosi || clk == cs ||
            (miso != 0U && miso == mosi) || (cs != 0U && (cs == miso || cs == mosi)))
        {
            return;
        }
        config.protocol = PROTOCOL_TYPE_SPI;
        config.decoder.spi.clk_channel = (LA_Service_Channel_t)(clk - 1U);
        config.decoder.spi.miso_channel =
            miso == 0U ? PROTOCOL_CHANNEL_UNUSED : (LA_Service_Channel_t)(miso - 1U);
        config.decoder.spi.mosi_channel =
            mosi == 0U ? PROTOCOL_CHANNEL_UNUSED : (LA_Service_Channel_t)(mosi - 1U);
        config.decoder.spi.cs_channel =
            cs == 0U ? PROTOCOL_CHANNEL_UNUSED : (LA_Service_Channel_t)(cs - 1U);
        config.decoder.spi.cs_active_level =
            (uint8_t)lv_dropdown_get_selected(gui_protocol_dialog.parameter_dropdowns[0]);
        config.decoder.spi.clock_mode =
            (uint8_t)((lv_dropdown_get_selected(gui_protocol_dialog.parameter_dropdowns[1]) << 1U) |
                      lv_dropdown_get_selected(gui_protocol_dialog.parameter_dropdowns[2]));
        config.decoder.spi.bit_order = (Protocol_Bit_Order_t)lv_dropdown_get_selected(
            gui_protocol_dialog.parameter_dropdowns[3]);
        config.decoder.spi.word_bits =
            (uint8_t)(1U + lv_dropdown_get_selected(gui_protocol_dialog.parameter_dropdowns[4]));
        roles[clk - 1U] = "CLK";
        if (miso != 0U)
        {
            roles[miso - 1U] = "MISO";
            result_channels[miso - 1U] = true;
        }
        if (mosi != 0U)
        {
            roles[mosi - 1U] = "MOSI";
            result_channels[mosi - 1U] = true;
        }
        if (cs != 0U)
        {
            roles[cs - 1U] = "CS";
        }
    }

    if (!Wave_Apply_Protocol_Layout(protocol_name, roles, result_channels))
    {
        (void)Log_Printf("ui: protocol layout rejected kind=%u\r\n",
                         (unsigned int)gui_protocol_dialog.kind);
        return;
    }
    if (!Wave_Protocol_Set_Display_Format(display_format))
    {
        (void)Log_Printf("ui: protocol display format rejected\r\n");
        return;
    }
    gui_protocol_display_formats[gui_protocol_dialog.kind] = display_format;
    if (gui_protocol_set_config_callback != NULL)
    {
        gui_protocol_set_config_callback(&config, gui_protocol_config_context);
    }
    GUI_Protocol_Dialog_Close();
}

/**
 * @brief  清理覆盖窗口句柄并恢复波形硬件层显示
 * @note   异步删除窗口，避免释放当前事件的祖先对象
 */
static void GUI_Protocol_Dialog_Close(void)
{
    const bool had_overlay = (gui_protocol_dialog.root != NULL) ||
                             (gui_protocol_dialog.clear_root != NULL) ||
                             (gui_protocol_dialog.open_timer != NULL);

    if (gui_protocol_dialog.open_timer != NULL)
    {
        lv_timer_delete(gui_protocol_dialog.open_timer);
        gui_protocol_dialog.open_timer = NULL;
    }
    if (gui_protocol_dialog.root != NULL)
    {
        /* 关闭按钮是根对象的子对象；异步删除避免在当前点击事件栈中释放祖先 */
        lv_obj_delete_async(gui_protocol_dialog.root);
        gui_protocol_dialog.root = NULL;
        gui_protocol_dialog.enabled_switch = NULL;
    }
    gui_protocol_dialog.uart_rx_dropdown = NULL;
    gui_protocol_dialog.uart_tx_dropdown = NULL;
    gui_protocol_dialog.display_dropdown = NULL;
    gui_protocol_dialog.error_label = NULL;
    gui_protocol_dialog.uart_rx_channel = 0U;
    gui_protocol_dialog.uart_tx_channel = 0U;
    memset(gui_protocol_dialog.channel_dropdowns, 0, sizeof(gui_protocol_dialog.channel_dropdowns));
    memset(gui_protocol_dialog.parameter_dropdowns,
           0,
           sizeof(gui_protocol_dialog.parameter_dropdowns));
    if (gui_protocol_dialog.clear_root != NULL)
    {
        lv_obj_delete_async(gui_protocol_dialog.clear_root);
        gui_protocol_dialog.clear_root = NULL;
    }
    if (had_overlay && !BSP_LTDC_LCD_Overlay_Layer_Set_Visible(true))
    {
        (void)Log_Printf("ui: protocol wave layer restore failed\r\n");
    }
    lv_obj_invalidate(lv_screen_active());
}
