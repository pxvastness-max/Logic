/**
 * @file   touch_service.h
 * @brief  触摸采样和最新触点数据
 */

#ifndef TOUCH_SERVICE_H
#define TOUCH_SERVICE_H

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================== */

#include "FreeRTOS.h"
#include "task.h"
#include "touch/touch.h"
#include <stdbool.h>
#include <stdint.h>

/* ================================================== */

typedef struct
{
    Touch_Point_t points[TOUCH_MAX_POINTS]; /* 最新采样的屏幕坐标，仅前 count 项有效 */
    uint8_t count;                          /* 当前有效触点数，范围为 0~TOUCH_MAX_POINTS */
    uint32_t sequence;                      /* 发布采样的递增序号，32 位自然回绕 */
} Touch_Service_Sample_t;

/* ================================================== */

/**
 * @brief  创建只保存最新采样的队列，并启动触摸任务
 * @note   设备就绪后、调度器启动前调用；无触摸设备也可成功
 */
bool Touch_Service_Init(void);

/**
 * @brief  设置触摸变化时需要被唤醒的 GUI 任务
 * @note   任务句柄在采样期间保持有效，NULL 关闭通知
 */
void Touch_Service_Bind_GUI_Task(TaskHandle_t gui_task);

/**
 * @brief  立即复制最近一次触摸采样
 * @note   不移除队列元素
 */
bool Touch_Service_Get_Latest(Touch_Service_Sample_t *sample);

/**
 * @brief  查询触摸器件和采样任务是否均可工作
 */
bool Touch_Service_Is_Ready(void);

/* ================================================== */

#ifdef __cplusplus
}
#endif

#endif /* TOUCH_SERVICE_H */
