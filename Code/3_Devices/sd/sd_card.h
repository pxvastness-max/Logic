/**
 * @file   sd_card.h
 * @brief  板载SDMMC1 TF卡扇区访问接口
 */

#ifndef SD_CARD_H
#define SD_CARD_H

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================== */

#include <stdint.h>

/* ================================================== */

typedef enum
{
    DEVICE_SD_OK = 0,          /* 成功，缓冲已停止使用 */
    DEVICE_SD_ERROR_PARAMETER, /* 参数或扇区范围无效 */
    DEVICE_SD_ERROR_CONTEXT,   /* 调用上下文无效 */
    DEVICE_SD_ERROR_NOT_READY, /* SD卡未就绪 */
    DEVICE_SD_ERROR_INIT,      /* 初始化失败，可重试 */
    DEVICE_SD_ERROR_TRANSFER,  /* 传输失败，须重新初始化 */
    DEVICE_SD_ERROR_TIMEOUT    /* 传输超时，硬件已停止 */
} Device_SD_Result_t;

typedef struct
{
    uint32_t sector_count; /* 逻辑扇区总数 */
    uint32_t sector_size;  /* 逻辑扇区字节数 */
} Device_SD_Card_Info_t;

/* ================================================== */

/**
 * @brief  初始化SD卡
 * @note   只在调度器运行的任务中调用
 */
Device_SD_Result_t Device_SD_Init(void);

/**
 * @brief  释放SD卡资源
 * @note   与读写互斥，不能中途取消传输
 */
Device_SD_Result_t Device_SD_DeInit(void);
/* 软件就绪，不提供物理插拔检测；控制器状态异常时返回0 */
uint8_t Device_SD_Is_Ready(void);

/**
 * @brief  取得SD卡扇区信息
 * @param  info 非空输出结构
 */
Device_SD_Result_t Device_SD_Get_Card_Info(Device_SD_Card_Info_t *info);

/**
 * @brief  同步读取SD卡扇区
 * @param  buffer 至少 count * 512 字节
 * @param  count 1~65535，不超过卡容量
 * @note   任务调用；缓冲需4字节对齐且可被DMA访问，调用者维护缓存
 */
Device_SD_Result_t Device_SD_Read_Blocks(uint8_t *buffer, uint32_t sector, uint32_t count);

/**
 * @brief  同步写入SD卡扇区
 * @param  buffer 至少 count * 512 字节
 * @param  count 1~65535，不超过卡容量
 * @note   缓冲要求与读取接口相同，返回前不得修改
 */
Device_SD_Result_t Device_SD_Write_Blocks(const uint8_t *buffer, uint32_t sector, uint32_t count);

/**
 * @brief  等待SD卡结束内部编程
 * @note   失败后须重新初始化
 */
Device_SD_Result_t Device_SD_Sync(void);
/* 最近一次HAL SD错误位图，仅用于故障诊断 */
uint32_t Device_SD_Get_Last_HAL_Error(void);

/* ================================================== */

#ifdef __cplusplus
}
#endif

#endif /* SD_CARD_H */
