/**
 * @file   platform_memory.c
 * @brief  Cortex-M7 MPU 与 L1 Cache 配置
 */

/* ================================================== */

#include "memory/platform_memory.h"
#include "stm32h7xx_hal.h"

/* ========================= 配置 ========================= */

#define PLATFORM_MEMORY_CACHE_LINE_BYTES 32U
#define PLATFORM_MEMORY_DMA_SUBREGION_DISABLE 0xE0U /* 128 KiB 区域保留前 5 个 16 KiB 子区 */

/* ================================================== */

void Platform_Memory_Bootstrap(void)
{
    MPU_Region_InitTypeDef mpu = {0};

    __HAL_RCC_D2SRAM1_CLK_ENABLE();
    __HAL_RCC_D2SRAM2_CLK_ENABLE();
    __HAL_RCC_D2SRAM3_CLK_ENABLE();
    HAL_MPU_Disable();
    mpu.Enable = MPU_REGION_ENABLE;
    mpu.Number = MPU_REGION_NUMBER0;
    mpu.BaseAddress = 0U;
    mpu.Size = MPU_REGION_SIZE_4GB;
    mpu.SubRegionDisable = 0x87U;
    mpu.TypeExtField = MPU_TEX_LEVEL0;
    mpu.AccessPermission = MPU_REGION_NO_ACCESS;
    mpu.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
    mpu.IsShareable = MPU_ACCESS_SHAREABLE;
    mpu.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
    mpu.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;
    HAL_MPU_ConfigRegion(&mpu);

    /* Flash 可执行、只读，使用可缓存的 Write-through 属性。 */
    mpu.Number = MPU_REGION_NUMBER1;
    mpu.BaseAddress = 0x08000000U;
    mpu.Size = MPU_REGION_SIZE_2MB;
    mpu.SubRegionDisable = 0x00U;
    mpu.TypeExtField = MPU_TEX_LEVEL0;
    mpu.AccessPermission = MPU_REGION_PRIV_RO_URO;
    mpu.DisableExec = MPU_INSTRUCTION_ACCESS_ENABLE;
    mpu.IsShareable = MPU_ACCESS_NOT_SHAREABLE;
    mpu.IsCacheable = MPU_ACCESS_CACHEABLE;
    mpu.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;
    HAL_MPU_ConfigRegion(&mpu);

    /* ITCM 由启动代码加载，只读且允许执行。 */
    mpu.Number = MPU_REGION_NUMBER2;
    mpu.BaseAddress = 0x00000000U;
    mpu.Size = MPU_REGION_SIZE_64KB;
    mpu.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
    HAL_MPU_ConfigRegion(&mpu);

    /* DTCM 存放栈和 RTOS 堆，不经过 D-Cache。 */
    mpu.Number = MPU_REGION_NUMBER3;
    mpu.BaseAddress = 0x20000000U;
    mpu.Size = MPU_REGION_SIZE_128KB;
    mpu.TypeExtField = MPU_TEX_LEVEL1;
    mpu.AccessPermission = MPU_REGION_FULL_ACCESS;
    mpu.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
    HAL_MPU_ConfigRegion(&mpu);

    /* AXI SRAM、SRAM1/2/3/4 使用可缓存的 Write-back 属性。 */
    mpu.Number = MPU_REGION_NUMBER4;
    mpu.BaseAddress = 0x24000000U;
    mpu.Size = MPU_REGION_SIZE_512KB;
    mpu.TypeExtField = MPU_TEX_LEVEL0;
    mpu.IsCacheable = MPU_ACCESS_CACHEABLE;
    mpu.IsBufferable = MPU_ACCESS_BUFFERABLE;
    HAL_MPU_ConfigRegion(&mpu);

    mpu.Number = MPU_REGION_NUMBER5;
    mpu.BaseAddress = 0x30000000U;
    mpu.Size = MPU_REGION_SIZE_128KB;
    HAL_MPU_ConfigRegion(&mpu);

    /* 更高优先级的区域覆盖 SRAM1 前 80 KiB，供 DMA 直接访问。 */
    mpu.Number = MPU_REGION_NUMBER6;
    mpu.SubRegionDisable = PLATFORM_MEMORY_DMA_SUBREGION_DISABLE;
    mpu.TypeExtField = MPU_TEX_LEVEL1;
    mpu.IsShareable = MPU_ACCESS_SHAREABLE;
    mpu.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
    mpu.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;
    HAL_MPU_ConfigRegion(&mpu);

    mpu.Number = MPU_REGION_NUMBER7;
    mpu.BaseAddress = 0x30020000U;
    mpu.SubRegionDisable = 0x00U;
    mpu.TypeExtField = MPU_TEX_LEVEL0;
    mpu.IsShareable = MPU_ACCESS_NOT_SHAREABLE;
    mpu.IsCacheable = MPU_ACCESS_CACHEABLE;
    mpu.IsBufferable = MPU_ACCESS_BUFFERABLE;
    HAL_MPU_ConfigRegion(&mpu);

    mpu.Number = MPU_REGION_NUMBER8;
    mpu.BaseAddress = 0x30040000U;
    mpu.Size = MPU_REGION_SIZE_32KB;
    HAL_MPU_ConfigRegion(&mpu);

    mpu.Number = MPU_REGION_NUMBER9;
    mpu.BaseAddress = 0x38000000U;
    mpu.Size = MPU_REGION_SIZE_64KB;
    HAL_MPU_ConfigRegion(&mpu);

    HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
}

