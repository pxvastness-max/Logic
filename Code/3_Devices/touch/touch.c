/**
 * @file   touch.c
 * @brief  FT5206/FT5426 和 GT9xxx 触摸设备识别、读取及报文解析
 */

/* ================================================== */

#include "touch/touch.h"
#include "touch/bsp_i2c_touch.h"
#include <stddef.h>
#include <string.h>

/* ========================= 配置 ========================= */

#define GT9XXX_WRITE_ADDRESS        0x28U   /* GT9xxx的8位I2C写地址 */
#define GT9XXX_READ_ADDRESS         0x29U   /* GT9xxx的8位I2C读地址 */
#define GT9XXX_CONTROL_REGISTER     0x8040U /* GT9xxx实时命令寄存器 */
#define GT9XXX_PRODUCT_ID_REGISTER  0x8140U /* GT9xxx产品ID首地址 */
#define GT9XXX_STATUS_REGISTER      0x814EU /* GT9xxx坐标状态和触点数寄存器 */
#define GT9XXX_POINT1_REGISTER      0x8150U /* GT9xxx第一个触点坐标首地址 */
#define FT5206_WRITE_ADDRESS        0x70U   /* FT5206的8位I2C写地址 */
#define FT5206_READ_ADDRESS         0x71U   /* FT5206的8位I2C读地址 */
#define FT5206_TOUCH_COUNT_REGISTER 0x02U   /* FT5206当前触点数量寄存器 */
#define FT5206_POINT1_REGISTER      0x03U   /* FT5206第一个触点数据首地址 */
#define FT5206_LIBRARY_REGISTER     0xA1U   /* FT5206固件库版本高字节寄存器 */
#define FT5206_MODE_REGISTER        0xA4U   /* FT5206中断上报模式寄存器 */
#define FT5206_THRESHOLD_REGISTER   0x80U   /* FT5206有效触摸检测阈值寄存器 */
#define FT5206_PERIOD_REGISTER      0x88U   /* FT5206工作态扫描周期寄存器 */
#define FT5206_POINT_SIZE           6U      /* FT5206单个触点数据长度 */
#define GT9XXX_POINT_SIZE           8U      /* GT9xxx单个触点数据长度 */

/* ================================================== */

static bool touch_ready;                                  /* 触摸设备是否初始化成功 */
static uint8_t touch_last_count;                          /* 最近一次有效触点数量 */
static Touch_Controller_t touch_controller;               /* 当前识别到的控制器类型 */
static char touch_controller_id[5];                       /* 控制器型号字符串 */
static Touch_Point_t touch_last_points[TOUCH_MAX_POINTS]; /* 最近一次全部有效触点 */

/* ================================================== */

/**
 * @brief  通过 BSP 读取 GT9xxx 的 16 位地址寄存器
 * @note   普通任务或调度器启动前调用；同步传输，数组只需在调用期间有效
 */
static bool GT9XXX_Read(uint16_t reg, uint8_t *data, uint8_t length)
{
    return BSP_I2C_Touch_Read16(GT9XXX_WRITE_ADDRESS, GT9XXX_READ_ADDRESS, reg, data, length);
}

/**
 * @brief  通过 BSP 写入 GT9xxx 的 16 位地址寄存器
 * @note   普通任务或调度器启动前调用；同步传输，数组只需在调用期间有效
 */
static bool GT9XXX_Write(uint16_t reg, const uint8_t *data, uint8_t length)
{
    return BSP_I2C_Touch_Write16(GT9XXX_WRITE_ADDRESS, reg, data, length);
}

/**
 * @brief  通过 BSP 读取 FT52xx 的 8 位地址寄存器
 * @note   普通任务或调度器启动前调用；同步传输，数组只需在调用期间有效
 */
static bool FT5206_Read(uint8_t reg, uint8_t *data, uint8_t length)
{
    return BSP_I2C_Touch_Read8(FT5206_WRITE_ADDRESS, FT5206_READ_ADDRESS, reg, data, length);
}

/**
 * @brief  通过 BSP 写入 FT52xx 的单字节寄存器
 * @note   普通任务或调度器启动前调用；同步传输，数组只需在调用期间有效
 */
