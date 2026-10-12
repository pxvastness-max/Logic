/**
 * @file   main.c
 * @brief  程序从这里进入，由应用层初始化并启动任务调度
 */

/* ================================================== */

#include "main/app_init.h"
#include "system/platform_system.h"

/* ================================================== */

/**
 * @brief  初始化应用并启动任务调度
 * @note   具体启动顺序、服务调度和运行状态由应用层管理
 * @retval 无正常返回，初始化失败或调度器返回时进入统一故障停机
 */
int main(void)
{
    if (!App_Init())
    {
        Platform_Fatal_Error();
    }

    App_Run();
}
