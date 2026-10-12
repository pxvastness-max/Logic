/**
 * @file   file_manager.c
 * @brief  在独立FreeRTOS任务中执行TF卡目录、保存、删除和加载操作
 */

/* ================================================== */

#include "filesystem/file_manager.h"
#include "filesystem/filesystem.h"
#include "compression/compression_service.h"
#include "protocol/protocol_service.h"
#include "log/log_service.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"
#include <stdio.h>
#include <string.h>

/* ========================= 配置 ========================= */

#define FILE_MANAGER_TASK_STACK_DEPTH  1536U
#define FILE_MANAGER_TASK_PRIORITY     1U
#define FILE_MANAGER_QUEUE_LENGTH      6U
#define FILE_MANAGER_PATH_CAPACITY     128U
#define FILE_MANAGER_SAVE_ATTEMPTS     1000U
#define FILE_MANAGER_RETRY_COUNT       200U
#define FILE_MANAGER_RETRY_DELAY_TICKS pdMS_TO_TICKS(10U)

/* ================================================== */

typedef struct
{
    File_Manager_Operation_t operation;    /* 后台串行执行的命令类型 */
    char name[FILE_MANAGER_NAME_CAPACITY]; /* 请求时复制的根目录名称，不保留 GUI 字符串地址 */
} File_Manager_Command_t;

/* ================================================== */

static QueueHandle_t file_manager_queue;
static TaskHandle_t file_manager_task_handle;
static SemaphoreHandle_t file_manager_snapshot_mutex;
static File_Manager_Snapshot_t file_manager_snapshot;
/* 保存、删除、加载和退出请求共用一个保留位 */
static bool file_manager_operation_reserved;

/* ================================================== */

static void File_Manager_Task(void *argument);
static void File_Manager_Run_Command(const File_Manager_Command_t *command);
static FRESULT File_Manager_Mount(void);
static FRESULT File_Manager_Refresh_Files(void);
static void File_Manager_Set_Busy(File_Manager_Operation_t operation);
static void File_Manager_Set_Result(File_Manager_Operation_t operation,
                                    bool success,
                                    FRESULT filesystem_result,
                                    Compression_Result_t compression_result);
static File_Manager_Request_Result_t File_Manager_Queue(File_Manager_Operation_t operation,
                                                        const char *name);
static File_Manager_Request_Result_t File_Manager_Can_Accept(File_Manager_Operation_t operation);
static void File_Manager_Release_Reservation(File_Manager_Operation_t operation);
static bool File_Manager_Is_Valid_Name(const char *name);
static bool File_Manager_Is_DTFH(const char *name);
static void File_Manager_Sort_Files(void);
static FRESULT File_Manager_Delete_All(void);
static FRESULT File_Manager_Select_Save_Path(char *path, size_t capacity);
static Compression_Result_t File_Manager_Save(const char *path, Compression_File_Info_t *info);
static bool File_Manager_Stop_Protocol_For_Save(void);
static Compression_Result_t File_Manager_Load(const char *path, Compression_File_Info_t *info);
static LA_Service_Result_t File_Manager_Reset_Capture(void);

/* ================================================== */

/**
 * @brief  创建文件任务、命令队列和供界面读取的状态数据
 */
bool File_Manager_Init(void)
{
    if (file_manager_task_handle != NULL)
    {
        return true;
    }
    if (FileSystem_Init() != FR_OK || Compression_Service_Init() != 0U)
    {
        return false;
    }

    file_manager_snapshot_mutex = xSemaphoreCreateMutex();
    file_manager_queue = xQueueCreate(FILE_MANAGER_QUEUE_LENGTH, sizeof(File_Manager_Command_t));
    if (file_manager_snapshot_mutex == NULL || file_manager_queue == NULL)
    {
        if (file_manager_queue != NULL)
        {
            vQueueDelete(file_manager_queue);
            file_manager_queue = NULL;
        }
        if (file_manager_snapshot_mutex != NULL)
        {
            vSemaphoreDelete(file_manager_snapshot_mutex);
            file_manager_snapshot_mutex = NULL;
        }
        return false;
    }
    memset(&file_manager_snapshot, 0, sizeof(file_manager_snapshot));
    file_manager_operation_reserved = false;
    if (xTaskCreate(File_Manager_Task,
                    "files",
                    FILE_MANAGER_TASK_STACK_DEPTH,
                    NULL,
                    FILE_MANAGER_TASK_PRIORITY,
                    &file_manager_task_handle) != pdPASS)
    {
        vQueueDelete(file_manager_queue);
        file_manager_queue = NULL;
        vSemaphoreDelete(file_manager_snapshot_mutex);
        file_manager_snapshot_mutex = NULL;
        return false;
    }
    return true;
}

