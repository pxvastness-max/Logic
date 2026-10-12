/**
 * @file   sd_card.c
 * @brief  板载SDMMC1 TF卡内置IDMA传输、任务同步等待接口
 */

/* ================================================== */

#include "sd/sd_card.h"
#include "sd/bsp_sdmmc.h"
#include "stm32h7xx_hal.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

/* ========================= 配置 ========================= */

#define DEVICE_SD_TRANSFER_TIMEOUT_MS  1000UL
#define DEVICE_SD_READY_TIMEOUT_MS     1000UL
#define DEVICE_SD_SECTOR_SIZE          512UL
#define DEVICE_SD_MAX_TRANSFER_SECTORS (0x01FFFFFFUL / DEVICE_SD_SECTOR_SIZE)
#define DEVICE_SD_CLOCK_DIVIDER        5UL

/* ================================================== */

static uint8_t device_sd_initialized = 0U;     /* 卡协议和几何信息均已建立，硬件失败后撤销 */
static uint8_t device_sd_hardware_active = 0U; /* 板级资源已经准备，枚举失败也必须释放它们 */

static Device_SD_Card_Info_t device_sd_card_info = {
    0U,
    0U}; /* 初始化时读取的容量和扇区大小，不能用于判断卡是否仍插着 */

/* 常驻二值信号量，初始化重试复用同一对象 */
static SemaphoreHandle_t device_sd_transfer_done = NULL;
/* IRQ 写入、等待任务读取的当前传输错误标志 */
static volatile uint8_t device_sd_transfer_failed = 0U;

/* ================================================== */

static Device_SD_Result_t Device_SD_Wait_Ready(void);

/* ================================================== */

/**
 * @brief  只允许调度器运行且相关中断未被屏蔽的任务执行阻塞卡操作
 */
static uint8_t Device_SD_Is_Allowed_Context(void)
{
    if ((__get_IPSR() != 0U) || (__get_PRIMASK() != 0U) || (__get_BASEPRI() != 0U))
    {
        return 0U;
    }
    return (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) ? 1U : 0U;
}

/**
 * @brief  清除卡的就绪状态和容量信息，并返回原错误；调用者负责确认硬件已停止
 */
static Device_SD_Result_t Device_SD_Invalidate(Device_SD_Result_t result)
{
    device_sd_initialized = 0U;
    device_sd_card_info.sector_count = 0U;
    device_sd_card_info.sector_size = 0U;
    return result;
}

/**
 * @brief  硬件出错后先停止 IDMA，再让调用者重新使用缓冲区
 * @note   HAL 启动命令和 Abort 均可能先启用 IDMA 再返回错误
 */
static Device_SD_Result_t Device_SD_Fail_Transfer(Device_SD_Result_t result)
{
    if (device_sd_hardware_active != 0U)
    {
        uint32_t error_code = hsd1.ErrorCode;

        (void)HAL_SD_Abort(&hsd1);
        error_code |= hsd1.ErrorCode;
        BSP_SDMMC_Hardware_Stop();
        hsd1.ErrorCode = error_code;
        device_sd_hardware_active = 0U;
    }
    return Device_SD_Invalidate(result);
}

static Device_SD_Result_t Device_SD_Map_Transfer_Result(HAL_StatusTypeDef result)
{
    if (result == HAL_OK)
    {
        return DEVICE_SD_OK;
    }
    if (result == HAL_TIMEOUT)
    {
        return Device_SD_Fail_Transfer(DEVICE_SD_ERROR_TIMEOUT);
    }
    return Device_SD_Fail_Transfer(DEVICE_SD_ERROR_TRANSFER);
}

static void Device_SD_Prepare_Transfer(void)
{
    while (xSemaphoreTake(device_sd_transfer_done, 0U) == pdTRUE)
    {
        /* 清除上一次传输留下的完成信号 */
    }
    device_sd_transfer_failed = 0U;
}

/**
 * @brief  等待 IDMA 完成，再确认卡已准备好下一次传输
 */
static Device_SD_Result_t Device_SD_Wait_Transfer(HAL_StatusTypeDef start_result)
{
    if (start_result != HAL_OK)
    {
        return Device_SD_Map_Transfer_Result(start_result);
    }
    if (xSemaphoreTake(device_sd_transfer_done, pdMS_TO_TICKS(DEVICE_SD_TRANSFER_TIMEOUT_MS)) !=
        pdTRUE)
    {
        return Device_SD_Fail_Transfer(DEVICE_SD_ERROR_TIMEOUT);
    }
    if ((device_sd_transfer_failed != 0U) || (hsd1.ErrorCode != HAL_SD_ERROR_NONE))
    {
        return Device_SD_Fail_Transfer(DEVICE_SD_ERROR_TRANSFER);
    }
    return Device_SD_Wait_Ready();
}

/**
 * @brief  等待卡结束当前操作；每次检查之间让出一个 tick
 */
