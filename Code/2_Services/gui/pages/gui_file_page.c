/**
 * @file   gui_file_page.c
 * @brief  采集文件操作页面
 */

/* ================================================== */

#include "gui/pages/gui_pages.h"
#include "filesystem/file_manager.h"
#include "waveform/wave_service.h"
#include "log/log_service.h"
#include "lvgl/resource/resource_font.h"
#include "lvgl/gui_language.h"
#include <stdio.h>
#include <string.h>

/* ========================= 配置 ========================= */

#define GUI_FILE_PAGE_WIDTH          138
#define GUI_FILE_PAGE_MARGIN         6
#define GUI_FILE_PAGE_TITLE_Y        10
#define GUI_FILE_PAGE_LIST_Y         70
#define GUI_FILE_PAGE_ACTION_BOTTOM  8
#define GUI_FILE_PAGE_ACTION_GAP     4
#define GUI_FILE_PAGE_BUTTON_HEIGHT  32
#define GUI_FILE_PAGE_ACTION_HEIGHT  (GUI_FILE_PAGE_BUTTON_HEIGHT * 2 + GUI_FILE_PAGE_ACTION_GAP)
#define GUI_FILE_PAGE_BG_COLOR       0x000000U
#define GUI_FILE_PAGE_TEXT_COLOR     0xE6EDF7U
#define GUI_FILE_PAGE_MUTED_COLOR    0x91A0B2U
#define GUI_FILE_PAGE_ACCENT_COLOR   0x2F80EDU
#define GUI_FILE_PAGE_ITEM_COLOR     0x202833U
#define GUI_FILE_PAGE_SELECTED_COLOR 0x334E70U
#define GUI_FILE_PAGE_DELETE_COLOR   0xA83A45U
#define GUI_FILE_PAGE_LIST_COLOR     0x10161EU
#define GUI_FILE_PAGE_BORDER_COLOR   0x2B3440U

/* ================================================== */

typedef struct
{
    lv_obj_t *root;
    lv_obj_t *save_button;
    lv_obj_t *status_label;
    lv_obj_t *file_list;
    lv_obj_t *delete_button;
    lv_obj_t *load_button;
    lv_obj_t *load_button_label;
    lv_obj_t *delete_all_button;
    uint32_t shown_file_revision;
    uint32_t shown_status_revision;
    char selected_name[FILE_MANAGER_NAME_CAPACITY];
} GUI_File_Page_t;

/* ================================================== */

static GUI_File_Page_t gui_file_page;
static File_Manager_Snapshot_t gui_file_snapshot;
static GUI_File_Request_Callback_t gui_file_request_callback;
static void *gui_file_request_context;

/* ================================================== */

static File_Manager_Request_Result_t GUI_File_Submit_Request(File_Manager_Operation_t operation,
                                                             const char *name);
static lv_obj_t *GUI_File_Page_Create_Root(lv_obj_t *parent);
static lv_obj_t *GUI_File_Page_Create_Button(lv_obj_t *parent,
                                             const char *text,
                                             int32_t x,
                                             int32_t y,
                                             int32_t width,
                                             uint32_t color,
                                             lv_event_cb_t callback);
static void GUI_File_Page_Format_Entry(char *text, size_t capacity, const char *name);
static void GUI_File_Page_Rebuild_List(void);
static void GUI_File_Page_Refresh_Status(void);
static void GUI_File_Page_Show_Request_Result(File_Manager_Operation_t operation,
                                              File_Manager_Request_Result_t result);
static void GUI_File_Page_Root_Event_Callback(lv_event_t *event);
static void GUI_File_Page_Save_Event_Callback(lv_event_t *event);
static void GUI_File_Page_Item_Event_Callback(lv_event_t *event);
static void GUI_File_Page_Delete_Event_Callback(lv_event_t *event);
static void GUI_File_Page_Delete_All_Event_Callback(lv_event_t *event);
static void GUI_File_Page_Load_Event_Callback(lv_event_t *event);

/* ================================================== */

/**
 * @brief  创建保存、目录、删除和加载操作的文件页
 * @note   先显示已有文件列表，再让后台重新扫描目录
 */