File_Manager_Request_Result_t File_Manager_Request(File_Manager_Operation_t operation,
                                                   const char *name)
{
    File_Manager_Request_Result_t result;

    if (operation < FILE_MANAGER_OPERATION_REFRESH || operation > FILE_MANAGER_OPERATION_EXIT)
    {
        return FILE_MANAGER_REQUEST_INVALID_OPERATION;
    }
    if ((operation == FILE_MANAGER_OPERATION_DELETE || operation == FILE_MANAGER_OPERATION_LOAD) &&
        !File_Manager_Is_Valid_Name(name))
    {
        return FILE_MANAGER_REQUEST_INVALID_NAME;
    }
    if (operation != FILE_MANAGER_OPERATION_REFRESH)
    {
        result = File_Manager_Can_Accept(operation);
        if (result != FILE_MANAGER_REQUEST_ACCEPTED)
        {
            return result;
        }
    }
    return File_Manager_Queue(operation, name);
}

bool File_Manager_Request_Refresh(void)
{
    return File_Manager_Request(FILE_MANAGER_OPERATION_REFRESH, NULL) ==
           FILE_MANAGER_REQUEST_ACCEPTED;
}

bool File_Manager_Request_Save(void)
{
    return File_Manager_Request(FILE_MANAGER_OPERATION_SAVE, NULL) == FILE_MANAGER_REQUEST_ACCEPTED;
}

bool File_Manager_Request_Delete(const char *name)
{
    return File_Manager_Request(FILE_MANAGER_OPERATION_DELETE, name) ==
           FILE_MANAGER_REQUEST_ACCEPTED;
}

bool File_Manager_Request_Delete_All(void)
{
    return File_Manager_Request(FILE_MANAGER_OPERATION_DELETE_ALL, NULL) ==
           FILE_MANAGER_REQUEST_ACCEPTED;
}

bool File_Manager_Request_Load(const char *name)
{
    return File_Manager_Request(FILE_MANAGER_OPERATION_LOAD, name) == FILE_MANAGER_REQUEST_ACCEPTED;
}

bool File_Manager_Request_Exit(void)
{
    return File_Manager_Request(FILE_MANAGER_OPERATION_EXIT, NULL) == FILE_MANAGER_REQUEST_ACCEPTED;
}

bool File_Manager_Get_Snapshot(File_Manager_Snapshot_t *snapshot)
{
    if (snapshot == NULL || file_manager_snapshot_mutex == NULL ||
        xSemaphoreTake(file_manager_snapshot_mutex, 0U) != pdTRUE)
    {
        return false;
    }
    *snapshot = file_manager_snapshot;
    (void)xSemaphoreGive(file_manager_snapshot_mutex);
    return true;
}

/* ================================================== */

/**
 * @brief  复制已经校验的命令和名称后不等待入队
 */
static File_Manager_Request_Result_t File_Manager_Queue(File_Manager_Operation_t operation,
                                                        const char *name)
{
    File_Manager_Command_t command;

    if (file_manager_queue == NULL)
    {
        return FILE_MANAGER_REQUEST_NOT_READY;
    }
    memset(&command, 0, sizeof(command));
    command.operation = operation;
    if (operation == FILE_MANAGER_OPERATION_DELETE || operation == FILE_MANAGER_OPERATION_LOAD)
    {
        memcpy(command.name, name, strlen(name) + 1U);
    }
    if (xQueueSend(file_manager_queue, &command, 0U) != pdPASS)
    {
        File_Manager_Release_Reservation(operation);
        return FILE_MANAGER_REQUEST_QUEUE_FULL;
    }
    return FILE_MANAGER_REQUEST_ACCEPTED;
}

/**
 * @brief  持有状态锁时检查文件模式，并预留唯一的写命令位置
 * @note   文件模式限制与正在执行的写操作分别报告
 */