static Device_SD_Result_t Device_SD_Wait_Ready(void)
{
    uint32_t start_tick = HAL_GetTick();

    for (;;)
    {
        HAL_SD_CardStateTypeDef card_state = HAL_SD_GetCardState(&hsd1);

        if (hsd1.ErrorCode != HAL_SD_ERROR_NONE)
        {
            return Device_SD_Fail_Transfer(DEVICE_SD_ERROR_TRANSFER);
        }
        if (card_state == HAL_SD_CARD_TRANSFER)
        {
            return DEVICE_SD_OK;
        }
        if ((HAL_GetTick() - start_tick) >= DEVICE_SD_READY_TIMEOUT_MS)
        {
            return Device_SD_Fail_Transfer(DEVICE_SD_ERROR_TIMEOUT);
        }
        if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING)
        {
            vTaskDelay((TickType_t)1U);
        }
    }
}

/**
 * @brief  读写前检查参数、是否在任务中调用、卡状态和扇区范围
 * @note   这里只检查地址非空且按 4 字节对齐，不检查缓冲区大小、DMA 可访问性或缓存状态
 */
static Device_SD_Result_t Device_SD_Check_Transfer(const uint8_t *buffer,
                                                   uint32_t sector,
                                                   uint32_t count)
{
    if ((buffer == NULL) || (((uintptr_t)buffer & 3U) != 0U) || (count == 0U) ||
        (count > DEVICE_SD_MAX_TRANSFER_SECTORS))
    {
        return DEVICE_SD_ERROR_PARAMETER;
    }
    if (Device_SD_Is_Allowed_Context() == 0U)
    {
        return DEVICE_SD_ERROR_CONTEXT;
    }
    if (Device_SD_Is_Ready() == 0U)
    {
        return DEVICE_SD_ERROR_NOT_READY;
    }
    /* 使用减法检查范围，避免sector + count产生32位整数溢出 */
    if ((sector >= device_sd_card_info.sector_count) ||
        (count > device_sd_card_info.sector_count - sector))
    {
        return DEVICE_SD_ERROR_PARAMETER;
    }
    return Device_SD_Wait_Ready();
}

/**
 * @brief  初始化SD卡
 * @note   只在调度器运行的任务中调用
 */
Device_SD_Result_t Device_SD_Init(void)
{
    HAL_SD_CardInfoTypeDef card_info;
    HAL_StatusTypeDef result;

    if (Device_SD_Is_Allowed_Context() == 0U)
    {
        return DEVICE_SD_ERROR_CONTEXT;
    }
    if (device_sd_transfer_done == NULL)
    {
        device_sd_transfer_done = xSemaphoreCreateBinary();
        if (device_sd_transfer_done == NULL)
        {
            return DEVICE_SD_ERROR_INIT;
        }
    }
    if (Device_SD_Is_Ready() != 0U)
    {
        return DEVICE_SD_OK;
    }

    (void)Device_SD_Invalidate(DEVICE_SD_ERROR_NOT_READY);
    if (device_sd_hardware_active != 0U)
    {
        if (HAL_SD_DeInit(&hsd1) != HAL_OK)
        {
            BSP_SDMMC_Hardware_Stop();
            device_sd_hardware_active = 0U;
            return DEVICE_SD_ERROR_INIT;
        }
        device_sd_hardware_active = 0U;
    }

    hsd1.Instance = SDMMC1;
    hsd1.Init.ClockEdge = SDMMC_CLOCK_EDGE_RISING;
    hsd1.Init.ClockPowerSave = SDMMC_CLOCK_POWER_SAVE_DISABLE;
    hsd1.Init.BusWide = SDMMC_BUS_WIDE_4B;
    hsd1.Init.HardwareFlowControl = SDMMC_HARDWARE_FLOW_CONTROL_DISABLE;
    hsd1.Init.ClockDiv = DEVICE_SD_CLOCK_DIVIDER;
#if (USE_SD_TRANSCEIVER != 0U)
    hsd1.Init.TranceiverPresent = SDMMC_TRANSCEIVER_NOT_PRESENT;
#endif

    if (!BSP_SDMMC_Hardware_Init())
    {
        return DEVICE_SD_ERROR_INIT;
    }
    device_sd_hardware_active = 1U;
    result = HAL_SD_Init(&hsd1);
    if (result != HAL_OK)
    {
        if (HAL_SD_DeInit(&hsd1) != HAL_OK)
        {
            BSP_SDMMC_Hardware_Stop();
        }
        device_sd_hardware_active = 0U;
        return (result == HAL_TIMEOUT) ? DEVICE_SD_ERROR_TIMEOUT : DEVICE_SD_ERROR_INIT;
    }
    if ((HAL_SD_GetCardInfo(&hsd1, &card_info) != HAL_OK) || (card_info.LogBlockNbr == 0U) ||
        (card_info.LogBlockSize != DEVICE_SD_SECTOR_SIZE))
    {
        if (HAL_SD_DeInit(&hsd1) != HAL_OK)
        {
            BSP_SDMMC_Hardware_Stop();
        }
        device_sd_hardware_active = 0U;
        return DEVICE_SD_ERROR_INIT;
    }

    device_sd_card_info.sector_count = card_info.LogBlockNbr;
    device_sd_card_info.sector_size = card_info.LogBlockSize;
    device_sd_initialized = 1U;
    return DEVICE_SD_OK;
}

