/**
 * @file   hardware_service.c
 * @brief  内存、显示与触摸设备初始化
 */

/* ================================================== */

#include "hardware/hardware_service.h"
#include "display/lcd.h"
#include "w9825g6kh/w9825g6kh.h"
#include "touch/touch.h"

/* ================================================== */

bool Hardware_Service_Init(void)
{
    if (!W9825G6KH_Init() || !W9825G6KH_Selftest())
    {
        return false;
    }
    if (LCD_Init() != LCD_STATUS_OK)
    {
        return false;
    }
    (void)Touch_Init();
    return true;
}
