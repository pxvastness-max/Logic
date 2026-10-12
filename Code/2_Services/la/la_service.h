/**
 * @file   la_service.h
 * @brief  逻辑分析仪服务头文件
 */

#ifndef LA_SERVICE_H
#define LA_SERVICE_H

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================== */

#include "stdint.h"

/* ================================================== */

#define LA_SERVICE_CHANNEL_COUNT 6U /* 逻辑分析仪通道数量 */

/* ================================================== */

typedef enum
{
    LA_SERVICE_CHANNEL_1 = 0,
    LA_SERVICE_CHANNEL_2,
    LA_SERVICE_CHANNEL_3,
    LA_SERVICE_CHANNEL_4,
    LA_SERVICE_CHANNEL_5,
    LA_SERVICE_CHANNEL_6
} LA_Service_Channel_t;

typedef enum
{
    LA_SERVICE_STATE_RESET = 0, /* 服务资源尚未初始化 */
    LA_SERVICE_STATE_IDLE,      /* 可接受新采集或文件导入 */
    LA_SERVICE_STATE_STARTING,  /* 服务已接受请求，BSP 启动结果尚未发布 */
    LA_SERVICE_STATE_RUNNING,   /* 定时器和 DMA 正在采集，可读取当前数据 */
    LA_SERVICE_STATE_STOPPING,  /* 硬件已请求停止，后台仍在搬运和确认停稳 */
    LA_SERVICE_STATE_DONE,      /* 正常终态，可保存、解析或保留数据 */
    LA_SERVICE_STATE_ERROR,     /* 错误终态，可检查已有数据，重新采集前须 Reset */
    LA_SERVICE_STATE_IMPORTING  /* 当前任务独占文件导入，数据尚未发布 */
} LA_Service_State_t;

typedef enum
{
    LA_SERVICE_STOP_NONE = 0,
    LA_SERVICE_STOP_DEADLINE,
    LA_SERVICE_STOP_USER,
    LA_SERVICE_STOP_STORAGE_FULL,
    LA_SERVICE_STOP_DMA_OVERRUN,
    LA_SERVICE_STOP_DMA_ERROR,
    LA_SERVICE_STOP_TIM_OVERCAPTURE,
    LA_SERVICE_STOP_HARDWARE_ERROR,
    LA_SERVICE_STOP_TRANSFER_TIMEOUT /* 保留的搬运超时原因，当前 CPU 同步路径不使用异步等待 */
} LA_Service_Stop_Reason_t;

typedef enum
{
    LA_SERVICE_OK = 0,
    LA_SERVICE_ERROR_BUSY,
    LA_SERVICE_ERROR_INVALID_PARAMETER,
    LA_SERVICE_ERROR_NOT_READY,
    LA_SERVICE_ERROR_HARDWARE,
    LA_SERVICE_ERROR_STORAGE,
    LA_SERVICE_ERROR_STATE,
    LA_SERVICE_ERROR_TIMEOUT
} LA_Service_Result_t;

typedef enum
{
    LA_SERVICE_EVENT_COMPLETE = 0,
    LA_SERVICE_EVENT_ERROR
} LA_Service_Event_Type_t;

typedef struct
{
    LA_Service_State_t state;                      /* 服务当前状态 */
    LA_Service_Stop_Reason_t stop_reason;          /* 采集停止原因 */
    uint32_t capture_id;                           /* 非零采集代数，复位后不回退 */
    uint32_t timestamp_hz;                         /* 实际时间戳频率 */
    uint32_t deadline_tick;                        /* 配置的采集截止计数值 */
    uint32_t cutoff_tick;                          /* 实际采集截止计数值 */
    uint32_t overcapture_mask;                     /* TIM捕获溢出通道位图 */
    uint32_t data_count[LA_SERVICE_CHANNEL_COUNT]; /* 六路已存储时间戳数量 */
    uint8_t initial_levels;                        /* 六路采集初始电平位图 */
    uint8_t finalize_failed;                       /* BSP收尾失败，不能据此认为硬件已停稳 */
} LA_Service_Info_t;

