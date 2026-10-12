/**
 * @file   lvgl_input_port.h
 * @brief  LVGL 触摸输入与手势适配
 */

#ifndef LVGL_INPUT_PORT_H
#define LVGL_INPUT_PORT_H

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================== */

#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>

/* ================================================== */

/* DRAG_MOVE/PINCH_MOVE 在按下期间增量上报 */
typedef enum
{
    LVGL_INPUT_GESTURE_SWIPE_LEFT,
    LVGL_INPUT_GESTURE_SWIPE_RIGHT,
    LVGL_INPUT_GESTURE_PINCH_IN,
    LVGL_INPUT_GESTURE_PINCH_OUT,
    LVGL_INPUT_GESTURE_DRAG_MOVE,
    LVGL_INPUT_GESTURE_PINCH_MOVE
} LVGL_Input_Gesture_t;

typedef struct
{
    lv_point_t start;       /* 单指滑动起点或双指中心起点 */
    lv_point_t end;         /* 单指滑动终点或双指中心终点 */
    int32_t distance_delta; /* 双指间距变化（放大为正，缩小为负） */
} LVGL_Input_Gesture_Data_t;

/**
 * @brief  GUI 输入读取过程中同步执行的手势处理回调
 * @note   GUI 任务同步调用，不能阻塞或保留临时数据指针
 */
typedef void (*LVGL_Input_Gesture_Callback_t)(LVGL_Input_Gesture_t gesture,
                                              const LVGL_Input_Gesture_Data_t *data,
                                              void *user_data);

/* ================================================== */

/**
 * @brief  将已就绪触摸服务注册为 LVGL 指针输入设备
 * @note   GUI 任务在显示端口就绪后调用
 */
bool LVGL_Input_Port_Init(void);

/**
 * @brief  检查触摸设备、采样服务和 LVGL 输入对象是否已就绪
 */
bool LVGL_Input_Port_Is_Ready(void);

/**
 * @brief  设置触摸手势处理回调和调用者上下文
 * @param  callback : NULL 关闭上报
 * @note   GUI 任务调用，上下文在注册期间保持有效
 */
void LVGL_Input_Port_Set_Gesture_Callback(LVGL_Input_Gesture_Callback_t callback, void *user_data);

/* ================================================== */

#ifdef __cplusplus
}
#endif

#endif /* LVGL_INPUT_PORT_H */
