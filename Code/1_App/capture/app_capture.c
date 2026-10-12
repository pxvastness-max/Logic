/**
 * @file   app_capture.c
 * @brief  采集流程与状态管理
 */

/* ================================================== */

#include "capture/app_capture.h"
#include "gui/app_protocol.h"
#include "gui/pages/gui_panel.h"
#include "filesystem/file_manager.h"
#include "waveform/wave_service.h"
#include "log/log_service.h"
#include "FreeRTOS.h"
#include "task.h"

/* ========================= 配置 ========================= */

#define APP_CAPTURE_START_TIMEOUT_MS 10000U

/* ================================================== */

typedef enum
{
    APP_CAPTURE_STATE_IDLE = 0,
    APP_CAPTURE_STATE_STARTING,
    APP_CAPTURE_STATE_RUNNING
} App_Capture_State_t;

typedef struct
{
    App_Capture_State_t state;     /* 应用采集状态，不等同于 LA 后台收尾状态 */
    uint32_t duration_cnt;         /* 本轮计划采集时长 */
    uint32_t elapsed_cnt;          /* 已读取的实际硬件截止 CNT */
    TickType_t start_request_tick; /* 启动等待起点 */
    uint8_t progress_percent;      /* 按实际 CNT 计算的进度 */
} App_Capture_Handle_t;

/* ================================================== */

static App_Capture_Handle_t app_capture;
static File_Manager_Snapshot_t app_capture_file_snapshot;

/* ================================================== */

static void App_Capture_Publish_Status(void);
static bool App_Capture_Try_Start(void);
static void App_Capture_Start(void);
static void App_Capture_Pause(void);
static void App_Capture_Request_Callback(void *context);

/* ================================================== */

/**
 * @brief  发布当前采集状态到显示服务
 */
static void App_Capture_Publish_Status(void)
{
    GUI_Panel_Capture_State_t state = GUI_PANEL_CAPTURE_IDLE;

    if (app_capture.state == APP_CAPTURE_STATE_STARTING)
    {
        state = GUI_PANEL_CAPTURE_STARTING;
    }
    else if (app_capture.state == APP_CAPTURE_STATE_RUNNING)
    {
        state = GUI_PANEL_CAPTURE_RUNNING;
    }
    GUI_Panel_Set_Capture_Status(state, app_capture.progress_percent);
}

/**
 * @brief  等待旧文件与解析会话释放后启动本轮采集
 * @note   异步等待，10 秒超时后回到空闲态
 */
static bool App_Capture_Try_Start(void)
{
    if ((TickType_t)(xTaskGetTickCount() - app_capture.start_request_tick) >=
        pdMS_TO_TICKS(APP_CAPTURE_START_TIMEOUT_MS))
    {
        app_capture.state = APP_CAPTURE_STATE_IDLE;
        App_Capture_Publish_Status();
        (void)Log_Printf("ui: capture restart timeout\r\n");
        return false;
    }

    if (!File_Manager_Get_Snapshot(&app_capture_file_snapshot))
    {
        return false;
    }
    if (app_capture_file_snapshot.file_loaded)
    {
        if (!app_capture_file_snapshot.busy)
        {
            (void)File_Manager_Request_Exit();
        }
        return false;
    }
    if (app_capture_file_snapshot.busy)
    {
        return false;
    }

    app_capture.duration_cnt = Wave_Get_Capture_Duration();
    if (app_capture.duration_cnt == 0U || !Wave_Start_Capture(0U))
    {
        return false;
    }
    app_capture.elapsed_cnt = 0U;
    app_capture.progress_percent = 0U;
    app_capture.state = APP_CAPTURE_STATE_RUNNING;
    App_Capture_Publish_Status();
    return true;
}

/**
 * @brief  登记新采集请求并取消旧协议解析
 */
static void App_Capture_Start(void)
{
    if (Wave_Protocol_Layout_Is_Active())
    {
        Wave_Protocol_Results_Clear();
    }
    App_Protocol_Cancel();
    app_capture.start_request_tick = xTaskGetTickCount();
    app_capture.state = APP_CAPTURE_STATE_STARTING;
    app_capture.progress_percent = 0U;
    App_Capture_Publish_Status();
    (void)App_Capture_Try_Start();
}

/**
 * @brief  暂停采集并调度总览显示与协议解析
 * @note   停止失败保留运行态，解析等待采集终态
 */
static void App_Capture_Pause(void)
{
    Wave_Cnt_Range_t view_range;

    if (!Wave_Pause_Capture())
    {
        return;
    }
    App_Protocol_Request_Parse();
    app_capture.state = APP_CAPTURE_STATE_IDLE;
    if (Wave_Get_View_Range(&view_range))
    {
        app_capture.elapsed_cnt = view_range.cnt_end;
    }
    if (app_capture.elapsed_cnt > app_capture.duration_cnt)
    {
        app_capture.elapsed_cnt = app_capture.duration_cnt;
    }
    app_capture.progress_percent =
        app_capture.duration_cnt == 0U ?
            0U :
            (uint8_t)(((uint64_t)app_capture.elapsed_cnt * 100U) / app_capture.duration_cnt);
    App_Capture_Publish_Status();
}

/**
 * @brief  按应用状态处理采集按钮请求
 */
static void App_Capture_Request_Callback(void *context)
{
    (void)context;
    if (app_capture.state == APP_CAPTURE_STATE_RUNNING)
    {
        App_Capture_Pause();
    }
    else if (app_capture.state == APP_CAPTURE_STATE_IDLE)
    {
        App_Capture_Start();
    }
}

/* ================================================== */

/**
 * @brief  绑定采集视图的用户请求
 */
void App_Capture_Init(void)
{
    GUI_Panel_Bind_Capture(App_Capture_Request_Callback, NULL);
    App_Capture_Publish_Status();
}

/**
 * @brief  更新采集状态，并把硬件进度通知界面
 * @note   完成和硬件错误均结束本轮
 */
void App_Capture_Process(void)
{
    uint32_t elapsed_cnt;
    bool finished;

    if (app_capture.state == APP_CAPTURE_STATE_STARTING)
    {
        (void)App_Capture_Try_Start();
        return;
    }
    if (app_capture.state != APP_CAPTURE_STATE_RUNNING || app_capture.duration_cnt == 0U)
    {
        return;
    }
    if (!Wave_Get_Capture_Progress(&elapsed_cnt, &finished))
    {
        return;
    }

    app_capture.elapsed_cnt =
        elapsed_cnt > app_capture.duration_cnt ? app_capture.duration_cnt : elapsed_cnt;
    app_capture.progress_percent =
        (uint8_t)(((uint64_t)app_capture.elapsed_cnt * 100U) / app_capture.duration_cnt);
    if (finished && Wave_Complete_Capture())
    {
        app_capture.state = APP_CAPTURE_STATE_IDLE;
        App_Protocol_Request_Parse();
    }
    App_Capture_Publish_Status();
}