static File_Manager_Request_Result_t File_Manager_Can_Accept(File_Manager_Operation_t operation)
{
    File_Manager_Request_Result_t result = FILE_MANAGER_REQUEST_ACCEPTED;

    if (file_manager_queue == NULL || file_manager_snapshot_mutex == NULL)
    {
        return FILE_MANAGER_REQUEST_NOT_READY;
    }
    if (xSemaphoreTake(file_manager_snapshot_mutex, 0U) != pdTRUE)
    {
        return FILE_MANAGER_REQUEST_LOCK_BUSY;
    }
    /* 刷新目录只读文件，因此刷新期间仍允许排队一个写操作 */
    if (file_manager_operation_reserved ||
        (file_manager_snapshot.busy &&
         file_manager_snapshot.operation != FILE_MANAGER_OPERATION_REFRESH))
    {
        result = FILE_MANAGER_REQUEST_BUSY;
    }
    else if (operation == FILE_MANAGER_OPERATION_EXIT && !file_manager_snapshot.file_loaded)
    {
        result = FILE_MANAGER_REQUEST_NOT_LOADED;
    }
    else if (operation != FILE_MANAGER_OPERATION_EXIT && file_manager_snapshot.file_loaded)
    {
        result = FILE_MANAGER_REQUEST_FILE_LOADED;
    }
    else
    {
        file_manager_operation_reserved = true;
    }
    (void)xSemaphoreGive(file_manager_snapshot_mutex);
    return result;
}

/* 释放改变文件或采集状态的命令占位 */
static void File_Manager_Release_Reservation(File_Manager_Operation_t operation)
{
    if (operation == FILE_MANAGER_OPERATION_REFRESH || file_manager_snapshot_mutex == NULL)
    {
        return;
    }
    if (xSemaphoreTake(file_manager_snapshot_mutex, portMAX_DELAY) == pdTRUE)
    {
        file_manager_operation_reserved = false;
        (void)xSemaphoreGive(file_manager_snapshot_mutex);
    }
}

static bool File_Manager_Is_Valid_Name(const char *name)
{
    size_t length;

    if (name == NULL)
    {
        return false;
    }
    length = strlen(name);
    if (length == 0U || length >= FILE_MANAGER_NAME_CAPACITY || strcmp(name, ".") == 0 ||
        strcmp(name, "..") == 0)
    {
        return false;
    }
    return strpbrk(name, "/\\:") == NULL;
}

static bool File_Manager_Is_DTFH(const char *name)
{
    size_t length;
    const char *extension;

    if (!File_Manager_Is_Valid_Name(name))
    {
        return false;
    }
    length = strlen(name);
    if (length < 5U)
    {
        return false;
    }
    extension = name + length - 5U;
    return (extension[0] == '.') && ((extension[1] == 'd') || (extension[1] == 'D')) &&
           ((extension[2] == 't') || (extension[2] == 'T')) &&
           ((extension[3] == 'f') || (extension[3] == 'F')) &&
           ((extension[4] == 'h') || (extension[4] == 'H'));
}

static void File_Manager_Task(void *argument)
{
    File_Manager_Command_t command;

    (void)argument;
    memset(&command, 0, sizeof(command));
    command.operation = FILE_MANAGER_OPERATION_REFRESH;
    File_Manager_Run_Command(&command);

    for (;;)
    {
        if (xQueueReceive(file_manager_queue, &command, portMAX_DELAY) == pdPASS)
        {
            File_Manager_Run_Command(&command);
        }
    }
}

/**
 * @brief  在文件任务中串行执行命令并发布状态
 * @note   只有文件任务访问文件和卷；状态锁只保护短时间的数据更新，不锁住整个读写过程
 */