void Platform_Memory_Enable_Cache(void)
{
    SCB_EnableICache();
    SCB_EnableDCache();
}

/**
 * @brief  配置SDRAM内存属性
 * @note   只在SDRAM硬件初始化成功后调用
 */
void Platform_SDRAM_MPU_Config(uint32_t base_address)
{
    MPU_Region_InitTypeDef mpu = {0};
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    __DSB();
    __ISB();
    HAL_MPU_Disable();
    mpu.Enable = MPU_REGION_ENABLE;
    /* 只开放已初始化的 SDRAM，继续保持整片不可缓存。 */
    mpu.Number = MPU_REGION_NUMBER10;
    mpu.BaseAddress = base_address;
    mpu.Size = MPU_REGION_SIZE_32MB;
    mpu.SubRegionDisable = 0x00U;
    mpu.TypeExtField = MPU_TEX_LEVEL1;
    mpu.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
    mpu.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;
    mpu.IsShareable = MPU_ACCESS_SHAREABLE;
    mpu.AccessPermission = MPU_REGION_FULL_ACCESS;
    mpu.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
    HAL_MPU_ConfigRegion(&mpu);
    HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
    __DSB();
    __ISB();
    __set_PRIMASK(primask);
}

void Platform_Memory_Barrier(void)
{
    __DSB();
}

void Platform_Memory_Clean_Cache(const void *address, uint32_t byte_count)
{
#if (__DCACHE_PRESENT == 1U)
    uintptr_t start;
    uintptr_t end;

    if ((byte_count != 0U) && ((SCB->CCR & SCB_CCR_DC_Msk) != 0U))
    {
        start = (uintptr_t)address & ~(uintptr_t)(PLATFORM_MEMORY_CACHE_LINE_BYTES - 1U);
        end = ((uintptr_t)address + byte_count + PLATFORM_MEMORY_CACHE_LINE_BYTES - 1U) &
              ~(uintptr_t)(PLATFORM_MEMORY_CACHE_LINE_BYTES - 1U);
        SCB_CleanDCache_by_Addr((uint32_t *)start, (int32_t)(end - start));
    }
#else
    (void)address;
    (void)byte_count;
#endif
}

void Platform_Memory_Invalidate_Cache(void *address, uint32_t byte_count)
{
#if (__DCACHE_PRESENT == 1U)
    uintptr_t start;
    uintptr_t end;

    if ((byte_count != 0U) && ((SCB->CCR & SCB_CCR_DC_Msk) != 0U))
    {
        start = (uintptr_t)address & ~(uintptr_t)(PLATFORM_MEMORY_CACHE_LINE_BYTES - 1U);
        end = ((uintptr_t)address + byte_count + PLATFORM_MEMORY_CACHE_LINE_BYTES - 1U) &
              ~(uintptr_t)(PLATFORM_MEMORY_CACHE_LINE_BYTES - 1U);
        SCB_InvalidateDCache_by_Addr((uint32_t *)start, (int32_t)(end - start));
    }
#else
    (void)address;
    (void)byte_count;
#endif
}