Device_SD_Result_t Device_SD_DeInit(void)
{
    if (Device_SD_Is_Allowed_Context() == 0U)
    {
        return DEVICE_SD_ERROR_CONTEXT;
    }

    (void)Device_SD_Invalidate(DEVICE_SD_ERROR_NOT_READY);
    if (device_sd_hardware_active != 0U)
    {
        if (HAL_SD_DeInit(&hsd1) != HAL_OK)
        {
            BSP_SDMMC_Hardware_Stop();
            device_sd_hardware_active = 0U;
            return DEVICE_SD_ERROR_TRANSFER;
        }
        device_sd_hardware_active = 0U;
    }
    return DEVICE_SD_OK;
}

/**
 * @brief  查询软件初始化状态，不发送卡命令
 */
uint8_t Device_SD_Is_Ready(void)
{
    if (Device_SD_Is_Allowed_Context() == 0U)
    {
        return 0U;
    }
    return ((device_sd_initialized != 0U) && (hsd1.State == HAL_SD_STATE_READY)) ? 1U : 0U;
}

Device_SD_Result_t Device_SD_Get_Card_Info(Device_SD_Card_Info_t *info)
{
    if (info == NULL)
    {
        return DEVICE_SD_ERROR_PARAMETER;
    }
    if (Device_SD_Is_Allowed_Context() == 0U)
    {
        return DEVICE_SD_ERROR_CONTEXT;
    }
    if (Device_SD_Is_Ready() == 0U)
    {
        return DEVICE_SD_ERROR_NOT_READY;
    }
    *info = device_sd_card_info;
    return DEVICE_SD_OK;
}

Device_SD_Result_t Device_SD_Read_Blocks(uint8_t *buffer, uint32_t sector, uint32_t count)
{
    Device_SD_Result_t result = Device_SD_Check_Transfer(buffer, sector, count);

    if (result != DEVICE_SD_OK)
    {
        return result;
    }
    Device_SD_Prepare_Transfer();
    return Device_SD_Wait_Transfer(HAL_SD_ReadBlocks_DMA(&hsd1, buffer, sector, count));
}

Device_SD_Result_t Device_SD_Write_Blocks(const uint8_t *buffer, uint32_t sector, uint32_t count)
{
    Device_SD_Result_t result = Device_SD_Check_Transfer(buffer, sector, count);

    if (result != DEVICE_SD_OK)
    {
        return result;
    }
    Device_SD_Prepare_Transfer();
    /* HAL 将 pData 用作 IDMA 读取源，不写入缓冲；传输完成前本函数始终等待。 */
    return Device_SD_Wait_Transfer(
        HAL_SD_WriteBlocks_DMA(&hsd1, (uint8_t *)buffer, sector, count));
}

Device_SD_Result_t Device_SD_Sync(void)
{
    if (Device_SD_Is_Allowed_Context() == 0U)
    {
        return DEVICE_SD_ERROR_CONTEXT;
    }
    if (Device_SD_Is_Ready() == 0U)
    {
        return DEVICE_SD_ERROR_NOT_READY;
    }
    return Device_SD_Wait_Ready();
}

uint32_t Device_SD_Get_Last_HAL_Error(void)
{
    return hsd1.ErrorCode;
}

/**
 * @brief  在 SDMMC 完成/错误中断中保存结果并唤醒等待任务
 * @note   这里只报告状态
 */
static void Device_SD_Notify_From_ISR(uint8_t failed)
{
    BaseType_t higher_priority_task_woken = pdFALSE;

    device_sd_transfer_failed = failed;
    if (device_sd_transfer_done != NULL)
    {
        (void)xSemaphoreGiveFromISR(device_sd_transfer_done, &higher_priority_task_woken);
        portYIELD_FROM_ISR(higher_priority_task_woken);
    }
}

void HAL_SD_TxCpltCallback(SD_HandleTypeDef *hsd)
{
    if (hsd == &hsd1)
    {
        Device_SD_Notify_From_ISR(0U);
    }
}

void HAL_SD_RxCpltCallback(SD_HandleTypeDef *hsd)
{
    if (hsd == &hsd1)
    {
        Device_SD_Notify_From_ISR(0U);
    }
}

void HAL_SD_ErrorCallback(SD_HandleTypeDef *hsd)
{
    if (hsd == &hsd1)
    {
        Device_SD_Notify_From_ISR(1U);
    }
}