static void File_Manager_Run_Command(const File_Manager_Command_t *command)
{
    char path[FILE_MANAGER_PATH_CAPACITY];
    FRESULT filesystem_result;
    Compression_Result_t compression_result = COMPRESSION_OK;
    bool success = false;

    if (command == NULL)
    {
        return;
    }
    File_Manager_Set_Busy(command->operation);
    if (command->operation == FILE_MANAGER_OPERATION_EXIT)
    {
        LA_Service_Result_t la_result = File_Manager_Reset_Capture();

        success = la_result == LA_SERVICE_OK;
        compression_result =
            success ? COMPRESSION_OK :
                      (la_result == LA_SERVICE_ERROR_BUSY ? COMPRESSION_ERROR_BUSY :
                                                            COMPRESSION_ERROR_LA_SERVICE);
        if (success && xSemaphoreTake(file_manager_snapshot_mutex, portMAX_DELAY) == pdTRUE)
        {
            file_manager_snapshot.file_loaded = false;
            file_manager_snapshot.exit_revision++;
            (void)xSemaphoreGive(file_manager_snapshot_mutex);
        }
        File_Manager_Set_Result(command->operation, success, FR_OK, compression_result);
        return;
    }
    filesystem_result = File_Manager_Mount();
    if (filesystem_result != FR_OK)
    {
        File_Manager_Set_Result(command->operation, false, filesystem_result, compression_result);
        return;
    }

    if (command->operation == FILE_MANAGER_OPERATION_REFRESH)
    {
        filesystem_result = File_Manager_Refresh_Files();
        success = filesystem_result == FR_OK;
    }
    else if (command->operation == FILE_MANAGER_OPERATION_SAVE)
    {
        Compression_File_Info_t info;

        filesystem_result = File_Manager_Select_Save_Path(path, sizeof(path));
        if (filesystem_result == FR_OK && !File_Manager_Stop_Protocol_For_Save())
        {
            memset(&info, 0, sizeof(info));
            info.la_result = LA_SERVICE_ERROR_BUSY;
            compression_result = COMPRESSION_ERROR_BUSY;
            filesystem_result = FR_OK;
        }
        else if (filesystem_result == FR_OK)
        {
            compression_result = File_Manager_Save(path, &info);
            filesystem_result = info.filesystem_result;
            success = compression_result == COMPRESSION_OK;
            if (success)
            {
                FRESULT refresh_result;

                refresh_result = File_Manager_Refresh_Files();
                if (refresh_result != FR_OK)
                {
                    filesystem_result = refresh_result;
                    success = false;
                }
            }
        }
    }
    else if (command->operation == FILE_MANAGER_OPERATION_DELETE)
    {
        int length = snprintf(path, sizeof(path), FILESYSTEM_ROOT_PATH "%s", command->name);
        FRESULT refresh_result;

        filesystem_result =
            ((length > 0) && ((size_t)length < sizeof(path))) ? f_unlink(path) : FR_INVALID_NAME;
        success = filesystem_result == FR_OK;
        /* 成败都重扫目录，让乐观删除的界面与TF卡真实内容重新同步 */
        refresh_result = File_Manager_Refresh_Files();
        if (success && refresh_result != FR_OK)
        {
            filesystem_result = refresh_result;
            success = false;
        }
    }
    else if (command->operation == FILE_MANAGER_OPERATION_DELETE_ALL)
    {
        filesystem_result = File_Manager_Delete_All();
        success = filesystem_result == FR_OK;
        if (success)
        {
            filesystem_result = File_Manager_Refresh_Files();
            success = filesystem_result == FR_OK;
        }
    }
    else if (command->operation == FILE_MANAGER_OPERATION_LOAD)
    {
        Compression_File_Info_t info;
        LA_Service_Info_t imported_capture;
        int length = snprintf(path, sizeof(path), FILESYSTEM_ROOT_PATH "%s", command->name);

        if ((length > 0) && ((size_t)length < sizeof(path)))
        {
            compression_result = File_Manager_Load(path, &info);
            filesystem_result = info.filesystem_result;
            success = compression_result == COMPRESSION_OK;
            if (success && (LA_Service_Get_Info(&imported_capture) != LA_SERVICE_OK ||
                            (imported_capture.state != LA_SERVICE_STATE_DONE &&
                             imported_capture.state != LA_SERVICE_STATE_ERROR)))
            {
                compression_result = COMPRESSION_ERROR_LA_SERVICE;
                success = false;
            }
            if (success && xSemaphoreTake(file_manager_snapshot_mutex, portMAX_DELAY) == pdTRUE)
            {
                /* 使用导入后的本机元数据，而不是只使用文件头的来源副本 */
                file_manager_snapshot.loaded_capture = imported_capture;
                file_manager_snapshot.file_loaded = true;
                file_manager_snapshot.load_revision++;
                (void)xSemaphoreGive(file_manager_snapshot_mutex);
            }
        }
        else
        {
            filesystem_result = FR_INVALID_NAME;
        }
    }
    else
    {
        filesystem_result = FR_INVALID_PARAMETER;
    }

    File_Manager_Set_Result(command->operation, success, filesystem_result, compression_result);
}

