/**
 * @file   touch.h
 * @brief  FT5206/FT5426 和 GT9xxx 的芯片识别、报文解析与原始坐标接口
 */

#ifndef TOUCH_H
#define TOUCH_H

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================== */

#include <stdbool.h>
#include <stdint.h>

/* ========================= 配置 ========================= */

/* 统一支持最多 5 个触点；超过此数量的 GT9xxx 触点不交给上层 */
#define TOUCH_MAX_POINTS 5U

/* ================================================== */

typedef enum
{
    TOUCH_STATUS_OK = 0,
    TOUCH_STATUS_INVALID_ARGUMENT,
    TOUCH_STATUS_NOT_FOUND,
    TOUCH_STATUS_UNSUPPORTED_CONTROLLER
} Touch_Status_t;

typedef enum
{
    TOUCH_CONTROLLER_NONE = 0,
    TOUCH_CONTROLLER_FT5206,
    TOUCH_CONTROLLER_GT9XXX
} Touch_Controller_t;

typedef struct
{
    uint16_t x;
    uint16_t y;
} Touch_Point_t;

/* ================================================== */

/**
 * @brief  识别并初始化触摸控制器
 */
Touch_Status_t Touch_Init(void);

/**
 * @brief  查询控制器识别和启动是否成功
 */
bool Touch_Is_Ready(void);

/**
 * @brief  获取当前识别到的触摸控制器型号字符串
 */
const char *Touch_Get_Controller_ID(void);

/**
 * @brief  获取当前控制器类型
 */
Touch_Controller_t Touch_Get_Controller(void);

/**
 * @brief  读取首个原始触点
 * @param  point 非空输出结构
 * @retval true-当前有触点
 */
bool Touch_Read_Point(Touch_Point_t *point);

/**
 * @brief  读取原始触点
 * @param  points 可为NULL以仅查询数量
 * @param  max_points 输出容量
 * @retval 当前触点总数，最多输出 max_points 项
 * @note   无新报文或读取失败时保留缓存，零触点报文释放
 */
uint8_t Touch_Read_Points(Touch_Point_t *points, uint8_t max_points);

/* ================================================== */

#ifdef __cplusplus
}
#endif

#endif /* TOUCH_H */
