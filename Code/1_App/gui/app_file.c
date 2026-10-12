/**
 * @file   app_file.c
 * @brief  文件请求与结果调度
 */

/* ================================================== */

#include "gui/app_file.h"
#include "gui/app_protocol.h"
#include "gui/pages/gui_pages.h"
#include "filesystem/file_manager.h"
#include "waveform/wave_service.h"

/* ================================================== */

typedef struct
{
    File_Manager_Snapshot_t snapshot; /* 最近一次读取到的完整文件列表和操作状态 */
    uint32_t handled_save_revision;   /* 已处理的保存完成事件 */
    uint32_t handled_load_revision;   /* 已处理的导入完成事件 */
    uint32_t handled_exit_revision;   /* 已处理的退出完成事件 */
} App_File_Handle_t;

/* ================================================== */

static App_File_Handle_t app_file;

/* ================================================== */

static File_Manager_Request_Result_t App_File_Request_Callback(File_Manager_Operation_t operation,
                                                               const char *name,
                                                               void *context);

/* ================================================== */

/**
 * @brief  校验应用状态后提交文件请求
 * @note   入队成功不代表文件操作完成
 */
static File_Manager_Request_Result_t App_File_Request_Callback(File_Manager_Operation_t operation,
                                                               const char *name,
                                                               void *context)
{
    (void)context;
    if ((operation == FILE_MANAGER_OPERATION_SAVE || operation == FILE_MANAGER_OPERATION_LOAD) &&
        Wave_Is_Capturing())
    {
        return FILE_MANAGER_REQUEST_BUSY;
    }
    return File_Manager_Request(operation, name);
}

/* ================================================== */

/**
 * @brief  绑定文件页面的操作请求，并显示现有文件列表
 */
void App_File_Init(void)
{
    GUI_File_Bind_Request(App_File_Request_Callback, NULL);
    if (File_Manager_Get_Snapshot(&app_file.snapshot))
    {
        GUI_File_Update(&app_file.snapshot);
    }
}

/**
 * @brief  只处理当前操作的文件结果，并更新界面
 * @note   页面关闭后仍处理结果，同一修订号只应用一次
 */
void App_File_Process(void)
{
    const File_Manager_Snapshot_t *snapshot = &app_file.snapshot;

    if (!File_Manager_Get_Snapshot(&app_file.snapshot))
    {
        return;
    }
    if (snapshot->load_revision != 0U && snapshot->load_revision != app_file.handled_load_revision)
    {
        if (Wave_Use_Loaded_Capture(snapshot->loaded_capture.timestamp_hz,
                                    snapshot->loaded_capture.cutoff_tick))
        {
            app_file.handled_load_revision = snapshot->load_revision;
            App_Protocol_Request_Parse();
        }
    }
    if (snapshot->save_revision != 0U && snapshot->save_revision != app_file.handled_save_revision)
    {
        app_file.handled_save_revision = snapshot->save_revision;
        App_Protocol_Request_Parse();
    }
    if (snapshot->exit_revision != 0U && snapshot->exit_revision != app_file.handled_exit_revision)
    {
        app_file.handled_exit_revision = snapshot->exit_revision;
        (void)Wave_Exit_Loaded_Capture();
    }
    GUI_File_Update(snapshot);
}