static FRESULT File_Manager_Mount(void)
{
    FRESULT result = FileSystem_Mount();

    if (xSemaphoreTake(file_manager_snapshot_mutex, portMAX_DELAY) == pdTRUE)
    {
        file_manager_snapshot.mounted = result == FR_OK;
        (void)xSemaphoreGive(file_manager_snapshot_mutex);
    }
    return result;
}

/**
 * @brief  在局部列表中完成扫描后一次性发布排序结果
 * @note   只有 f_opendir 成功才调用 f_closedir，扫描或关闭失败不覆盖上一份已发布列表
 */
static FRESULT File_Manager_Refresh_Files(void)
{
    File_Manager_Entry_t entries[FILE_MANAGER_MAX_FILES];
    DIR directory;
    FILINFO info;
    FRESULT result;
    uint16_t count = 0U;

    memset(entries, 0, sizeof(entries));
    result = f_opendir(&directory, FILESYSTEM_ROOT_PATH);
    if (result != FR_OK)
    {
        return result;
    }
    while (result == FR_OK && count < FILE_MANAGER_MAX_FILES)
    {
        size_t length;

        result = f_readdir(&directory, &info);
        if (result != FR_OK || info.fname[0] == '\0')
        {
            break;
        }
        if ((info.fattrib & AM_DIR) != 0U || !File_Manager_Is_DTFH(info.fname))
        {
            continue;
        }
        length = strlen(info.fname);
        if (length >= FILE_MANAGER_NAME_CAPACITY)
        {
            continue;
        }
        memcpy(entries[count].name, info.fname, length + 1U);
        entries[count].size_bytes = (uint64_t)info.fsize;
        count++;
    }
    if (result == FR_OK)
    {
        result = f_closedir(&directory);
    }
    else
    {
        (void)f_closedir(&directory);
    }
    if (result != FR_OK)
    {
        return result;
    }

    if (xSemaphoreTake(file_manager_snapshot_mutex, portMAX_DELAY) == pdTRUE)
    {
        memcpy(file_manager_snapshot.files, entries, sizeof(entries));
        file_manager_snapshot.file_count = count;
        File_Manager_Sort_Files();
        file_manager_snapshot.file_revision++;
        (void)xSemaphoreGive(file_manager_snapshot_mutex);
    }
    return FR_OK;
}

static void File_Manager_Sort_Files(void)
{
    uint16_t i;

    for (i = 1U; i < file_manager_snapshot.file_count; i++)
    {
        File_Manager_Entry_t entry = file_manager_snapshot.files[i];
        uint16_t position = i;

        while (position > 0U &&
               strcmp(file_manager_snapshot.files[position - 1U].name, entry.name) > 0)
        {
            file_manager_snapshot.files[position] = file_manager_snapshot.files[position - 1U];
            position--;
        }
        file_manager_snapshot.files[position] = entry;
    }
}

/**
 * @brief  枚举并逐个删除合法根目录 DTFH 文件
 */
static FRESULT File_Manager_Delete_All(void)
{
    for (;;)
    {
        DIR directory;
        FILINFO info;
        FRESULT result;
        char name[FILE_MANAGER_NAME_CAPACITY];
        char path[FILE_MANAGER_PATH_CAPACITY];
        bool found = false;

        name[0] = '\0';
        result = f_opendir(&directory, FILESYSTEM_ROOT_PATH);
        if (result != FR_OK)
        {
            return result;
        }
        while (result == FR_OK)
        {
            size_t length;

            result = f_readdir(&directory, &info);
            if (result != FR_OK || info.fname[0] == '\0')
            {
                break;
            }
            if ((info.fattrib & AM_DIR) != 0U || !File_Manager_Is_DTFH(info.fname))
            {
                continue;
            }
            length = strlen(info.fname);
            if (length >= sizeof(name))
            {
                continue;
            }
            memcpy(name, info.fname, length + 1U);
            found = true;
            break;
        }
        {
            FRESULT close_result = f_closedir(&directory);

            if (result == FR_OK && close_result != FR_OK)
            {
                result = close_result;
            }
        }
        if (result != FR_OK)
        {
            return result;
        }
        if (!found)
        {
            return FR_OK;
        }
        {
            int length = snprintf(path, sizeof(path), FILESYSTEM_ROOT_PATH "%s", name);

            if (length <= 0 || (size_t)length >= sizeof(path))
            {
                return FR_INVALID_NAME;
            }
        }
        result = f_unlink(path);
        if (result != FR_OK)
        {
            return result;
        }
    }
}