typedef struct
{
    uint32_t next_timestamp; /* 第一个严格大于窗口结束值的翻转时间戳 */
    uint8_t has_transition;  /* 窗口内是否存在翻转 */
    uint8_t end_level;       /* 窗口内最后一次翻转后的电平 */
    uint8_t has_next;        /* 是否存在next_timestamp */
} LA_Service_Window_Result_t;

typedef struct
{
    uint32_t first_index; /* 第一个大于等于范围起点的记录下标 */
    uint32_t after_index; /* 第一个大于范围终点的记录下标 */
    uint8_t start_level;  /* 范围起点发生翻转前的电平 */
} LA_Service_Range_Result_t;

typedef struct
{
    uint32_t token; /* 由Begin/End管理，不得复制并当作独立视图使用 */
} LA_Service_Query_View_t;

typedef struct
{
    uint32_t token;      /* 单实例数据保留凭据，不得重复释放 */
    uint32_t capture_id; /* 保留的采集代数 */
} LA_Service_Capture_Lease_t;

typedef struct
{
    uint32_t token; /* 由Begin/Commit/Abort管理，不得复制或跨任务使用 */
} LA_Service_Import_t;

typedef struct
{
    LA_Service_Event_Type_t type; /* 服务事件类型 */
    LA_Service_Info_t info;       /* 事件发生时的采集信息 */
} LA_Service_Event_t;

/* 回调由逻辑分析仪服务任务调用，event指针只在回调执行期间有效 */
typedef void (*LA_Service_Event_Callback_t)(const LA_Service_Event_t *event, void *context);

/* ================================================== */

/**
 * @brief  初始化六路采集服务的存储、互斥锁和后台任务
 * @retval 0 初始化成功或此前已完成初始化
 * @note   由启动代码串行调用，BSP 输入捕获与 SDRAM 必须已经就绪
 */
uint8_t LA_Service_Init(void);

/**
 * @brief  登记采集终态的任务上下文回调
 * @note   回调由 LA 后台任务执行，event 只在该次回调期间有效
 */
void LA_Service_Set_Event_Callback(LA_Service_Event_Callback_t callback, void *context);

/**
 * @brief  从 IDLE 启动六路同步双边沿采集
 * @param  duration_us 请求的采集时长
 * @retval LA_SERVICE_OK 启动被接受
 * @note   只在服务初始化后的任务上下文调用，会清空当前存储并分配新采集编号
 */
LA_Service_Result_t LA_Service_Start(uint32_t timestamp_hz,
                                     uint32_t duration_us,
                                     uint8_t input_filter);

/**
 * @brief  请求停止 RUNNING 采集并由后台任务提交剩余有效数据
 * @retval LA_SERVICE_OK 已取得硬件截止 CNT，并进入 STOPPING
 * @note   任务上下文调用，返回成功不代表数据收尾完成
 */
LA_Service_Result_t LA_Service_Stop(void);

/**
 * @brief  丢弃当前采集数据并恢复 IDLE
 * @retval LA_SERVICE_OK 硬件复位成功，记录数与采集信息已经清空
 * @note   任务上下文调用，立即尝试查询锁后等待存储锁，不允许在持有视图时调用
 */
LA_Service_Result_t LA_Service_Reset(void);

/**
 * @brief  复制当前采集状态及六路已提交记录数量
 * @param  info 非 NULL 的输出结构，由调用者持有
 * @retval LA_SERVICE_OK 信息复制成功
 * @note   只能在任务中调用；复制时锁住存储，返回这次读取的数据副本
 */
LA_Service_Result_t LA_Service_Get_Info(LA_Service_Info_t *info);

/**
 * @brief  读取硬件采集进度或终态实际截止值
 * @param  current_tick 非 NULL 的 CNT 输出地址
 * @retval LA_SERVICE_OK RUNNING 返回 TIM2 当前 CNT
 * @note   任务上下文调用，返回值使用 Get_Info 中的实际 timestamp_hz 解释
 */
LA_Service_Result_t LA_Service_Get_Current_Tick(uint32_t *current_tick);

