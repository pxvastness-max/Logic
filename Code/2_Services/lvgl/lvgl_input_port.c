/**
 * @file   lvgl_input_port.c
 * @brief  LVGL 触摸输入与手势适配
 */

/* ================================================== */

#include "lvgl/lvgl_input_port.h"
#include "touch/touch.h"
#include "touch/touch_service.h"
#include "log/log_service.h"

/* ========================= 配置 ========================= */

#define GESTURE_SWIPE_THRESHOLD      40
#define GESTURE_PINCH_THRESHOLD      40
#define GESTURE_DRAG_START_THRESHOLD 8
#define GESTURE_PINCH_STEP_THRESHOLD 24

/* ================================================== */

static bool lvgl_input_ready;            /* 触摸控制器和 LVGL 输入对象均初始化成功 */
static lv_point_t lvgl_input_last_point; /* 最近一次有效坐标，释放时继续报告该坐标 */
static lv_indev_t *lvgl_input_indev;     /* 注册到 LVGL 的 pointer 输入设备对象 */
static LVGL_Input_Gesture_Callback_t lvgl_input_gesture_cb;
static void *lvgl_input_gesture_user_data;
static bool lvgl_input_gesture_active;
static bool lvgl_input_gesture_had_multi_touch;
static bool lvgl_input_gesture_has_pinch;
static bool lvgl_input_gesture_has_drag;
static bool lvgl_input_gesture_has_pinch_move;
static uint32_t lvgl_input_last_sample_sequence = UINT32_MAX;
static Touch_Point_t lvgl_input_gesture_start;
static Touch_Point_t lvgl_input_gesture_last;
static Touch_Point_t lvgl_input_pinch_start_center;
static Touch_Point_t lvgl_input_pinch_last_center;
static int32_t lvgl_input_pinch_start_distance;
static int32_t lvgl_input_pinch_last_distance;
static int32_t lvgl_input_pinch_report_distance;

/* ================================================== */

static void LVGL_Input_Port_Read_Callback(lv_indev_t *indev, lv_indev_data_t *data);
static void LVGL_Input_Gesture_Process(const Touch_Point_t *points, uint8_t count);
static int32_t LVGL_Input_Gesture_Distance(const Touch_Point_t *first, const Touch_Point_t *second);

/* ================================================== */