static void File_Manager_Set_Busy(File_Manager_Operation_t operation)
{
    if (xSemaphoreTake(file_manager_snapshot_mutex, portMAX_DELAY) == pdTRUE)
    {
        file_manager_snapshot.operation = operation;
        file_manager_snapshot.busy = true;
        file_manager_snapshot.last_success = false;
        file_manager_snapshot.filesystem_result = FR_OK;
        file_manager_snapshot.compression_result = COMPRESSION_OK;
        file_manager_snapshot.status_revision++;
        (void)xSemaphoreGive(file_manager_snapshot_mutex);
    }
}

static void File_Manager_Set_Result(File_Manager_Operation_t operation,
                                    bool success,
                                    FRESULT filesystem_result,
                                    Compression_Result_t compression_result)
{
    if (xSemaphoreTake(file_manager_snapshot_mutex, portMAX_DELAY) == pdTRUE)
    {
        file_manager_snapshot.operation = operation;
        file_manager_snapshot.busy = false;
        file_manager_snapshot.last_success = success;
        file_manager_snapshot.filesystem_result = (uint32_t)filesystem_result;
        file_manager_snapshot.compression_result = (uint32_t)compression_result;
        file_manager_snapshot.status_revision++;
        if (success && operation == FILE_MANAGER_OPERATION_SAVE)
        {
            file_manager_snapshot.save_revision++;
        }
        if (operation != FILE_MANAGER_OPERATION_REFRESH)
        {
            file_manager_operation_reserved = false;
        }
        (void)xSemaphoreGive(file_manager_snapshot_mutex);
    }
    if (!success)
    {
        (void)Log_Printf("file: failed op=%u fs=%u compression=%u\r\n",
                         (unsigned int)operation,
                         (unsigned int)filesystem_result,
                         (unsigned int)compression_result);
    }
}

/**
 * @brief  等待终态并选择不存在的正式文件与临时文件路径
 */
static FRESULT File_Manager_Select_Save_Path(char *path, size_t capacity)
{
    LA_Service_Info_t capture;
    uint16_t attempt;
    uint16_t retry;

    if (path == NULL || capacity == 0U)
    {
        return FR_INVALID_PARAMETER;
    }
    for (retry = 0U; retry < FILE_MANAGER_RETRY_COUNT; retry++)
    {
        if (LA_Service_Get_Info(&capture) == LA_SERVICE_OK &&
            (capture.state == LA_SERVICE_STATE_DONE || capture.state == LA_SERVICE_STATE_ERROR))
        {
            break;
        }
        vTaskDelay(FILE_MANAGER_RETRY_DELAY_TICKS);
    }
    if (retry == FILE_MANAGER_RETRY_COUNT)
    {
        return FR_NOT_READY;
    }

    for (attempt = 0U; attempt < FILE_MANAGER_SAVE_ATTEMPTS; attempt++)
    {
        char temporary[FILE_MANAGER_PATH_CAPACITY];
        int length = snprintf(path,
                              capacity,
                              FILESYSTEM_ROOT_PATH "capture_%08lX_%03u.dtfh",
                              (unsigned long)capture.capture_id,
                              (unsigned int)attempt);
        FRESULT result;

        if (length <= 0 || (size_t)length >= capacity)
        {
            return FR_INVALID_NAME;
        }
        result = f_stat(path, NULL);
        if (result == FR_OK)
        {
            continue;
        }
        if (result != FR_NO_FILE)
        {
            return result;
        }
        length = snprintf(temporary, sizeof(temporary), "%s.tmp", path);
        if (length <= 0 || (size_t)length >= sizeof(temporary))
        {
            return FR_INVALID_NAME;
        }
        result = f_stat(temporary, NULL);
        if (result == FR_NO_FILE)
        {
            return FR_OK;
        }
        if (result != FR_OK)
        {
            return result;
        }
    }
    return FR_EXIST;
}

/**
 * @brief  对共享工作区忙或采集未就绪进行有界保存重试
 */
static Compression_Result_t File_Manager_Save(const char *path, Compression_File_Info_t *info)
{
    Compression_Result_t result = COMPRESSION_ERROR_NOT_READY;
    uint16_t retry;

    memset(info, 0, sizeof(*info));
    for (retry = 0U; retry < FILE_MANAGER_RETRY_COUNT; retry++)
    {
        result = Compression_Service_Save(path, info);
        if (result != COMPRESSION_ERROR_BUSY && result != COMPRESSION_ERROR_NOT_READY)
        {
            break;
        }
        vTaskDelay(FILE_MANAGER_RETRY_DELAY_TICKS);
    }
    return result;
}

