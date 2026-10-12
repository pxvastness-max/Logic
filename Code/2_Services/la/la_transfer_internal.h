/**
 * @file   la_transfer_internal.h
 * @brief  逻辑分析仪采集数据存储搬运适配私有接口
 */

#ifndef LA_TRANSFER_INTERNAL_H
#define LA_TRANSFER_INTERNAL_H

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================== */

#include "la/la_service.h"

/* ================================================== */

/* 校验源缓冲区并原子提交，调用时已持有storage_mutex */
typedef LA_Service_Result_t (*LA_Transfer_Commit_Callback_t)(uint8_t channel,
                                                             uint16_t count,
                                                             void *context);

/* ================================================== */

/**
 * @brief  同步追加严格早于截止 CNT 的 DMA 时间戳
 * @param  data 单调不减的 DMA 源缓冲，count 非零时不能为空
 * @retval LA_SERVICE_OK 有效记录已提交，或截止前没有记录
 * @note   调用者持有 storage_mutex，保持源数据有效直至返回
 */
LA_Service_Result_t LA_Transfer_Append(uint8_t channel,
                                      const uint32_t *data,
                                      uint16_t count,
                                      uint32_t cutoff_tick);

/**
 * @brief  CPU 复制后由采集任务复核源半区并提交
 * @param  data 单调不减的 DMA 源缓冲，count 非零时不能为空
 * @retval LA_SERVICE_OK 复制与回调成功
 * @note   调用者持有 storage_mutex，中断可在复制期间标记源半区已被 DMA 复用
 */
LA_Service_Result_t LA_Transfer_Append_Checked(
        uint8_t channel,
        const uint32_t *data,
        uint16_t count,
        uint32_t cutoff_tick,
        LA_Transfer_Commit_Callback_t commit,
        void *context);

/* ================================================== */

#ifdef __cplusplus
}
#endif

#endif /* LA_TRANSFER_INTERNAL_H */
