/**
 * @file   gui_wave_gesture.c
 * @brief  波形触摸手势适配
 */

/* ================================================== */

#include "gui/gesture/gui_wave_gesture.h"
#include "waveform/wave_service.h"

/* ================================================== */

void GUI_Wave_Gesture_Handle(LVGL_Input_Gesture_t gesture,
                             const LVGL_Input_Gesture_Data_t *data,
                             void *user_data)
{
    const int32_t wave_right = WAVE_AREA_X + WAVE_AREA_WIDTH;

    (void)user_data;
    if ((data == NULL) || (data->start.x < WAVE_AREA_X) || (data->start.x >= wave_right) ||
        (data->end.x < WAVE_AREA_X) || (data->end.x >= wave_right))
    {
        return;
    }

    if ((gesture == LVGL_INPUT_GESTURE_SWIPE_LEFT) || (gesture == LVGL_INPUT_GESTURE_SWIPE_RIGHT) ||
        (gesture == LVGL_INPUT_GESTURE_DRAG_MOVE))
    {
        Wave_Pan_View_Pixels(data->end.x - data->start.x);
    }
    else if ((gesture == LVGL_INPUT_GESTURE_PINCH_OUT) ||
             (gesture == LVGL_INPUT_GESTURE_PINCH_IN) || (gesture == LVGL_INPUT_GESTURE_PINCH_MOVE))
    {
        const bool zoom_in =
            (gesture == LVGL_INPUT_GESTURE_PINCH_OUT) ||
            ((gesture == LVGL_INPUT_GESTURE_PINCH_MOVE) && (data->distance_delta > 0));

        Wave_Zoom_View_At(data->end.x, zoom_in);
    }
}
