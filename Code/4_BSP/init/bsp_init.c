/**
 * @file   bsp_init.c
 * @brief  片上外设和板级调试接口初始化
 */

/* ================================================== */

#include "init/bsp_init.h"
#include "display/bsp_dma2d.h"
#include "la/la_ic.h"
#include "timer/bsp_timer.h"
#include "serial/uart.h"

/* ================================================== */

/**
 * @brief  依次准备周期定时器、采集硬件、DMA2D 和串口，向应用上报首个失败
 */
bool BSP_Init(void)
{
    if (!BSP_Timer_Tick_Init())
    {
        return false;
    }
    if (BSP_LA_IC_Init() != BSP_LA_IC_OK)
    {
        return false;
    }
    if (!BSP_DMA2D_Init())
    {
        return false;
    }
    return UART_Init();
}