/**
 * @brief  保存前停止协议解析，等它不再占用当前采集数据
 */
static bool File_Manager_Stop_Protocol_For_Save(void)
{
    Protocol_Info_t protocol_info;
    uint16_t retry;

    if (Protocol_Service_Get_Info(&protocol_info) != PROTOCOL_OK)
    {
        return true;
    }
    if (protocol_info.state != PROTOCOL_STATE_STARTING &&
        protocol_info.state != PROTOCOL_STATE_RUNNING)
    {
        return true;
    }
    (void)Protocol_Service_Cancel();
    for (retry = 0U; retry < FILE_MANAGER_RETRY_COUNT; retry++)
    {
        if (Protocol_Service_Get_Info(&protocol_info) != PROTOCOL_OK ||
            (protocol_info.state != PROTOCOL_STATE_STARTING &&
             protocol_info.state != PROTOCOL_STATE_RUNNING))
        {
            return true;
        }
        vTaskDelay(FILE_MANAGER_RETRY_DELAY_TICKS);
    }
    (void)Log_Printf("file: protocol cancel timeout state=%u\r\n",
                     (unsigned int)protocol_info.state);
    return false;
}

/**
 * @brief  完整预校验后才清空旧采集并正式导入
 */
static Compression_Result_t File_Manager_Load(const char *path, Compression_File_Info_t *info)
{
    LA_Service_Result_t la_result;
    Compression_File_Info_t inspected;
    Compression_Result_t result;

    /* 先做纯读取校验文件损坏或拔卡时保留屏幕上的原采集，不能先Reset */
    memset(&inspected, 0, sizeof(inspected));
    result = Compression_Service_Inspect(path, &inspected);
    if (result != COMPRESSION_OK)
    {
        if (info != NULL)
        {
            *info = inspected;
        }
        return result;
    }

    la_result = File_Manager_Reset_Capture();
    if (la_result != LA_SERVICE_OK)
    {
        if (info != NULL)
        {
            *info = inspected;
            info->la_result = la_result;
        }
        return la_result == LA_SERVICE_ERROR_BUSY ? COMPRESSION_ERROR_BUSY :
                                                    COMPRESSION_ERROR_LA_SERVICE;
    }
    return Compression_Service_Load(path, info);
}

/**
 * @brief  取消协议解析，限时等待采集数据不再被占用，然后 Reset
 */
static LA_Service_Result_t File_Manager_Reset_Capture(void)
{
    Protocol_Info_t protocol_info;
    LA_Service_Info_t capture;
    LA_Service_Result_t la_result = LA_SERVICE_ERROR_BUSY;
    uint16_t retry;

    (void)Protocol_Service_Cancel();
    for (retry = 0U; retry < FILE_MANAGER_RETRY_COUNT; retry++)
    {
        if (Protocol_Service_Get_Info(&protocol_info) != PROTOCOL_OK ||
            (protocol_info.state != PROTOCOL_STATE_STARTING &&
             protocol_info.state != PROTOCOL_STATE_RUNNING))
        {
            break;
        }
        vTaskDelay(FILE_MANAGER_RETRY_DELAY_TICKS);
    }

    if (LA_Service_Get_Info(&capture) != LA_SERVICE_OK)
    {
        return LA_SERVICE_ERROR_STATE;
    }
    if (capture.state == LA_SERVICE_STATE_STARTING || capture.state == LA_SERVICE_STATE_RUNNING ||
        capture.state == LA_SERVICE_STATE_STOPPING || capture.state == LA_SERVICE_STATE_IMPORTING)
    {
        return LA_SERVICE_ERROR_BUSY;
    }
    if (capture.state != LA_SERVICE_STATE_IDLE)
    {
        for (retry = 0U; retry < FILE_MANAGER_RETRY_COUNT; retry++)
        {
            la_result = LA_Service_Reset();
            if (la_result != LA_SERVICE_ERROR_BUSY)
            {
                break;
            }
            vTaskDelay(FILE_MANAGER_RETRY_DELAY_TICKS);
        }
        if (la_result != LA_SERVICE_OK)
        {
            return la_result;
        }
    }
    return LA_SERVICE_OK;
}