void GUI_File_Page_Create(lv_obj_t *page_parent)
{
    lv_obj_t *files_label;
    int32_t height;
    int32_t action_y;
    int32_t action_width;

    gui_file_page.root = GUI_File_Page_Create_Root(page_parent);
    if (gui_file_page.root == NULL)
    {
        (void)Log_Printf("file: page allocation failed\r\n");
        return;
    }
    /* LVGL尺寸通过样式延迟提交；先布局，否则首次进入页面时高度可能仍为0 */
    lv_obj_update_layout(gui_file_page.root);
    height = lv_obj_get_height(gui_file_page.root);

    if (height <= GUI_FILE_PAGE_LIST_Y + GUI_FILE_PAGE_ACTION_HEIGHT + GUI_FILE_PAGE_ACTION_BOTTOM +
                      GUI_FILE_PAGE_ACTION_GAP)
    {
        height = lv_obj_get_height(page_parent);
    }
    action_y = height - GUI_FILE_PAGE_ACTION_BOTTOM - GUI_FILE_PAGE_ACTION_HEIGHT;
    action_width = (GUI_FILE_PAGE_WIDTH - GUI_FILE_PAGE_MARGIN * 2 - GUI_FILE_PAGE_ACTION_GAP) / 2;
    /* 先显示已有文件列表；后台扫描失败时仍保留它 */
    /* 重建页面保留已选文件，目录更新时再清除已经不存在的名称 */
    gui_file_page.shown_file_revision = UINT32_MAX;
    gui_file_page.shown_status_revision = UINT32_MAX;

    gui_file_page.save_button =
        GUI_File_Page_Create_Button(gui_file_page.root,
                                    GUI_TEXT("保存文件", "Save file"),
                                    GUI_FILE_PAGE_MARGIN,
                                    GUI_FILE_PAGE_TITLE_Y,
                                    GUI_FILE_PAGE_WIDTH - GUI_FILE_PAGE_MARGIN * 2,
                                    GUI_FILE_PAGE_ACCENT_COLOR,
                                    GUI_File_Page_Save_Event_Callback);

    files_label = lv_label_create(gui_file_page.root);
    if (files_label != NULL)
    {
        lv_label_set_text(files_label, GUI_TEXT("文件", "Files"));
        lv_obj_set_pos(files_label, 8, 45);
        lv_obj_set_style_text_color(files_label,
                                    lv_color_hex(GUI_FILE_PAGE_TEXT_COLOR),
                                    LV_PART_MAIN);
    }
    gui_file_page.status_label = lv_label_create(gui_file_page.root);
    if (gui_file_page.status_label != NULL)
    {
        lv_obj_set_pos(gui_file_page.status_label, 44, 45);
        lv_obj_set_width(gui_file_page.status_label, 86);
        lv_label_set_long_mode(gui_file_page.status_label, LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_align(gui_file_page.status_label, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
        lv_obj_set_style_text_color(gui_file_page.status_label,
                                    lv_color_hex(GUI_FILE_PAGE_MUTED_COLOR),
                                    LV_PART_MAIN);
    }

    gui_file_page.file_list = lv_obj_create(gui_file_page.root);
    if (gui_file_page.file_list != NULL)
    {
        lv_obj_remove_style_all(gui_file_page.file_list);
        lv_obj_set_pos(gui_file_page.file_list, GUI_FILE_PAGE_MARGIN, GUI_FILE_PAGE_LIST_Y);
        lv_obj_set_size(gui_file_page.file_list,
                        GUI_FILE_PAGE_WIDTH - GUI_FILE_PAGE_MARGIN * 2,
                        action_y - GUI_FILE_PAGE_LIST_Y - GUI_FILE_PAGE_ACTION_GAP);
        lv_obj_set_style_radius(gui_file_page.file_list, 4, LV_PART_MAIN);
        lv_obj_set_style_bg_color(gui_file_page.file_list,
                                  lv_color_hex(GUI_FILE_PAGE_LIST_COLOR),
                                  LV_PART_MAIN);
        lv_obj_set_style_bg_opa(gui_file_page.file_list, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_color(gui_file_page.file_list,
                                      lv_color_hex(GUI_FILE_PAGE_BORDER_COLOR),
                                      LV_PART_MAIN);
        lv_obj_set_style_border_width(gui_file_page.file_list, 1, LV_PART_MAIN);
        lv_obj_set_style_pad_all(gui_file_page.file_list, 0, LV_PART_MAIN);
        lv_obj_set_scrollable(gui_file_page.file_list, true);
        lv_obj_set_scroll_dir(gui_file_page.file_list, LV_DIR_VER);
        lv_obj_set_scrollbar_mode(gui_file_page.file_list, LV_SCROLLBAR_MODE_AUTO);
        lv_obj_set_style_width(gui_file_page.file_list, 3, LV_PART_SCROLLBAR);
        lv_obj_set_style_bg_color(gui_file_page.file_list,
                                  lv_color_hex(GUI_FILE_PAGE_ACCENT_COLOR),
                                  LV_PART_SCROLLBAR);
        lv_obj_set_style_bg_opa(gui_file_page.file_list, LV_OPA_COVER, LV_PART_SCROLLBAR);
    }

    gui_file_page.delete_button = GUI_File_Page_Create_Button(gui_file_page.root,
                                                              GUI_TEXT("删除", "Delete"),
                                                              GUI_FILE_PAGE_MARGIN,
                                                              action_y,
                                                              action_width,
                                                              GUI_FILE_PAGE_DELETE_COLOR,
                                                              GUI_File_Page_Delete_Event_Callback);
    gui_file_page.load_button =
        GUI_File_Page_Create_Button(gui_file_page.root,
                                    GUI_TEXT("加载", "Load"),
                                    GUI_FILE_PAGE_MARGIN + action_width + GUI_FILE_PAGE_ACTION_GAP,
                                    action_y,
                                    action_width,
                                    GUI_FILE_PAGE_ACCENT_COLOR,
                                    GUI_File_Page_Load_Event_Callback);
    if (gui_file_page.load_button != NULL)
    {
        gui_file_page.load_button_label = lv_obj_get_child(gui_file_page.load_button, 0);
    }
    gui_file_page.delete_all_button = GUI_File_Page_Create_Button(
        gui_file_page.root,
        GUI_TEXT("全部删除", "Delete all"),
        GUI_FILE_PAGE_MARGIN,
        action_y + GUI_FILE_PAGE_BUTTON_HEIGHT + GUI_FILE_PAGE_ACTION_GAP,
        GUI_FILE_PAGE_WIDTH - GUI_FILE_PAGE_MARGIN * 2,
        GUI_FILE_PAGE_DELETE_COLOR,
        GUI_File_Page_Delete_All_Event_Callback);

    GUI_File_Update(&gui_file_snapshot);
    GUI_File_Page_Show_Request_Result(
        FILE_MANAGER_OPERATION_REFRESH,
        GUI_File_Submit_Request(FILE_MANAGER_OPERATION_REFRESH, NULL));
}

/**
 * @brief  绑定文件操作请求处理接口
 */
void GUI_File_Bind_Request(GUI_File_Request_Callback_t callback, void *context)
{
    gui_file_request_callback = callback;
    gui_file_request_context = context;
}

/**
 * @brief  用应用传来的文件列表和操作状态更新页面
 */
void GUI_File_Update(const File_Manager_Snapshot_t *snapshot)
{
    if (snapshot == NULL)
    {
        return;
    }
    if (snapshot != &gui_file_snapshot)
    {
        gui_file_snapshot = *snapshot;
    }
    if (gui_file_page.root == NULL)
    {
        return;
    }
    if (gui_file_page.load_button_label != NULL)
    {
        lv_label_set_text(gui_file_page.load_button_label,
                          gui_file_snapshot.file_loaded ? GUI_TEXT("退出", "Exit") :
                                                          GUI_TEXT("加载", "Load"));
    }
    if (gui_file_page.shown_file_revision != gui_file_snapshot.file_revision)
    {
        gui_file_page.shown_file_revision = gui_file_snapshot.file_revision;
        GUI_File_Page_Rebuild_List();
    }
    if (gui_file_page.shown_status_revision != gui_file_snapshot.status_revision)
    {
        gui_file_page.shown_status_revision = gui_file_snapshot.status_revision;
        GUI_File_Page_Refresh_Status();
    }
}

/* ================================================== */

/**
 * @brief  将页面文件操作请求提交给应用
 */
static File_Manager_Request_Result_t GUI_File_Submit_Request(File_Manager_Operation_t operation,
                                                             const char *name)
{
    if (gui_file_request_callback == NULL)
    {
        return FILE_MANAGER_REQUEST_NOT_READY;
    }
    return gui_file_request_callback(operation, name, gui_file_request_context);
}

/**
 * @brief  创建文件页根容器并注册删除时的句柄清理
 */
static lv_obj_t *GUI_File_Page_Create_Root(lv_obj_t *parent)
{
    lv_obj_t *page;

    if (parent == NULL)
    {
        return NULL;
    }
    page = lv_obj_create(parent);
    if (page == NULL)
    {
        return NULL;
    }
    lv_obj_remove_style_all(page);
    lv_obj_set_pos(page, 0, 0);
    lv_obj_set_size(page, lv_obj_get_width(parent), lv_obj_get_height(parent));
    lv_obj_set_style_bg_color(page, lv_color_hex(GUI_FILE_PAGE_BG_COLOR), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(page, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_text_font(page, APP_FONT_CN_14_PTR, LV_PART_MAIN);
    lv_obj_set_scrollable(page, false);
    lv_obj_add_event_cb(page, GUI_File_Page_Root_Event_Callback, LV_EVENT_DELETE, NULL);
    return page;
}

/**
 * @brief  创建统一外观的文件操作按钮和文字
 */
static lv_obj_t *GUI_File_Page_Create_Button(lv_obj_t *parent,
                                             const char *text,
                                             int32_t x,
                                             int32_t y,
                                             int32_t width,
                                             uint32_t color,
                                             lv_event_cb_t callback)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_t *label;

    if (button == NULL)
    {
        return NULL;
    }
    lv_obj_set_pos(button, x, y);
    lv_obj_set_size(button, width, GUI_FILE_PAGE_BUTTON_HEIGHT);
    lv_obj_set_style_radius(button, 5, LV_PART_MAIN);
    lv_obj_set_style_bg_color(button, lv_color_hex(color), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(button, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN);
    /* 窄按钮留足双语标签宽度，避免英文被默认主题的水平内边距裁切 */
    lv_obj_set_style_pad_hor(button, 4, LV_PART_MAIN);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, NULL);

    label = lv_label_create(button);
    if (label != NULL)
    {
        lv_label_set_text(label, text);
        lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFFU), LV_PART_MAIN);
        lv_obj_center(label);
    }
    return button;
}

/**
 * @brief  缩短采集文件名，让它能放进文件列表
 * @note   仅缩短显示文字，文件操作使用完整名称
 */
static void GUI_File_Page_Format_Entry(char *text, size_t capacity, const char *name)
{
    static const char hex[] = "0123456789abcdefABCDEF";
    size_t index;

    if (text == NULL || capacity == 0U)
    {
        return;
    }
    if (name != NULL && strncmp(name, "capture_", 8U) == 0)
    {
        for (index = 8U; index < 16U; index++)
        {
            if (name[index] == '\0' || strchr(hex, name[index]) == NULL)
            {
                break;
            }
        }
        if (index == 16U && name[16] == '_' && name[17] >= '0' && name[17] <= '9' &&
            name[18] >= '0' && name[18] <= '9' && name[19] >= '0' && name[19] <= '9')
        {
            /* 保存文件名中的采集ID和同ID序号，直接作为列表主文本显示 */
            (void)snprintf(text, capacity, "ID:%.8s-%.3s", name + 8U, name + 17U);
            return;
        }
    }
    (void)snprintf(text, capacity, "%s", name == NULL ? "" : name);
}

/**
 * @brief  更新文件列表，尽量保留之前选中的文件
 * @note   新目录不含原选择时清空选择
 */
static void GUI_File_Page_Rebuild_List(void)
{
    uint16_t index;
    bool selection_found = false;

    if (gui_file_page.file_list == NULL)
    {
        return;
    }
    lv_obj_clean(gui_file_page.file_list);
    for (index = 0U; index < gui_file_snapshot.file_count; index++)
    {
        lv_obj_t *button = lv_button_create(gui_file_page.file_list);
        lv_obj_t *label;
        char display_name[FILE_MANAGER_NAME_CAPACITY];

        if (button == NULL)
        {
            break;
        }
        lv_obj_set_pos(button, 2, (int32_t)index * 34);
        lv_obj_set_size(button, GUI_FILE_PAGE_WIDTH - 22, 30);
        lv_obj_set_checkable(button, true);
        lv_obj_set_style_radius(button, 4, LV_PART_MAIN);
        lv_obj_set_style_bg_color(button, lv_color_hex(GUI_FILE_PAGE_ITEM_COLOR), LV_PART_MAIN);
        lv_obj_set_style_bg_color(button,
                                  lv_color_hex(GUI_FILE_PAGE_SELECTED_COLOR),
                                  LV_PART_MAIN | LV_STATE_CHECKED);
        lv_obj_set_style_border_width(button, 0, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(button, 4, LV_PART_MAIN);
        lv_obj_add_event_cb(button,
                            GUI_File_Page_Item_Event_Callback,
                            LV_EVENT_CLICKED,
                            (void *)(uintptr_t)index);

        label = lv_label_create(button);
        if (label != NULL)
        {
            GUI_File_Page_Format_Entry(display_name,
                                       sizeof(display_name),
                                       gui_file_snapshot.files[index].name);
            lv_label_set_text(label, display_name);
            lv_obj_set_width(label, GUI_FILE_PAGE_WIDTH - 32);
            lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
            lv_obj_set_style_text_color(label,
                                        lv_color_hex(GUI_FILE_PAGE_TEXT_COLOR),
                                        LV_PART_MAIN);
            lv_obj_center(label);
        }
        if (strcmp(gui_file_page.selected_name, gui_file_snapshot.files[index].name) == 0)
        {
            lv_obj_add_state(button, LV_STATE_CHECKED);
            selection_found = true;
        }
    }
    if (!selection_found)
    {
        gui_file_page.selected_name[0] = '\0';
    }
}

/**
 * @brief  根据文件服务的执行状态显示提示文字
 */
static void GUI_File_Page_Refresh_Status(void)
{
    static char error_text[24];
    const char *text = GUI_TEXT("就绪", "Ready");

    if (gui_file_page.status_label == NULL)
    {
        return;
    }
    if (gui_file_snapshot.busy)
    {
        switch (gui_file_snapshot.operation)
        {
            case FILE_MANAGER_OPERATION_SAVE:
                text = GUI_TEXT("保存中...", "Saving...");
                break;
            case FILE_MANAGER_OPERATION_DELETE:
                text = GUI_TEXT("删除中...", "Deleting...");
                break;
            case FILE_MANAGER_OPERATION_DELETE_ALL:
                text = GUI_TEXT("全部删除中", "Deleting all");
                break;
            case FILE_MANAGER_OPERATION_LOAD:
                text = GUI_TEXT("加载中...", "Loading...");
                break;
            case FILE_MANAGER_OPERATION_EXIT:
                text = GUI_TEXT("退出中...", "Exiting...");
                break;
            default:
                text = GUI_TEXT("扫描中...", "Scanning...");
                break;
        }
    }
    else if (gui_file_snapshot.last_success &&
             gui_file_snapshot.operation == FILE_MANAGER_OPERATION_EXIT)
    {
        text = GUI_TEXT("已退出", "Exited");
    }
    else if (!gui_file_snapshot.mounted)
    {
        text = GUI_TEXT("无TF卡", "No card");
    }
    else if (!gui_file_snapshot.last_success &&
             gui_file_snapshot.operation != FILE_MANAGER_OPERATION_NONE)
    {
        (void)snprintf(error_text,
                       sizeof(error_text),
                       "F:%lu C:%lu",
                       (unsigned long)gui_file_snapshot.filesystem_result,
                       (unsigned long)gui_file_snapshot.compression_result);
        text = error_text;
    }
    else if (gui_file_snapshot.operation == FILE_MANAGER_OPERATION_SAVE)
    {
        text = GUI_TEXT("已保存", "Saved");
    }
    else if (gui_file_snapshot.operation == FILE_MANAGER_OPERATION_DELETE)
    {
        text = GUI_TEXT("已删除", "Deleted");
    }
    else if (gui_file_snapshot.operation == FILE_MANAGER_OPERATION_DELETE_ALL)
    {
        text = GUI_TEXT("已全部删除", "Deleted all");
    }
    else if (gui_file_snapshot.operation == FILE_MANAGER_OPERATION_LOAD)
    {
        text = GUI_TEXT("已加载", "Loaded");
    }
    lv_label_set_text(gui_file_page.status_label, text);
}

/**
 * @brief  根据本次提交结果显示排队或拒绝原因
 * @note   请求成功只表示入队
 */
static void GUI_File_Page_Show_Request_Result(File_Manager_Operation_t operation,
                                              File_Manager_Request_Result_t result)
{
    const char *text;

    switch (result)
    {
        case FILE_MANAGER_REQUEST_ACCEPTED:
            switch (operation)
            {
                case FILE_MANAGER_OPERATION_REFRESH:
                    return;
                case FILE_MANAGER_OPERATION_SAVE:
                    text = GUI_TEXT("保存已排队", "Save queued");
                    break;
                case FILE_MANAGER_OPERATION_EXIT:
                    text = GUI_TEXT("退出已排队", "Exit queued");
                    break;
                case FILE_MANAGER_OPERATION_LOAD:
                    text = GUI_TEXT("加载已排队", "Queued");
                    break;
                default:
                    text = GUI_TEXT("删除已排队", "Queued");
                    break;
            }
            break;
        case FILE_MANAGER_REQUEST_FILE_LOADED:
            text = GUI_TEXT("请退出文件", "Exit file");
            break;
        case FILE_MANAGER_REQUEST_NOT_LOADED:
            text = GUI_TEXT("未加载文件", "No file");
            break;
        case FILE_MANAGER_REQUEST_NOT_READY:
            text = GUI_TEXT("服务未就绪", "Not ready");
            break;
        case FILE_MANAGER_REQUEST_INVALID_NAME:
            text = GUI_TEXT("文件名无效", "Invalid name");
            break;
        case FILE_MANAGER_REQUEST_QUEUE_FULL:
            text = GUI_TEXT("队列已满", "Queue full");
            break;
        case FILE_MANAGER_REQUEST_LOCK_BUSY:
        case FILE_MANAGER_REQUEST_BUSY:
            text = GUI_TEXT("忙", "Busy");
            break;
        default:
            text = GUI_TEXT("操作无效", "Invalid op");
            break;
    }
    if (gui_file_page.status_label != NULL)
    {
        lv_label_set_text(gui_file_page.status_label, text);
    }
    /* 忙和文件视图限制是正常状态提示，只有资源或请求错误进入串口日志 */
    if (result == FILE_MANAGER_REQUEST_NOT_READY ||
        result == FILE_MANAGER_REQUEST_INVALID_OPERATION ||
        result == FILE_MANAGER_REQUEST_INVALID_NAME || result == FILE_MANAGER_REQUEST_QUEUE_FULL)
    {
        (void)Log_Printf("file: request rejected op=%u result=%u\r\n",
                         (unsigned int)operation,
                         (unsigned int)result);
    }
}

/**
 * @brief  在文件页删除时清空全部失效的 LVGL 控件句柄
 * @note   只清空控件句柄，后台结果由应用继续处理
 */
static void GUI_File_Page_Root_Event_Callback(lv_event_t *event)
{
    (void)event;
    gui_file_page.root = NULL;
    gui_file_page.save_button = NULL;
    gui_file_page.status_label = NULL;
    gui_file_page.file_list = NULL;
    gui_file_page.delete_button = NULL;
    gui_file_page.load_button = NULL;
    gui_file_page.load_button_label = NULL;
    gui_file_page.delete_all_button = NULL;
}

/**
 * @brief  响应保存按钮并提交应用请求
 */
static void GUI_File_Page_Save_Event_Callback(lv_event_t *event)
{
    (void)event;

    if (Wave_Is_Capturing())
    {
        if (gui_file_page.status_label != NULL)
        {
            lv_label_set_text(gui_file_page.status_label, GUI_TEXT("请停止采集", "Stop capture"));
        }
        return;
    }
    GUI_File_Page_Show_Request_Result(FILE_MANAGER_OPERATION_SAVE,
                                      GUI_File_Submit_Request(FILE_MANAGER_OPERATION_SAVE, NULL));
}

/**
 * @brief  把被点击的目录条目标记为唯一选择
 */
static void GUI_File_Page_Item_Event_Callback(lv_event_t *event)
{
    uint16_t index = (uint16_t)(uintptr_t)lv_event_get_user_data(event);
    lv_obj_t *target = lv_event_get_target(event);
    uint32_t child_count;
    uint32_t child_index;

    if (index >= gui_file_snapshot.file_count || gui_file_page.file_list == NULL)
    {
        return;
    }
    child_count = lv_obj_get_child_count(gui_file_page.file_list);
    for (child_index = 0U; child_index < child_count; child_index++)
    {
        lv_obj_clear_state(lv_obj_get_child(gui_file_page.file_list, (int32_t)child_index),
                           LV_STATE_CHECKED);
    }
    lv_obj_add_state(target, LV_STATE_CHECKED);
    memcpy(gui_file_page.selected_name,
           gui_file_snapshot.files[index].name,
           strlen(gui_file_snapshot.files[index].name) + 1U);
}

/**
 * @brief  按选中的真实文件名提交异步删除请求
 */
static void GUI_File_Page_Delete_Event_Callback(lv_event_t *event)
{
    (void)event;
    if (gui_file_page.selected_name[0] == '\0')
    {
        if (gui_file_page.status_label != NULL)
        {
            lv_label_set_text(gui_file_page.status_label, GUI_TEXT("请选择文件", "Select file"));
        }
        return;
    }
    /* 等后台删除并重扫成功后按修订号刷新列表，失败时保留原行便于重试 */
    GUI_File_Page_Show_Request_Result(
        FILE_MANAGER_OPERATION_DELETE,
        GUI_File_Submit_Request(FILE_MANAGER_OPERATION_DELETE, gui_file_page.selected_name));
}

/**
 * @brief  提交异步删除全部采集文件请求
 * @note   等待删除和目录重扫完成后更新列表
 */
static void GUI_File_Page_Delete_All_Event_Callback(lv_event_t *event)
{
    (void)event;
    /* 删除和目录重扫完成后，由修订号更新列表 */
    GUI_File_Page_Show_Request_Result(
        FILE_MANAGER_OPERATION_DELETE_ALL,
        GUI_File_Submit_Request(FILE_MANAGER_OPERATION_DELETE_ALL, NULL));
}

/**
 * @brief  按当前文件模式提交加载或退出文件视图请求
 */
static void GUI_File_Page_Load_Event_Callback(lv_event_t *event)
{
    (void)event;
    if (gui_file_snapshot.file_loaded)
    {
        GUI_File_Page_Show_Request_Result(
            FILE_MANAGER_OPERATION_EXIT,
            GUI_File_Submit_Request(FILE_MANAGER_OPERATION_EXIT, NULL));
        return;
    }
    if (gui_file_page.selected_name[0] == '\0')
    {
        if (gui_file_page.status_label != NULL)
        {
            lv_label_set_text(gui_file_page.status_label, GUI_TEXT("请选择文件", "Select file"));
        }
        return;
    }
    if (Wave_Is_Capturing())
    {
        if (gui_file_page.status_label != NULL)
        {
            lv_label_set_text(gui_file_page.status_label, GUI_TEXT("请停止采集", "Stop capture"));
        }
        return;
    }
    GUI_File_Page_Show_Request_Result(
        FILE_MANAGER_OPERATION_LOAD,
        GUI_File_Submit_Request(FILE_MANAGER_OPERATION_LOAD, gui_file_page.selected_name));
}
