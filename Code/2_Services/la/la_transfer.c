/**
 * @file   la_transfer.c
 * @brief  逻辑分析仪采集数据存储搬运适配实现
 */

/* ================================================== */

#include "la/la_transfer_internal.h"
#include "la/la_storage.h"
#include "memory/platform_memory.h"
#include "stddef.h"
#include <string.h>

/* ================================================== */

/**
 * @brief  取得截止计数值之前的有效记录数量
 */
static uint16_t LA_Transfer_Get_Valid_Count(const uint32_t *data,
                                            uint16_t count,
                                            uint32_t cutoff_tick)
{
    uint16_t left = 0U;
    uint16_t right = count;

    if ((count == 0U) || (data[0] >= cutoff_tick))
    {
        return 0U;
    }
    if (data[count - 1U] < cutoff_tick)
    {
        return count;
    }

    while (left < right)
    {
        uint16_t middle = (uint16_t)(left + ((right - left) / 2U));

        if (data[middle] < cutoff_tick)
        {
            left = (uint16_t)(middle + 1U);
        }
        else
        {
            right = middle;
        }
    }
    return left;
}

/**
 * @brief  使用CPU完成一段时间戳到SDRAM的同步搬运
 */
static LA_Service_Result_t LA_Transfer_Copy(const uint32_t *source,
                                           uint32_t *destination,
                                           uint16_t count)
{
    if ((source == NULL) || (destination == NULL) || (count == 0U))
    {
        return LA_SERVICE_ERROR_HARDWARE;
    }

    memcpy(destination, source, (size_t)count * sizeof(*source));

    /* 先完成对SDRAM的所有写入，再允许校验源半区并发布存储记录 */
    Platform_Memory_Barrier();
    return LA_SERVICE_OK;
}

/* ================================================== */

/**
 * @brief  将一段有效时间戳通过CPU追加到通道存储
 * @note   调用者必须持有存储互斥锁
 */
LA_Service_Result_t LA_Transfer_Append(uint8_t channel,
                                      const uint32_t *data,
                                      uint16_t count,
                                      uint32_t cutoff_tick)
{
    return LA_Transfer_Append_Checked(channel, data, count, cutoff_tick,
                                      NULL, NULL);
}

/**
 * @brief  CPU复制后校验源数据有效性并提交
 * @note   中断可在CPU复制期间标记半区污染，受污染记录不得发布
 */
LA_Service_Result_t LA_Transfer_Append_Checked(
        uint8_t channel,
        const uint32_t *data,
        uint16_t count,
        uint32_t cutoff_tick,
        LA_Transfer_Commit_Callback_t commit,
        void *context)
{
    uint32_t *destination;
    uint16_t valid_count;
    LA_Service_Result_t service_result;

    if ((channel >= LA_STORAGE_CHANNEL_COUNT) || ((data == NULL) && (count != 0U)) ||
        (count > LA_STORAGE_BLOCK_CAPACITY))
    {
        return LA_SERVICE_ERROR_INVALID_PARAMETER;
    }
    valid_count = LA_Transfer_Get_Valid_Count(data, count, cutoff_tick);
    if (valid_count == 0U)
    {
        return (commit == NULL) ? LA_SERVICE_OK : commit(channel, 0U, context);
    }
    if (LA_Storage_Reserve(channel, valid_count, &destination) != LA_STORAGE_OK)
    {
        return LA_SERVICE_ERROR_STORAGE;
    }

    service_result = LA_Transfer_Copy(data, destination, valid_count);
    if (service_result != LA_SERVICE_OK)
    {
        return service_result;
    }

    if (commit != NULL)
    {
        return commit(channel, valid_count, context);
    }
    return (LA_Storage_Commit(channel, valid_count) == LA_STORAGE_OK) ? LA_SERVICE_OK :
                                                                        LA_SERVICE_ERROR_STORAGE;
}