/**
 * @brief  查询六路在指定时间戳翻转前的电平
 * @param  levels 非 NULL 的输出地址，bit0..5 对应通道 0..5
 * @retval LA_SERVICE_OK 六路查询成功，输出位图有效
 * @note   只能在任务中调用；读取时会锁住存储，并按需复制各通道当前的 DMA 数据
 */
LA_Service_Result_t LA_Service_Get_Levels(uint32_t timestamp, uint8_t *levels);

/**
 * @brief  查找指定通道在起止时间内的边沿下标，包含两个端点
 * @param  result 非 NULL 的输出
 * @retval LA_SERVICE_OK 查询成功，两个下标相等表示范围内无边沿
 * @note   任务上下文调用，start_level 为起点恰好翻转之前的电平
 */
LA_Service_Result_t LA_Service_Query_Range(LA_Service_Channel_t channel,
                                           uint32_t start_tick,
                                           uint32_t end_tick,
                                           LA_Service_Range_Result_t *result);

/**
 * @brief  从通道的已存储记录与运行中 DMA 尾部批量复制时间戳
 * @param  data 调用者独立输出缓冲
 * @retval LA_SERVICE_OK 复制成功，数量可能小于请求上限
 * @note   只能在任务中调用；不保存 data 地址，查询期间只复制一次当前 DMA 数据
 */
LA_Service_Result_t LA_Service_Get_Data(LA_Service_Channel_t channel,
                                        uint32_t start_index,
                                        uint32_t *data,
                                        uint16_t max_count,
                                        uint16_t *data_count);

/**
 * @brief  保留已结束采集并取得与该采集一致的元数据
 * @param  lease 输出数据保留凭证，不能为 NULL
 * @retval LA_SERVICE_OK 已保留本次采集数据
 */
LA_Service_Result_t LA_Service_Capture_Acquire(
                                        LA_Service_Capture_Lease_t *lease,
                                        LA_Service_Info_t *info);

/**
 * @brief  释放这次采集的数据保留权，之后才允许 Reset
 * @retval LA_SERVICE_OK 释放成功
 * @note   在任务上下文调用，不要求与 Acquire 为同一任务
 */
LA_Service_Result_t LA_Service_Capture_Release(
                                        LA_Service_Capture_Lease_t *lease);

/**
 * @brief  只读检查文件采集元数据和共享存储块容量
 * @retval LA_SERVICE_OK 元数据与六路占块总量合法
 * @note   不依赖服务初始化或硬件，不修改存储与调用者结构
 */
LA_Service_Result_t LA_Service_Import_Validate(const LA_Service_Info_t *metadata);

/**
 * @brief  在 IDLE 建立由当前任务独占的采集导入事务
 * @param  import 非 NULL 的导入凭据输出，不能复制成独立事务或跨任务使用
 * @retval LA_SERVICE_OK BSP 已确认停止
 * @note   任务上下文调用，加载旧数据前须由上层显式 Reset
 */
LA_Service_Result_t LA_Service_Import_Begin(
                                        LA_Service_Import_t *import,
                                        const LA_Service_Info_t *metadata);

/**
 * @brief  向当前导入事务追加单通道绝对时间戳
 * @param  data 调用者独立缓冲，count 非零时必须有效，不得重叠内部存储
 * @retval LA_SERVICE_OK 本批同步复制和提交完成
 */
LA_Service_Result_t LA_Service_Import_Append(
                                        const LA_Service_Import_t *import,
                                        LA_Service_Channel_t channel,
                                        const uint32_t *data,
                                        uint16_t count);

/**
 * @brief  发布完整导入数据并结束事务
 * @retval LA_SERVICE_OK 六路数量完整，发布来源状态并分配本机新采集编号
 * @note   任务上下文调用，失败不会替调用者 Abort，应显式清理活动事务
 */
LA_Service_Result_t LA_Service_Import_Commit(LA_Service_Import_t *import);

