/**
 * @file   file_manager.h
 * @brief  GUI文件列表、手动保存、删除、加载和退出的异步任务接口
 */

#ifndef FILE_MANAGER_H
#define FILE_MANAGER_H

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================== */

#include <stdbool.h>
#include <stdint.h>
#include "la/la_service.h"

/* ========================= 配置 ========================= */

#define FILE_MANAGER_MAX_FILES     32U
#define FILE_MANAGER_NAME_CAPACITY 96U

/* ================================================== */

typedef enum
{
    FILE_MANAGER_OPERATION_NONE = 0,
    FILE_MANAGER_OPERATION_REFRESH,
    FILE_MANAGER_OPERATION_SAVE,
    FILE_MANAGER_OPERATION_DELETE,
    FILE_MANAGER_OPERATION_DELETE_ALL,
    FILE_MANAGER_OPERATION_LOAD,
    FILE_MANAGER_OPERATION_EXIT
} File_Manager_Operation_t;

/* 请求返回值只表示是否入队；实际执行结果要另外读取 */
typedef enum
{
    FILE_MANAGER_REQUEST_ACCEPTED = 0,
    FILE_MANAGER_REQUEST_NOT_READY,
    FILE_MANAGER_REQUEST_INVALID_OPERATION,
    FILE_MANAGER_REQUEST_INVALID_NAME,
    FILE_MANAGER_REQUEST_LOCK_BUSY,
    FILE_MANAGER_REQUEST_BUSY,
    FILE_MANAGER_REQUEST_FILE_LOADED,
    FILE_MANAGER_REQUEST_NOT_LOADED,
    FILE_MANAGER_REQUEST_QUEUE_FULL
} File_Manager_Request_Result_t;

typedef struct
{
    char name[FILE_MANAGER_NAME_CAPACITY]; /* 根目录 UTF-8 文件名及 NUL，不包含 0:/ 前缀 */
    uint64_t size_bytes;                   /* 目录扫描时的文件字节数 */
} File_Manager_Entry_t;

typedef struct
{
    /* 排序后的列表副本，只有前 file_count 项有效 */
    File_Manager_Entry_t files[FILE_MANAGER_MAX_FILES];
    LA_Service_Info_t loaded_capture;   /* 成功导入后的本机采集元数据，file_loaded 为真时使用 */
    uint32_t file_revision;             /* 成功目录刷新时增加，GUI 用于判断列表变化 */
    uint32_t status_revision;           /* 后台操作开始或结束时增加 */
    uint32_t save_revision;             /* 保存及后续刷新均成功时增加 */
    uint32_t load_revision;             /* 正式导入成功时增加 */
    uint32_t exit_revision;             /* 清空已加载采集并退出文件模式时增加 */
    uint32_t filesystem_result;         /* 最近操作的 FatFs FRESULT 值 */
    uint32_t compression_result;        /* 最近操作的 Compression_Result_t 值 */
    uint16_t file_count;                /* 有效列表数量 0..FILE_MANAGER_MAX_FILES */
    File_Manager_Operation_t operation; /* 当前或最近一次执行的命令类型 */
    bool mounted;                       /* 最近挂载检查是否成功，不代替物理插拔检测 */
    bool busy;                          /* 后台正在执行命令，异步请求的返回值不表示其完成 */
    bool last_success;                  /* 最近操作是否成功，开始执行新命令时先清零 */
    bool file_loaded;                   /* 已成功进入文件数据模式，退出成功后清零 */
} File_Manager_Snapshot_t;

/* ================================================== */

/**
 * @brief  初始化文件系统、压缩服务、命令队列和文件后台任务
 * @retval true 已完成初始化，重复调用不重复创建任务
 * @note   启动时串行调用，LA/SDRAM 与硬件初始化必须先完成
 */
bool File_Manager_Init(void);

/**
 * @brief  不等待提交文件命令并返回本次请求的具体结果
 * @param  operation REFRESH 至 EXIT 范围内的命令类型
 * @retval FILE_MANAGER_REQUEST_ACCEPTED 命令及名称副本已入队
 * @note   任务上下文调用，结果随调用返回，不使用可能被其他调用覆盖的全局错误状态
 */
File_Manager_Request_Result_t File_Manager_Request(File_Manager_Operation_t operation,
                                                   const char *name);

/**
 * @brief  不等待提交一次根目录 DTFH 文件列表刷新
 * @retval true 请求已复制到队列，后台稍后执行
 * @note   任务上下文调用，成功不代表卡已挂载或扫描已经完成
 */
bool File_Manager_Request_Refresh(void);

/**
 * @brief  请求后台保存最近一次已结束的采集
 * @retval true 已保留命令位置并入队
 * @note   任务上下文调用，允许排在只读 REFRESH 之后，重复写操作不能同时入队
 */
bool File_Manager_Request_Save(void);

/**
 * @brief  提交删除根目录中一个指定文件的请求
 * @retval true 文件名已复制到队列并取得操作保留位
 * @note   任务上下文调用，不保留 name 地址，返回后调用者可复用名称缓冲
 */
bool File_Manager_Request_Delete(const char *name);

/**
 * @brief  提交删除根目录全部合法 DTFH 文件的请求
 * @retval true 请求已入队
 * @note   任务上下文调用，只删除识别出的 DTFH 文件，不删除子目录或其他文件
 */
bool File_Manager_Request_Delete_All(void);

/**
 * @brief  提交校验并导入指定根目录 DTFH 文件的请求
 * @retval true 名称副本已经入队并保留操作位置
 * @note   任务上下文调用，后台先 Inspect，校验失败时保留原采集
 */
bool File_Manager_Request_Load(const char *name);

/**
 * @brief  提交退出文件模式并清空导入数据的请求
 * @retval true 已加载文件状态下请求入队
 * @note   任务上下文调用，后台取消协议并等待 Reset 的数据保留条件解除
 */
bool File_Manager_Request_Exit(void);

/**
 * @brief  非阻塞复制文件列表与后台操作状态
 * @param  snapshot 非 NULL 的调用者输出结构，成功后可独立保存和读取
 * @retval true 已在锁保护下复制文件列表和操作状态
 * @note   只能在任务中调用；失败时不改写 snapshot，GUI 可以稍后重试
 */
bool File_Manager_Get_Snapshot(File_Manager_Snapshot_t *snapshot);

/* ================================================== */

#ifdef __cplusplus
}
#endif

#endif /* FILE_MANAGER_H */
