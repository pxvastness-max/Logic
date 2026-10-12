/* stm32h7xx_hal_conf.h：STM32 HAL 编译选项与板级振荡器参数 */

#ifndef STM32H7XX_HAL_CONF_H
#define STM32H7XX_HAL_CONF_H

/* HAL 基础功能和 Cortex-M7 内核支持 */
#define HAL_MODULE_ENABLED
#define HAL_CORTEX_MODULE_ENABLED
/* 采集和串口使用 DMA；显示刷新可使用 DMA2D */
#define HAL_DMA_MODULE_ENABLED
#define HAL_DMA2D_MODULE_ENABLED
/* Flash、GPIO 和 LCD 控制器 */
#define HAL_FLASH_MODULE_ENABLED
#define HAL_GPIO_MODULE_ENABLED
#define HAL_LTDC_MODULE_ENABLED
/* SDRAM 驱动会用到 MDMA 类型，两个模块都要启用 */
#define HAL_MDMA_MODULE_ENABLED
/* 电源和系统时钟 */
#define HAL_PWR_MODULE_ENABLED
#define HAL_RCC_MODULE_ENABLED
/* SD 卡和 SDRAM */
#define HAL_SD_MODULE_ENABLED
#define HAL_SDRAM_MODULE_ENABLED
/* 定时器用于系统计时和采集；串口用于日志 */
#define HAL_TIM_MODULE_ENABLED
#define HAL_UART_MODULE_ENABLED

/* 板上的 25 MHz 晶振供 PLL1 使用 */

#ifndef HSE_VALUE
    #define HSE_VALUE 25000000UL
#endif
/* 等待外部晶振启动的时间上限，单位为毫秒 */

#ifndef HSE_STARTUP_TIMEOUT
    #define HSE_STARTUP_TIMEOUT 100UL
#endif
/* 切换到 PLL 前，芯片使用标称 64 MHz 的内部时钟 */

#ifndef HSI_VALUE
    #define HSI_VALUE 64000000UL
#endif
/* HAL 计算时钟时使用的 CSI 标称频率 */

#ifndef CSI_VALUE
    #define CSI_VALUE 4000000UL
#endif
/* 外部低速晶振的标称频率；启动代码没有启用 RTC */

#ifndef LSE_VALUE
    #define LSE_VALUE 32768UL
#endif
/* 启用外部低速晶振时的等待上限，单位为毫秒 */

#ifndef LSE_STARTUP_TIMEOUT
    #define LSE_STARTUP_TIMEOUT 5000UL
#endif
/* 内部低速时钟的标称频率；实际频率有误差 */

#ifndef LSI_VALUE
    #define LSI_VALUE 32000UL
#endif
/* HAL 的外部音频时钟默认值；本项目未使用 */

#ifndef EXTERNAL_CLOCK_VALUE
    #define EXTERNAL_CLOCK_VALUE 12288000UL
#endif

/* HAL 使用的供电电压标称值，单位为 mV */
#define VDD_VALUE 3300UL
/* TIM6 中断使用最低优先级 15 */
#define TICK_INT_PRIORITY 15UL
/* HAL 的此项设置保持为 0；FreeRTOS 由项目代码接入 */
#define USE_RTOS 0U
/* SD 卡没有外接 1.8 V 电平转换器 */
#define USE_SD_TRANSCEIVER 0U
/* 项目未使用 HAL SPI 驱动 */
#define USE_SPI_CRC 0U

/* 各外设回调在 BSP 中直接定义，无需运行时注册 */
#define USE_HAL_DMA2D_REGISTER_CALLBACKS 0U
#define USE_HAL_LTDC_REGISTER_CALLBACKS  0U
#define USE_HAL_SD_REGISTER_CALLBACKS    0U
#define USE_HAL_SDRAM_REGISTER_CALLBACKS 0U
#define USE_HAL_TIM_REGISTER_CALLBACKS   0U
#define USE_HAL_UART_REGISTER_CALLBACKS  0U

/* SDRAM 头文件使用 MDMA 类型，所以先包含 MDMA 头文件 */

#include "stm32h7xx_hal_rcc.h"
#include "stm32h7xx_hal_gpio.h"
#include "stm32h7xx_hal_dma.h"
#include "stm32h7xx_hal_mdma.h"
#include "stm32h7xx_hal_dma2d.h"
#include "stm32h7xx_hal_cortex.h"
#include "stm32h7xx_hal_flash.h"
#include "stm32h7xx_hal_ltdc.h"
#include "stm32h7xx_hal_pwr.h"
#include "stm32h7xx_hal_sd.h"
#include "stm32h7xx_hal_sdram.h"
#include "stm32h7xx_hal_tim.h"
#include "stm32h7xx_hal_uart.h"

/* 启用完整断言后，错误位置交给平台的 assert_failed 处理 */
#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line);
    #define assert_param(expression)  ((expression) ? (void)0U : assert_failed((uint8_t *)__FILE__, __LINE__))
#else
    #define assert_param(expression) ((void)0U)
#endif

#endif /* STM32H7XX_HAL_CONF_H */
