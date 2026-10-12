/**
 * @file   la_storage.h
 * @brief  逻辑分析仪采集数据存储头文件
 */

#ifndef LA_STORAGE_H
#define LA_STORAGE_H

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================== */

#include "stdint.h"

/* ================================================== */

#define LA_STORAGE_CHANNEL_COUNT 6U     /* 存储通道数量 */
#define LA_STORAGE_BLOCK_COUNT   11264U /* 22MB共享存储块数量 */

#define LA_STORAGE_BLOCK_CAPACITY 512U /* 每个存储块的时间戳数量 */

/* ================================================== */

typedef enum
{
    LA_STORAGE_OK = 0,
    LA_STORAGE_ERROR_INVALID_PARAMETER,
    LA_STORAGE_ERROR_FULL
} LA_Storage_Result_t;

/* ================================================== */

/**
 * @brief  初始化共享存储池的有效数量与分配游标
 * @note   SDRAM 已就绪后由 LA 服务串行调用，不清零整个 22 MiB 数据区
 */
void LA_Storage_Init(void);

/**
 * @brief  使全部通道记录与未提交预留失效
 * @note   调用者须独占存储，并确认没有其他任务还在读取或保留当前数据
 */
void LA_Storage_Reset(void);

/**
 * @brief  为单通道预留当前物理块内连续的可写空间
 * @param  count 预留数量 1..512，不得超过当前块剩余容量
 * @retval LA_STORAGE_OK 空间预留成功，但有效记录数尚未增加
 * @note   调用者持有存储锁，复制并校验源有效性后才能 Commit
 */
LA_Storage_Result_t LA_Storage_Reserve(uint8_t channel,
                                       uint16_t count,
                                       uint32_t **destination);

/**
 * @brief  将最近一次成功预留的已写记录发布给读者
 * @retval LA_STORAGE_OK 已增加有效计数并结束该通道预留
 * @note   调用者持有存储锁，必须保证写入完成且时间戳单调不减
 */
LA_Storage_Result_t LA_Storage_Commit(uint8_t channel, uint16_t count);

/**
 * @brief  读取指定通道已提交记录数量
 * @retval 已提交数量，无效通道返回 0
 * @note   调用者持有存储锁，预留但尚未 Commit 的记录不计入数量
 */
uint32_t LA_Storage_Get_Count(uint8_t channel);

/**
 * @brief  读取通道内一个已提交时间戳
 * @param  timestamp 非 NULL 的时间戳输出地址
 * @retval LA_STORAGE_OK 读取成功
 * @note   调用者持有存储锁，不返回内部块地址
 */
LA_Storage_Result_t LA_Storage_Get(uint8_t channel,
                                   uint32_t index,
                                   uint32_t *timestamp);

/**
 * @brief  按通道逻辑顺序复制一段已提交记录
 * @param  data 独立调用者缓冲
 * @retval LA_STORAGE_OK 全部记录复制完成
 * @note   调用者持有存储锁，每个物理块使用一次连续 memcpy
 */
LA_Storage_Result_t LA_Storage_Copy(uint8_t channel,
                                    uint32_t index,
                                    uint32_t *data,
                                    uint16_t count);

/**
 * @brief  二分查找第一个大于等于目标时间戳的已提交记录
 * @param  index 非 NULL 的下标输出，允许等于通道有效数量
 * @retval LA_STORAGE_OK 查找成功，无符合记录时返回数量作为末端下标
 * @note   调用者持有存储锁并保证时间戳单调不减，重复值返回第一个位置
 */
LA_Storage_Result_t LA_Storage_Lower_Bound(uint8_t channel,
                                           uint32_t timestamp,
                                           uint32_t *index);

/**
 * @brief  二分查找第一个严格大于目标时间戳的已提交记录
 * @param  index 非 NULL 的下标输出，允许等于通道有效数量
 * @retval LA_STORAGE_OK 查找成功
 * @note   调用者持有存储锁，重复目标值全部归入返回下标之前
 */
LA_Storage_Result_t LA_Storage_Upper_Bound(uint8_t channel,
                                           uint32_t timestamp,
                                           uint32_t *index);

/* ================================================== */

#ifdef __cplusplus
}
#endif

#endif /* LA_STORAGE_H */
