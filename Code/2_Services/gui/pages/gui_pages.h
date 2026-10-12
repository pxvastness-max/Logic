/**
 * @file   gui_pages.h
 * @brief  GUI 页面服务接口
 */

#ifndef GUI_PAGES_H
#define GUI_PAGES_H

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================== */

#include "lvgl.h"
#include "filesystem/file_manager.h"
#include "protocol/protocol_service.h"

/* ================================================== */

typedef File_Manager_Request_Result_t (*GUI_File_Request_Callback_t)(
    File_Manager_Operation_t operation,
    const char *name,
    void *context);
typedef bool (*GUI_Protocol_Get_Config_Callback_t)(Protocol_Config_t *config, void *context);
typedef void (*GUI_Protocol_Set_Config_Callback_t)(const Protocol_Config_t *config, void *context);

/* ================================================== */

/* 创建当前页面，控件对象全部归 page_parent 管理 */
void GUI_File_Page_Create(lv_obj_t *page_parent);
void GUI_Device_Page_Create(lv_obj_t *page_parent);
void GUI_System_Page_Create(lv_obj_t *page_parent);
void GUI_Protocol_Page_Create(lv_obj_t *page_parent);

/* 同步配置页显示，不提交采集请求 */
void GUI_Device_Page_Process(void);

/* 文件页面只提交操作请求，列表和结果由应用传入 */
void GUI_File_Bind_Request(GUI_File_Request_Callback_t callback, void *context);
void GUI_File_Update(const File_Manager_Snapshot_t *snapshot);

/* 协议弹窗编辑值通过回调交给应用保存，解析会话不由页面管理 */
void GUI_Protocol_Bind_Config(GUI_Protocol_Get_Config_Callback_t get_callback,
                              GUI_Protocol_Set_Config_Callback_t set_callback,
                              void *context);
void GUI_Protocol_Clear_Results(void);
void GUI_Protocol_Update_Results(const Protocol_Info_t *info);
void GUI_Protocol_Close_Dialog(void);

/* ================================================== */

#ifdef __cplusplus
}
#endif

#endif /* GUI_PAGES_H */