bool LVGL_Input_Port_Init(void)
{
    lv_display_t *display = lv_display_get_default();

    if (lvgl_input_indev != NULL)
    {
        return true;
    }
    if (display == NULL)
    {
        lvgl_input_ready = false;
        (void)Log_Printf("touch: default display not created\r\n");
        return false;
    }

    lvgl_input_ready = Touch_Is_Ready() && Touch_Service_Is_Ready();
    lvgl_input_last_point.x = 0;
    lvgl_input_last_point.y = 0;
    lvgl_input_indev = NULL;

    if (!lvgl_input_ready)
    {
        (void)Log_Printf("touch: controller not detected\r\n");
        return false;
    }

    lvgl_input_indev = lv_indev_create();
    if (lvgl_input_indev == NULL)
    {
        lvgl_input_ready = false;
        (void)Log_Printf("touch: LVGL indev allocation failed\r\n");
        return false;
    }

    lv_indev_set_type(lvgl_input_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(lvgl_input_indev, LVGL_Input_Port_Read_Callback);
    return true;
}

bool LVGL_Input_Port_Is_Ready(void)
{
    return lvgl_input_ready;
}

void LVGL_Input_Port_Set_Gesture_Callback(LVGL_Input_Gesture_Callback_t callback, void *user_data)
{
    lvgl_input_gesture_cb = callback;
    lvgl_input_gesture_user_data = user_data;
}

/* ================================================== */

/**
 * @brief  把最近一次触摸采样交给 LVGL
 */
static void LVGL_Input_Port_Read_Callback(lv_indev_t *indev, lv_indev_data_t *data)
{
    Touch_Service_Sample_t sample = {0};

    (void)indev;
    data->continue_reading = false;
    data->point = lvgl_input_last_point;

    if (!lvgl_input_ready || !Touch_Service_Get_Latest(&sample))
    {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    if (sample.sequence != lvgl_input_last_sample_sequence)
    {
        LVGL_Input_Gesture_Process(sample.points, sample.count);
        lvgl_input_last_sample_sequence = sample.sequence;
    }
    if (sample.count == 0U)
    {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    lvgl_input_last_point.x = (int32_t)sample.points[0].x;
    lvgl_input_last_point.y = (int32_t)sample.points[0].y;
    data->point = lvgl_input_last_point;
    data->state = LV_INDEV_STATE_PRESSED;
}

/**
 * @brief  跟踪单指拖动和双指缩放，并按阈值上报增量或释放结果
 * @note   双指会话不再识别单指手势，释放时不重复上报
 */
static void LVGL_Input_Gesture_Process(const Touch_Point_t *points, uint8_t count)
{
    LVGL_Input_Gesture_Data_t data;
    int32_t dx;
    int32_t dy;

    if (count > 0U && !lvgl_input_gesture_active)
    {
        lvgl_input_gesture_active = true;
        lvgl_input_gesture_had_multi_touch = false;
        lvgl_input_gesture_has_pinch = false;
        lvgl_input_gesture_has_drag = false;
        lvgl_input_gesture_has_pinch_move = false;
        lvgl_input_gesture_start = points[0];
        lvgl_input_gesture_last = points[0];
    }

    if (count == 1U && lvgl_input_gesture_active && !lvgl_input_gesture_had_multi_touch)
    {
        const int32_t total_dx = (int32_t)points[0].x - (int32_t)lvgl_input_gesture_start.x;
        const int32_t total_dy = (int32_t)points[0].y - (int32_t)lvgl_input_gesture_start.y;
        const int32_t abs_dx = total_dx >= 0 ? total_dx : -total_dx;
        const int32_t abs_dy = total_dy >= 0 ? total_dy : -total_dy;

        if ((lvgl_input_gesture_has_drag || abs_dx >= GESTURE_DRAG_START_THRESHOLD) &&
            abs_dx > abs_dy)
        {
            data.start.x = (int32_t)(lvgl_input_gesture_has_drag ? lvgl_input_gesture_last.x :
                                                                   lvgl_input_gesture_start.x);
            data.start.y = (int32_t)(lvgl_input_gesture_has_drag ? lvgl_input_gesture_last.y :
                                                                   lvgl_input_gesture_start.y);
            data.end.x = (int32_t)points[0].x;
            data.end.y = (int32_t)points[0].y;
            data.distance_delta = 0;
            if (data.start.x != data.end.x && lvgl_input_gesture_cb != NULL)
            {
                lvgl_input_gesture_cb(LVGL_INPUT_GESTURE_DRAG_MOVE,
                                      &data,
                                      lvgl_input_gesture_user_data);
                lvgl_input_gesture_has_drag = true;
            }
        }
        lvgl_input_gesture_last = points[0];
    }
    else if (count >= 2U && lvgl_input_gesture_active)
    {
        const int32_t distance = LVGL_Input_Gesture_Distance(&points[0], &points[1]);
        Touch_Point_t center;

        center.x = (uint16_t)(((uint32_t)points[0].x + points[1].x) / 2U);
        center.y = (uint16_t)(((uint32_t)points[0].y + points[1].y) / 2U);
        lvgl_input_gesture_had_multi_touch = true;
        if (!lvgl_input_gesture_has_pinch)
        {
            lvgl_input_gesture_has_pinch = true;
            lvgl_input_pinch_start_center = center;
            lvgl_input_pinch_last_center = center;
            lvgl_input_pinch_start_distance = distance;
            lvgl_input_pinch_last_distance = distance;
            lvgl_input_pinch_report_distance = distance;
        }
        else
        {
            const int32_t distance_delta = distance - lvgl_input_pinch_report_distance;
            const int32_t abs_delta = distance_delta >= 0 ? distance_delta : -distance_delta;

            if (abs_delta >= GESTURE_PINCH_STEP_THRESHOLD)
            {
                data.start.x = (int32_t)lvgl_input_pinch_last_center.x;
                data.start.y = (int32_t)lvgl_input_pinch_last_center.y;
                data.end.x = (int32_t)center.x;
                data.end.y = (int32_t)center.y;
                data.distance_delta = distance_delta;
                if (lvgl_input_gesture_cb != NULL)
                {
                    lvgl_input_gesture_cb(LVGL_INPUT_GESTURE_PINCH_MOVE,
                                          &data,
                                          lvgl_input_gesture_user_data);
                }
                lvgl_input_gesture_has_pinch_move = true;
                lvgl_input_pinch_report_distance = distance;
            }
        }
        lvgl_input_pinch_last_center = center;
        lvgl_input_pinch_last_distance = distance;
        lvgl_input_gesture_last = points[0];
    }

    if (count != 0U || !lvgl_input_gesture_active)
    {
        return;
    }

    data.start.x = (int32_t)lvgl_input_gesture_start.x;
    data.start.y = (int32_t)lvgl_input_gesture_start.y;
    data.end.x = (int32_t)lvgl_input_gesture_last.x;
    data.end.y = (int32_t)lvgl_input_gesture_last.y;
    data.distance_delta = 0;
    dx = data.end.x - data.start.x;
    dy = data.end.y - data.start.y;

    if (lvgl_input_gesture_has_pinch && !lvgl_input_gesture_has_pinch_move)
    {
        const int32_t distance_delta =
            lvgl_input_pinch_last_distance - lvgl_input_pinch_start_distance;

        if (distance_delta >= GESTURE_PINCH_THRESHOLD || distance_delta <= -GESTURE_PINCH_THRESHOLD)
        {
            data.start.x = (int32_t)lvgl_input_pinch_start_center.x;
            data.start.y = (int32_t)lvgl_input_pinch_start_center.y;
            data.end.x = (int32_t)lvgl_input_pinch_last_center.x;
            data.end.y = (int32_t)lvgl_input_pinch_last_center.y;
            data.distance_delta = distance_delta;
            if (lvgl_input_gesture_cb != NULL)
            {
                lvgl_input_gesture_cb(distance_delta > 0 ? LVGL_INPUT_GESTURE_PINCH_OUT :
                                                           LVGL_INPUT_GESTURE_PINCH_IN,
                                      &data,
                                      lvgl_input_gesture_user_data);
            }
        }
    }
    else if (!lvgl_input_gesture_had_multi_touch && !lvgl_input_gesture_has_drag &&
             ((dx >= GESTURE_SWIPE_THRESHOLD || dx <= -GESTURE_SWIPE_THRESHOLD) &&
              (dx >= 0 ? dx : -dx) > (dy >= 0 ? dy : -dy)))
    {
        if (lvgl_input_gesture_cb != NULL)
        {
            lvgl_input_gesture_cb(dx > 0 ? LVGL_INPUT_GESTURE_SWIPE_RIGHT :
                                           LVGL_INPUT_GESTURE_SWIPE_LEFT,
                                  &data,
                                  lvgl_input_gesture_user_data);
        }
    }

    lvgl_input_gesture_active = false;
}

/**
 * @brief  计算两个触点在屏幕上的直线距离
 * @note   先算水平和垂直距离的平方和，再取整数平方根
 */
static int32_t LVGL_Input_Gesture_Distance(const Touch_Point_t *first, const Touch_Point_t *second)
{
    const int32_t dx = (int32_t)first->x - second->x;
    const int32_t dy = (int32_t)first->y - second->y;
    uint32_t value = (uint32_t)(dx * dx + dy * dy);
    uint32_t bit = 1UL << 30;
    uint32_t root = 0U;

    while (bit > value)
    {
        bit >>= 2U;
    }
    while (bit != 0U)
    {
        if (value >= root + bit)
        {
            value -= root + bit;
            root = (root >> 1U) + bit;
        }
        else
        {
            root >>= 1U;
        }
        bit >>= 2U;
    }
    return (int32_t)root;
}
