/**
 * @file   platform_dwt.c
 * @brief  DWT 周期计数和时间转换
 */

/* ================================================== */

#include "time/platform_dwt.h"
#include "platform_dwt_config.h"
#include PLATFORM_DWT_CMSIS_HEADER

/* ========================= 配置 ========================= */

#if (PLATFORM_DWT_CLOCK_HZ == 0U)
    #error "PLATFORM_DWT_CLOCK_HZ must be non-zero"
#endif

/* ================================================== */

static volatile uint64_t platform_dwt_accumulated_cycles;
static uint32_t platform_dwt_last_cycle;
static bool platform_dwt_initialized;

/* ================================================== */

/**
 * @brief  保存 PRIMASK 并关闭中断，保护两次采样之间的累计值更新
 */
static uint32_t Platform_DWT_Lock(void)
{
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    return primask;
}

/**
 * @brief  恢复进入临界区前的中断屏蔽状态，允许已屏蔽中断的调用者嵌套使用
 */
static void Platform_DWT_Unlock(uint32_t primask)
{
    __set_PRIMASK(primask);
}

/**
 * @brief  将最新无符号 32 位采样差加入 64 位累计值，跨越一次硬件回绕
 * @note   调用者持有 PRIMASK 临界区
 */
static void Platform_DWT_Update_Locked(void)
{
    uint32_t current_cycle = DWT->CYCCNT;

    platform_dwt_accumulated_cycles += current_cycle - platform_dwt_last_cycle;
    platform_dwt_last_cycle = current_cycle;
}

/**
 * @brief  开启 DWT CYCCNT，保留调试器已启用的硬件计数并重置本模块累计起点
 */
bool Platform_DWT_Init(void)
{
    uint32_t primask;

    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    if ((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) == 0U)
    {
        DWT->CYCCNT = 0U;
        DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

        __DSB();
        __ISB();
    }

    if ((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) == 0U)
    {
        return false;
    }

    primask = Platform_DWT_Lock();
    platform_dwt_accumulated_cycles = 0U;
    platform_dwt_last_cycle = DWT->CYCCNT;
    platform_dwt_initialized = true;
    Platform_DWT_Unlock(primask);

    return true;
}

/**
 * @brief  维护64位周期累计值
 * @note   相邻维护间隔须小于32位计数回绕周期
 */
void Platform_DWT_Maintain(void)
{
    uint32_t primask;

    if (!Platform_DWT_Is_Enabled())
    {
        return;
    }

    primask = Platform_DWT_Lock();
    Platform_DWT_Update_Locked();
    Platform_DWT_Unlock(primask);
}

/**
 * @brief  检查软件累计器已初始化且硬件周期计数仍处于启用状态
 */
bool Platform_DWT_Is_Enabled(void)
{
    return platform_dwt_initialized && ((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) != 0U);
}

/**
 * @brief  读取硬件周期计数，不维护 64 位累计状态，适合短区间耗时测量
 */
uint32_t Platform_DWT_Cycles(void)
{
    return DWT->CYCCNT;
}

/**
 * @brief  通过无符号差值计算距离指定周期采样的耗时，允许跨越一次回绕
 */
uint32_t Platform_DWT_Elapsed(uint32_t start_cycle)
{
    return Platform_DWT_Cycles() - start_cycle;
}

/**
 * @brief  在临界区内采样并读取累计 CPU 周期，未初始化时返回 0
 */
uint64_t Platform_DWT_Cycles64(void)
{
    uint64_t cycles;
    uint32_t primask;

    if (!Platform_DWT_Is_Enabled())
    {
        return 0U;
    }

    primask = Platform_DWT_Lock();
    Platform_DWT_Update_Locked();
    cycles = platform_dwt_accumulated_cycles;
    Platform_DWT_Unlock(primask);

    return cycles;
}

/**
 * @brief  将累计周期拆分为整秒与余数后换算微秒，避免直接乘法溢出
 */
uint64_t Platform_DWT_Time_Us(void)
{
    uint64_t cycles = Platform_DWT_Cycles64();

    return ((cycles / PLATFORM_DWT_CLOCK_HZ) * 1000000ULL) +
           (((cycles % PLATFORM_DWT_CLOCK_HZ) * 1000000ULL) / PLATFORM_DWT_CLOCK_HZ);
}

/**
 * @brief  将累计周期拆分为整秒与余数后换算毫秒，避免直接乘法溢出
 */
uint64_t Platform_DWT_Time_Ms(void)
{
    uint64_t cycles = Platform_DWT_Cycles64();

    return ((cycles / PLATFORM_DWT_CLOCK_HZ) * 1000ULL) +
           (((cycles % PLATFORM_DWT_CLOCK_HZ) * 1000ULL) / PLATFORM_DWT_CLOCK_HZ);
}

/**
 * @brief  按固定 CPU 时钟契约把一个 32 位周期区间换算为微秒
 */
uint32_t Platform_DWT_Cycles_To_Us(uint32_t cycles)
{
    return (uint32_t)(((uint64_t)cycles * 1000000ULL) / PLATFORM_DWT_CLOCK_HZ);
}

/**
 * @brief  给出 32 位 CYCCNT 回绕周期的微秒上界，供维护定时器检查采样间隔
 */
uint32_t Platform_DWT_Max_Maintain_Interval_Us(void)
{
    return (uint32_t)(((uint64_t)UINT32_MAX * 1000000ULL) / PLATFORM_DWT_CLOCK_HZ);
}
