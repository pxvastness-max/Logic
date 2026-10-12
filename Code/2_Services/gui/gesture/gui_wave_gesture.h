/**
 * @file   gui_wave_gesture.h
 * @brief  波形触摸手势适配
 */

#ifndef GUI_WAVE_GESTURE_H
#define GUI_WAVE_GESTURE_H

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================== */

#include "lvgl/lvgl_input_port.h"

/* ================================================== */

/**
 * @brief  把波形区域内的单指水平移动和双指缩放转换为视图请求
 * @note   仅处理 x=100~599 波形区内的手势
 */
void GUI_Wave_Gesture_Handle(LVGL_Input_Gesture_t gesture,
                             const LVGL_Input_Gesture_Data_t *data,
                             void *user_data);

/* ================================================== */

#ifdef __cplusplus
}
#endif

#endif /* GUI_WAVE_GESTURE_H */