static bool FT5206_Write(uint8_t reg, uint8_t value)
{
    return BSP_I2C_Touch_Write8(FT5206_WRITE_ADDRESS, reg, &value, 1U);
}

/**
 * @brief  复位 FT52xx 并配置上报模式、检测阈值和扫描周期
 */
static bool FT5206_Init(void)
{
    uint8_t version[2];
    BSP_I2C_Touch_Reset(20U, 50U);
    if (!FT5206_Write(0x00U, 0x00U) || !FT5206_Write(FT5206_MODE_REGISTER, 0x00U) ||
        !FT5206_Write(FT5206_THRESHOLD_REGISTER, 22U) ||
        !FT5206_Write(FT5206_PERIOD_REGISTER, 12U) ||
        !FT5206_Read(FT5206_LIBRARY_REGISTER, version, sizeof(version)))
    {
        return false;
    }

    if (!((version[0] == 0x30U && version[1] == 0x03U) || version[1] == 0x01U ||
          version[1] == 0x02U || (version[0] == 0x00U && version[1] == 0x00U)))
    {
        return false;
    }
    (void)memcpy(touch_controller_id, "FT52", 5U);
    return true;
}

/**
 * @brief  复位 GT9xxx 并确认产品编号和启动命令
 * @note   型号缓存已经清零且保留字符串结束符；识别完成后退出配置命令状态
 */
static Touch_Status_t GT9XXX_Init(void)
{
    uint8_t control;
    BSP_I2C_Touch_Reset(10U, 100U);
    if (!GT9XXX_Read(GT9XXX_PRODUCT_ID_REGISTER, (uint8_t *)touch_controller_id, 4U))
    {
        return TOUCH_STATUS_NOT_FOUND;
    }
    if (strcmp(touch_controller_id, "911") != 0 && strcmp(touch_controller_id, "9147") != 0 &&
        strcmp(touch_controller_id, "1158") != 0 && strcmp(touch_controller_id, "9271") != 0)
    {
        return TOUCH_STATUS_UNSUPPORTED_CONTROLLER;
    }

    control = 0x02U;
    if (!GT9XXX_Write(GT9XXX_CONTROL_REGISTER, &control, 1U))
    {
        return TOUCH_STATUS_NOT_FOUND;
    }
    BSP_I2C_Touch_Delay_Ms(10U);
    control = 0x00U;
    if (!GT9XXX_Write(GT9XXX_CONTROL_REGISTER, &control, 1U))
    {
        return TOUCH_STATUS_NOT_FOUND;
    }
    return TOUCH_STATUS_OK;
}

/**
 * @brief  识别并初始化触摸控制器
 */
Touch_Status_t Touch_Init(void)
{
    Touch_Status_t status;

    touch_ready = false;
    touch_controller = TOUCH_CONTROLLER_NONE;
    touch_last_count = 0U;
    memset(touch_controller_id, 0, sizeof(touch_controller_id));
    memset(touch_last_points, 0, sizeof(touch_last_points));
    BSP_I2C_Touch_Init();

    if (FT5206_Init())
    {
        touch_controller = TOUCH_CONTROLLER_FT5206;
        touch_ready = true;
        return TOUCH_STATUS_OK;
    }
    memset(touch_controller_id, 0, sizeof(touch_controller_id));
    status = GT9XXX_Init();
    if (status == TOUCH_STATUS_OK)
    {
        touch_controller = TOUCH_CONTROLLER_GT9XXX;
        touch_ready = true;
    }
    return status;
}

/**
 * @brief  查询控制器识别和启动是否成功
 */
bool Touch_Is_Ready(void)
{
    return touch_ready;
}

/**
 * @brief  获取当前识别到的触摸控制器型号字符串
 */
const char *Touch_Get_Controller_ID(void)
{
    return touch_controller_id;
}

/**
 * @brief  获取当前控制器类型
 */
Touch_Controller_t Touch_Get_Controller(void)
{
    return touch_controller;
}

/**
 * @brief  将最近有效触点复制到容量受限的调用者数组
 * @note   仅触摸读取路径调用；不访问总线，不修改缓存所有权
 */