/**
 * @brief  丢弃本次导入的部分记录并恢复 IDLE
 * @retval LA_SERVICE_OK 部分存储及来源信息已经清空
 * @note   同一任务在 Begin 后的失败出口调用，不恢复 Begin 前已丢弃的旧采集
 */
LA_Service_Result_t LA_Service_Import_Abort(LA_Service_Import_t *import);

/**
 * @brief  建立由当前任务持有的六通道固定查询视图
 * @param  view 非 NULL 的视图输出，成功后负责配对调用 End
 * @retval LA_SERVICE_OK 已固定本次查询的数据范围，查询锁保持到 End
 * @note   任务上下文阻塞等待查询锁及存储锁，成功后只能由同一任务查询/End
 */
LA_Service_Result_t LA_Service_Query_Begin(LA_Service_Query_View_t *view);

/**
 * @brief  在总等待预算内建立固定查询视图
 * @param  view 非 NULL 的视图输出，拥有者与释放规则同 Begin
 * @retval LA_SERVICE_OK 成功，调用者持有查询锁
 */
LA_Service_Result_t LA_Service_Query_Begin_Timeout(
                                        LA_Service_Query_View_t *view,
                                        uint32_t timeout_ms);

/**
 * @brief  不等待地尝试建立固定查询视图
 * @param  view 非 NULL 的视图输出
 * @retval LA_SERVICE_OK 建立成功，必须由当前任务配对 End
 */
LA_Service_Result_t LA_Service_Query_Try_Begin(LA_Service_Query_View_t *view);

/**
 * @brief  使固定查询视图失效并释放查询锁
 * @retval LA_SERVICE_OK 释放成功
 */
LA_Service_Result_t LA_Service_Query_End(LA_Service_Query_View_t *view);

/**
 * @brief  在固定的数据视图中查找起止时间内的边沿，包含两个端点
 * @param  result 非 NULL 的下标范围和起点翻转前电平输出
 * @retval LA_SERVICE_OK 查询成功
 * @note   不重新读取 DMA，也不获取新的视图锁，结果使用 Begin 时的固定数量
 */
LA_Service_Result_t LA_Service_Query_View_Range(
                                        const LA_Service_Query_View_t *view,
                                        LA_Service_Channel_t channel,
                                        uint32_t start_tick,
                                        uint32_t end_tick,
                                        LA_Service_Range_Result_t *result);

/**
 * @brief  在视图指定下标区间内二分查找严格大于目标 CNT 的记录
 * @param  index 非 NULL 的结果输出，允许等于 maximum_index
 * @retval LA_SERVICE_OK 查找成功
 * @note   minimum_index 必须不大于 maximum_index，调用者负责提供有序记录区间
 */
LA_Service_Result_t LA_Service_Query_View_Upper_Bound(
                                        const LA_Service_Query_View_t *view,
                                        LA_Service_Channel_t channel,
                                        uint32_t timestamp,
                                        uint32_t minimum_index,
                                        uint32_t maximum_index,
                                        uint32_t *index);

/**
 * @brief  从固定视图同步复制一批绝对时间戳
 * @param  data 调用者独立输出缓冲
 * @retval LA_SERVICE_OK 复制成功，只处理实际返回的记录数量
 */
LA_Service_Result_t LA_Service_Query_View_Get_Data(
                                        const LA_Service_Query_View_t *view,
                                        LA_Service_Channel_t channel,
                                        uint32_t start_index,
                                        uint32_t *data,
                                        uint16_t max_count,
                                        uint16_t *data_count);

/**
 * @brief  查询单通道闭时间窗口内的翻转及后续边沿
 * @param  result 非 NULL 的窗口输出
 * @retval LA_SERVICE_OK 查询成功
 * @note   任务上下文调用，end_level 为窗口所有边沿处理后的电平
 */
LA_Service_Result_t LA_Service_Query_Window(LA_Service_Channel_t channel,
                                            uint32_t start_tick,
                                            uint32_t end_tick,
                                            LA_Service_Window_Result_t *result);

/* ================================================== */

#ifdef __cplusplus
}
#endif

#endif /* LA_SERVICE_H */
