/**
 * @file   touch_service.c
 * @brief  触摸采样和最新触点数据
 */

/* ================================================== */

#include "touch/touch_service.h"
#include "display/lcd.h"
#include "queue.h"
#include <string.h>

/* ========================= 配置 ========================= */

#define TOUCH_SERVICE_TASK_STACK 512U
#define TOUCH_SERVICE_TASK_PRIO  3U
#define TOUCH_SERVICE_PERIOD     pdMS_TO_TICKS(5U)

/* ================================================== */

static QueueHandle_t touch_service_sample_queue;
static TaskHandle_t touch_service_gui_task;
static bool touch_service_ready;
static uint16_t touch_service_display_width;
static uint16_t touch_service_display_height;

/* ================================================== */

static void Touch_Service_Task(void *parameter);
static bool Touch_Service_Sample_Changed(const Touch_Service_Sample_t *first,
                                         const Touch_Service_Sample_t *second);
static void Touch_Service_Map_To_Display(Touch_Point_t *point);

/* ================================================== */

bool Touch_Service_Init(void)
{
    Touch_Service_Sample_t initial_sample = {0};

    if (touch_service_sample_queue != NULL)
    {
        return true;
    }
    touch_service_display_width = LCD_Get_Width();
    touch_service_display_height = LCD_Get_Height();
    if ((touch_service_display_width == 0U) || (touch_service_display_height == 0U))
    {
        return false;
    }
    touch_service_sample_queue = xQueueCreate(1U, sizeof(Touch_Service_Sample_t));
    if (touch_service_sample_queue == NULL)
    {
        return false;
    }
    (void)xQueueOverwrite(touch_service_sample_queue, &initial_sample);

    if (!Touch_Is_Ready())
    {
        touch_service_ready = false;
        return true;
    }

    touch_service_ready = xTaskCreate(Touch_Service_Task,
                                      "touch",
                                      TOUCH_SERVICE_TASK_STACK,
                                      NULL,
                                      TOUCH_SERVICE_TASK_PRIO,
                                      NULL) == pdPASS;
    if (!touch_service_ready)
    {
        vQueueDelete(touch_service_sample_queue);
        touch_service_sample_queue = NULL;
    }
    return touch_service_ready;
}

void Touch_Service_Bind_GUI_Task(TaskHandle_t gui_task)
{
    touch_service_gui_task = gui_task;
}

bool Touch_Service_Get_Latest(Touch_Service_Sample_t *sample)
{
    if ((sample == NULL) || (touch_service_sample_queue == NULL))
    {
        return false;
    }
    return xQueuePeek(touch_service_sample_queue, sample, 0U) == pdPASS;
}

bool Touch_Service_Is_Ready(void)
{
    return touch_service_ready;
}

/* ================================================== */

/**
 * @brief  周期读取触摸设备，仅发布有效触点数量或坐标变化
 * @note   每 5 ms 采样，独占设备读取路径
 */
static void Touch_Service_Task(void *parameter)
{
    Touch_Service_Sample_t previous = {0};
    Touch_Service_Sample_t current = {0};
    TickType_t last_wake_time = xTaskGetTickCount();

    (void)parameter;
    for (;;)
    {
        uint8_t index;

        current.count = Touch_Read_Points(current.points, TOUCH_MAX_POINTS);
        for (index = 0U; index < current.count; index++)
        {
            Touch_Service_Map_To_Display(&current.points[index]);
        }
        if (Touch_Service_Sample_Changed(&current, &previous))
        {
            current.sequence++;
            (void)xQueueOverwrite(touch_service_sample_queue, &current);
            previous = current;
            if (touch_service_gui_task != NULL)
            {
                xTaskNotifyGive(touch_service_gui_task);
            }
        }
        xTaskDelayUntil(&last_wake_time, TOUCH_SERVICE_PERIOD);
    }
}

/**
 * @brief  将芯片原始坐标转换并裁剪为显示区域坐标
 * @note   FT52xx 交换横纵轴，GT9xxx 保持方向
 */
static void Touch_Service_Map_To_Display(Touch_Point_t *point)
{
    uint16_t x = point->x;
    uint16_t y = point->y;

    if (Touch_Get_Controller() != TOUCH_CONTROLLER_GT9XXX)
    {
        x = point->y;
        y = point->x;
    }
    point->x = (x < touch_service_display_width) ? x : (uint16_t)(touch_service_display_width - 1U);
    point->y =
        (y < touch_service_display_height) ? y : (uint16_t)(touch_service_display_height - 1U);
}

/**
 * @brief  比较两次采样的触点数量和坐标
 */
static bool Touch_Service_Sample_Changed(const Touch_Service_Sample_t *first,
                                         const Touch_Service_Sample_t *second)
{
    if (first->count != second->count)
    {
        return true;
    }
    return memcmp(first->points, second->points, (size_t)first->count * sizeof(first->points[0])) !=
           0;
}