static uint8_t Touch_Copy_Cached_Points(Touch_Point_t *points, uint8_t max_points)
{
    if (points != NULL && max_points > 0U && touch_last_count > 0U)
    {
        const uint8_t copy_count = touch_last_count < max_points ? touch_last_count : max_points;
        memcpy(points, touch_last_points, (size_t)copy_count * sizeof(points[0]));
    }
    return touch_last_count;
}

/**
 * @brief  读取原始触点
 * @note   无新报文或读取失败时保留缓存，零触点报文释放
 */
uint8_t Touch_Read_Points(Touch_Point_t *points, uint8_t max_points)
{
    uint8_t status;
    uint8_t count;
    uint8_t index;
    uint8_t valid_count;
    uint8_t point_size;
    uint8_t data[TOUCH_MAX_POINTS * GT9XXX_POINT_SIZE];
    Touch_Point_t raw;

    if (!touch_ready)
    {
        return 0U;
    }
    if (!BSP_I2C_Touch_Interrupt_Active())
    {
        return Touch_Copy_Cached_Points(points, max_points);
    }

    if (touch_controller == TOUCH_CONTROLLER_FT5206)
    {
        if (!FT5206_Read(FT5206_TOUCH_COUNT_REGISTER, &status, 1U) || (status & 0x0FU) > 5U)
        {
            return Touch_Copy_Cached_Points(points, max_points);
        }
        count = status & 0x0FU;
        point_size = FT5206_POINT_SIZE;
        if (count == 0U)
        {
            touch_last_count = 0U;
            return 0U;
        }
        if (!FT5206_Read(FT5206_POINT1_REGISTER, data, (uint8_t)(count * point_size)))
        {
            return Touch_Copy_Cached_Points(points, max_points);
        }
    }
    else if (touch_controller == TOUCH_CONTROLLER_GT9XXX)
    {
        if (!GT9XXX_Read(GT9XXX_STATUS_REGISTER, &status, 1U) || (status & 0x80U) == 0U)
        {
            return Touch_Copy_Cached_Points(points, max_points);
        }
        count = status & 0x0FU;
        if (count == 0U || count > 10U)
        {
            status = 0U;
            (void)GT9XXX_Write(GT9XXX_STATUS_REGISTER, &status, 1U);
            touch_last_count = 0U;
            return 0U;
        }
        if (count > TOUCH_MAX_POINTS)
        {
            count = TOUCH_MAX_POINTS;
        }
        point_size = GT9XXX_POINT_SIZE;
        if (!GT9XXX_Read(GT9XXX_POINT1_REGISTER, data, (uint8_t)(count * point_size)))
        {
            return Touch_Copy_Cached_Points(points, max_points);
        }
        status = 0U;
        (void)GT9XXX_Write(GT9XXX_STATUS_REGISTER, &status, 1U);
    }
    else
    {
        return 0U;
    }

    valid_count = 0U;
    for (index = 0U; index < count; index++)
    {
        const uint8_t *entry = &data[index * point_size];

        if (touch_controller == TOUCH_CONTROLLER_FT5206)
        {
            if ((entry[0] & 0xC0U) == 0x40U)
            {
                continue;
            }
            raw.x = (uint16_t)(((uint16_t)(entry[0] & 0x0FU) << 8U) | entry[1]);
            raw.y = (uint16_t)(((uint16_t)(entry[2] & 0x0FU) << 8U) | entry[3]);
        }
        else
        {
            raw.x = (uint16_t)(((uint16_t)entry[1] << 8U) | entry[0]);
            raw.y = (uint16_t)(((uint16_t)entry[3] << 8U) | entry[2]);
        }

        touch_last_points[valid_count] = raw;
        if (points != NULL && valid_count < max_points)
        {
            points[valid_count] = touch_last_points[valid_count];
        }
        valid_count++;
    }

    count = valid_count;
    if (count == 0U)
    {
        touch_last_count = 0U;
        return 0U;
    }
    touch_last_count = count;
    return count;
}

/**
 * @brief  读取首个原始触点
 */
bool Touch_Read_Point(Touch_Point_t *point)
{
    return point != NULL && Touch_Read_Points(point, 1U) > 0U;
}
