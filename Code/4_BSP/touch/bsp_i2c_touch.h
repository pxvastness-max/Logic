/**
 * @file   bsp_i2c_touch.h
 * @brief  板级触摸软件I2C、复位和中断接口
 */

#ifndef BSP_I2C_TOUCH_H
#define BSP_I2C_TOUCH_H

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================== */

#include <stdbool.h>
#include <stdint.h>

/* ================================================== */

/**
 * @brief  配置 PH6/PI3 软件 IIC、PI8 复位和 PH7 中断输入
 * @note   SCL/SDA 为带上拉的开漏输出，中断脚只轮询读取，不配置 EXTI
 */
void BSP_I2C_Touch_Init(void);

/**
 * @brief  使用 HAL 毫秒时基等待触摸设备启动
 * @note   忙等待且不主动让出任务
 */
void BSP_I2C_Touch_Delay_Ms(uint32_t delay_ms);

/**
 * @brief  拉低复位脚后释放，并等待触摸控制器完成启动
 * @param  low_ms 复位低电平保持时间，单位毫秒
 */
void BSP_I2C_Touch_Reset(uint32_t low_ms, uint32_t high_ms);

/**
 * @brief  读取低电平有效的中断脚；返回 true 仅表示电平有效，不确认触摸数据已经就绪
 */
bool BSP_I2C_Touch_Interrupt_Active(void);

/**
 * @brief  从使用 8 位寄存器地址的触摸芯片连续读取数据
 * @param  write_address 含最低位写标志的完整 8 位总线地址
 * @param  data 调用者提供的可写数组，容量至少为 length 字节
 */
bool BSP_I2C_Touch_Read8(uint8_t write_address,
                         uint8_t read_address,
                         uint8_t reg,
                         uint8_t *data,
                         uint8_t length);

/**
 * @brief  向使用 8 位寄存器地址的触摸芯片连续写入数据
 * @param  data 长度至少为 length 的源数组，调用期间保持有效
 * @param  length 写入字节数，0 表示仅发送地址及寄存器定位阶段
 */
bool BSP_I2C_Touch_Write8(uint8_t write_address, uint8_t reg, const uint8_t *data, uint8_t length);

/**
 * @brief  从使用 16 位寄存器地址的触摸芯片连续读取数据
 */
bool BSP_I2C_Touch_Read16(uint8_t write_address,
                          uint8_t read_address,
                          uint16_t reg,
                          uint8_t *data,
                          uint8_t length);

/**
 * @brief  向使用 16 位寄存器地址的触摸芯片连续写入数据
 */
bool BSP_I2C_Touch_Write16(uint8_t write_address,
                           uint16_t reg,
                           const uint8_t *data,
                           uint8_t length);

/* ================================================== */

#ifdef __cplusplus
}
#endif

#endif /* BSP_I2C_TOUCH_H */
