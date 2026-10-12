/**
 * @file   app_protocol.c
 * @brief  协议配置与解析会话管理
 */

/* ================================================== */

#include "gui/app_protocol.h"
#include "gui/pages/gui_pages.h"
#include "waveform/wave_service.h"
#include "la/la_service.h"
#include "protocol/protocol_service.h"
#include "log/log_service.h"

/* ================================================== */

typedef struct
{
    Protocol_Config_t config;          /* 最近确认的有效解析配置 */
    bool config_valid;                 /* 已保存配置 */
    bool start_pending;                /* 等待采集结束且数据可供解析 */
    bool running;                      /* 正在跟踪已接受的解析会话 */
    bool results_available;            /* 解析已完成或被截断，且有结果可显示 */
    uint32_t session_id;               /* 当前应用跟踪的解析会话 */
    uint32_t last_terminal_capture_id; /* 已处理的 LA 终态采集编号 */
} App_Protocol_Handle_t;

/* ================================================== */

static App_Protocol_Handle_t app_protocol;

/* ================================================== */

static bool App_Protocol_Get_Config_Callback(Protocol_Config_t *config, void *context);
static void App_Protocol_Set_Config_Callback(const Protocol_Config_t *config, void *context);

/* ================================================== */

/**
 * @brief  向协议弹窗提供已确认配置副本
 */
static bool App_Protocol_Get_Config_Callback(Protocol_Config_t *config, void *context)
{
    (void)context;
    if (config == NULL || !app_protocol.config_valid)
    {
        return false;
    }
    *config = app_protocol.config;
    return true;
}

/**
 * @brief  保存弹窗确认的配置并登记解析请求
 */
static void App_Protocol_Set_Config_Callback(const Protocol_Config_t *config, void *context)
{
    (void)context;
    if (config == NULL)
    {
        return;
    }
    app_protocol.config = *config;
    app_protocol.config_valid = true;
    App_Protocol_Request_Parse();
}

/* ================================================== */

/**
 * @brief  绑定协议配置的读取与确认请求
 */
void App_Protocol_Init(void)
{
    GUI_Protocol_Bind_Config(App_Protocol_Get_Config_Callback,
                             App_Protocol_Set_Config_Callback,
                             NULL);
}

/**
 * @brief  取消旧会话并登记针对当前采集的解析请求
 * @note   异步等待采集终态和旧会话释放
 */
void App_Protocol_Request_Parse(void)
{
    if (!app_protocol.config_valid || !Wave_Protocol_Layout_Is_Active())
    {
        return;
    }
    if (app_protocol.running)
    {
        (void)Protocol_Service_Cancel();
    }
    app_protocol.start_pending = true;
    app_protocol.running = false;
    app_protocol.results_available = false;
    GUI_Protocol_Clear_Results();
}

/**
 * @brief  取消待启动或运行中的自动解析
 * @note   标记当前终态已处理，避免自动重启解析
 */
void App_Protocol_Cancel(void)
{
    Protocol_Info_t info;
    LA_Service_Info_t la_info;

    if (Protocol_Service_Get_Info(&info) == PROTOCOL_OK &&
        (info.state == PROTOCOL_STATE_STARTING || info.state == PROTOCOL_STATE_RUNNING))
    {
        (void)Protocol_Service_Cancel();
    }
    app_protocol.start_pending = false;
    app_protocol.running = false;
    app_protocol.results_available = false;
    if (LA_Service_Get_Info(&la_info) == LA_SERVICE_OK &&
        (la_info.state == LA_SERVICE_STATE_DONE || la_info.state == LA_SERVICE_STATE_ERROR))
    {
        app_protocol.last_terminal_capture_id = la_info.capture_id;
    }
    GUI_Protocol_Clear_Results();
}

/**
 * @brief  检查自动解析进度，并更新界面显示的结果
 * @note   按 capture_id 去重，按 session_id 隔离结果
 */
void App_Protocol_Process(void)
{
    Protocol_Info_t protocol_info;
    LA_Service_Info_t la_info;
    bool la_info_valid = false;

    if (!Wave_Protocol_Layout_Is_Active())
    {
        if (app_protocol.running)
        {
            (void)Protocol_Service_Cancel();
        }
        app_protocol.start_pending = false;
        app_protocol.running = false;
        app_protocol.results_available = false;
        return;
    }

    if (LA_Service_Get_Info(&la_info) == LA_SERVICE_OK)
    {
        la_info_valid = true;
        if ((la_info.state == LA_SERVICE_STATE_DONE || la_info.state == LA_SERVICE_STATE_ERROR) &&
            la_info.capture_id != 0U && la_info.capture_id != app_protocol.last_terminal_capture_id)
        {
            app_protocol.last_terminal_capture_id = la_info.capture_id;
            App_Protocol_Request_Parse();
        }
    }

    if (app_protocol.start_pending)
    {
        Protocol_Result_t start_result;

        if (!la_info_valid && LA_Service_Get_Info(&la_info) != LA_SERVICE_OK)
        {
            return;
        }
        if (la_info.state != LA_SERVICE_STATE_DONE && la_info.state != LA_SERVICE_STATE_ERROR)
        {
            return;
        }
        start_result = Protocol_Service_Start(&app_protocol.config);
        if (start_result == PROTOCOL_ERROR_BUSY)
        {
            return;
        }
        if (start_result != PROTOCOL_OK)
        {
            app_protocol.start_pending = false;
            app_protocol.running = false;
            return;
        }
        if (Protocol_Service_Get_Info(&protocol_info) == PROTOCOL_OK)
        {
            app_protocol.session_id = protocol_info.session_id;
        }
        app_protocol.start_pending = false;
        app_protocol.running = true;
        return;
    }

    if (app_protocol.running)
    {
        if (Protocol_Service_Get_Info(&protocol_info) != PROTOCOL_OK ||
            protocol_info.session_id != app_protocol.session_id)
        {
            return;
        }
        if (protocol_info.state == PROTOCOL_STATE_COMPLETE ||
            protocol_info.state == PROTOCOL_STATE_TRUNCATED)
        {
            app_protocol.running = false;
            app_protocol.results_available = true;
            GUI_Protocol_Clear_Results();
            if (protocol_info.error_count != 0U || protocol_info.dropped_count != 0U)
            {
                (void)Log_Printf("ui: protocol errors=%lu dropped=%lu records=%lu\r\n",
                                 (unsigned long)protocol_info.error_count,
                                 (unsigned long)protocol_info.dropped_count,
                                 (unsigned long)protocol_info.result_count);
            }
        }
        else if (protocol_info.state == PROTOCOL_STATE_ERROR ||
                 protocol_info.state == PROTOCOL_STATE_CANCELLED)
        {
            app_protocol.running = false;
            app_protocol.results_available = false;
            GUI_Protocol_Clear_Results();
        }
    }
    if (app_protocol.results_available &&
        Protocol_Service_Get_Info(&protocol_info) == PROTOCOL_OK &&
        protocol_info.session_id == app_protocol.session_id)
    {
        GUI_Protocol_Update_Results(&protocol_info);
    }
}
