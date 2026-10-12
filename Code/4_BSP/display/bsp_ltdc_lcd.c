/**
 * @file   bsp_ltdc_lcd.c
 * @brief  阿波罗 H743 板级 LTDC、RGB GPIO、背光和中断实现
 */

/* ================================================== */

#include "display/bsp_ltdc_lcd.h"
#include "memory/memory_placement.h"

/* ========================= 配置 ========================= */

#define BSP_LTDC_OVERLAY_LINE_ALIGNMENT_BYTES   64U
#define BSP_LTDC_OVERLAY_BUFFER_ALIGNMENT_BYTES 1024U
#define BSP_LTDC_ALIGN_UP(value, alignment)     (((value) + (alignment) - 1U) & ~((alignment) - 1U))
/* 三块 RGB565 帧缓冲由链接器放入 SDRAM 的 UI_DATA 区域 */
#define BSP_LTDC_MAX_FRAME_PIXELS (BSP_LTDC_FRAMEBUFFER_MAX_WIDTH * BSP_LTDC_FRAMEBUFFER_MAX_HEIGHT)
#if (3UL * BSP_LTDC_MAX_FRAME_PIXELS * BSP_LTDC_PIXEL_SIZE_BYTES) > BSP_LTDC_SDRAM_SIZE_BYTES
    #error "LTDC frame buffers exceed the SDRAM UI region"
#endif

/* ================================================== */

PLATFORM_SDRAM_UI_ZI
__attribute__((aligned(BSP_LTDC_OVERLAY_BUFFER_ALIGNMENT_BYTES))) static uint16_t
    bsp_ltdc_ui_framebuffer[BSP_LTDC_MAX_FRAME_PIXELS];

PLATFORM_SDRAM_UI_ZI
__attribute__((aligned(BSP_LTDC_OVERLAY_BUFFER_ALIGNMENT_BYTES))) static uint16_t
    bsp_ltdc_overlay_framebuffer_a[BSP_LTDC_MAX_FRAME_PIXELS];

PLATFORM_SDRAM_UI_ZI
__attribute__((aligned(BSP_LTDC_OVERLAY_BUFFER_ALIGNMENT_BYTES))) static uint16_t
    bsp_ltdc_overlay_framebuffer_b[BSP_LTDC_MAX_FRAME_PIXELS];

LTDC_HandleTypeDef bsp_ltdc_handle;                 /* STM32 HAL的LTDC外设句柄 */
static bool bsp_ltdc_ready;                         /* LTDC硬件是否初始化成功 */
static volatile uint32_t bsp_ltdc_frame_count = 0U; /* LTDC帧起始事件计数 */
static volatile uint32_t bsp_ltdc_overlay_present_count;
static volatile uint32_t bsp_ltdc_fifo_underrun_count;
static volatile uint32_t bsp_ltdc_transfer_error_count;
static volatile uint32_t bsp_ltdc_error_irq_count;
static volatile uint32_t bsp_ltdc_reload_event_count;
static volatile uint32_t bsp_ltdc_last_error_flags;
static volatile uint32_t bsp_ltdc_last_error_position;
static volatile uint32_t bsp_ltdc_last_error_display_status;
static volatile uint32_t bsp_ltdc_last_error_frame_count;
static uint16_t bsp_ltdc_display_width;
static uint16_t bsp_ltdc_display_height;
static uint16_t bsp_ltdc_overlay_width;
static uint16_t bsp_ltdc_overlay_height;
static uint16_t bsp_ltdc_overlay_stride;
static volatile uint32_t bsp_ltdc_overlay_front_address;
static volatile uint32_t bsp_ltdc_overlay_back_address;
static volatile bool bsp_ltdc_overlay_swap_pending;
static bool bsp_ltdc_overlay_layer_visible;
static bool bsp_ltdc_overlay_layer_masked; /* 调用者请求隐藏硬件 Layer2 时置位 */
static bool bsp_ltdc_overlay_layer_ready;
static BSP_LTDC_Frame_Callback_t bsp_ltdc_frame_callback;
static void *bsp_ltdc_frame_callback_context;

/* ================================================== */

/**
 * @brief  以 CPU 连续写方式初始化一块 RGB565 叠加图像帧缓冲
 */
static void BSP_LTDC_Fill_Buffer(uint16_t *buffer, uint32_t pixel_count, uint16_t color)
{
    uint32_t index;

    for (index = 0U; index < pixel_count; index++)
    {
        buffer[index] = color;
    }
}

/**
 * @brief  初始化LTDC RGB数据线、同步信号、像素时钟和背光GPIO
 */
static void BSP_LTDC_GPIO_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();
    __HAL_RCC_GPIOH_CLK_ENABLE();
    __HAL_RCC_GPIOI_CLK_ENABLE();

    gpio.Pin = GPIO_PIN_5;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &gpio);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, GPIO_PIN_RESET);

    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF14_LTDC;

    gpio.Pin = GPIO_PIN_10;
    HAL_GPIO_Init(GPIOF, &gpio);

    gpio.Pin = GPIO_PIN_6 | GPIO_PIN_7 | GPIO_PIN_11;
    HAL_GPIO_Init(GPIOG, &gpio);

    gpio.Pin = GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14 |
               GPIO_PIN_15;
    HAL_GPIO_Init(GPIOH, &gpio);

    gpio.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6 |
               GPIO_PIN_7 | GPIO_PIN_9 | GPIO_PIN_10;
    HAL_GPIO_Init(GPIOI, &gpio);
}

/**
 * @brief  配置PLL3作为LTDC像素时钟
 */
static bool BSP_LTDC_Clock_Config(uint16_t pll3_r)
{
    RCC_PeriphCLKInitTypeDef clock = {0};

    clock.PeriphClockSelection = RCC_PERIPHCLK_LTDC;
    clock.PLL3.PLL3M = 25U;
    clock.PLL3.PLL3N = 300U;
    clock.PLL3.PLL3P = 2U;
    clock.PLL3.PLL3Q = 2U;
    clock.PLL3.PLL3R = pll3_r;
    clock.PLL3.PLL3RGE = RCC_PLL3VCIRANGE_0;
    clock.PLL3.PLL3VCOSEL = RCC_PLL3VCOWIDE;
    clock.PLL3.PLL3FRACN = 0U;
    return (HAL_RCCEx_PeriphCLKConfig(&clock) == HAL_OK);
}

/**
 * @brief  在配置 RGB 复用前读取 PG6/PI2/PI7 面板识别位，供设备层选择面板时序
 */
uint8_t BSP_LTDC_LCD_Read_ID_Bits(void)
{
    GPIO_InitTypeDef gpio = {0};
    uint8_t id_bits;

    __HAL_RCC_GPIOG_CLK_ENABLE();
    __HAL_RCC_GPIOI_CLK_ENABLE();
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio.Pin = GPIO_PIN_6;
    HAL_GPIO_Init(GPIOG, &gpio);
    gpio.Pin = GPIO_PIN_2 | GPIO_PIN_7;
    HAL_GPIO_Init(GPIOI, &gpio);
    HAL_Delay(1U);

    id_bits = (uint8_t)HAL_GPIO_ReadPin(GPIOG, GPIO_PIN_6);
    id_bits |= (uint8_t)HAL_GPIO_ReadPin(GPIOI, GPIO_PIN_2) << 1;
    id_bits |= (uint8_t)HAL_GPIO_ReadPin(GPIOI, GPIO_PIN_7) << 2;
    return id_bits;
}

/**
 * @brief  HAL 初始化 LTDC 时配置 RGB GPIO，并分别启用正常事件与错误事件中断
 */
void HAL_LTDC_MspInit(LTDC_HandleTypeDef *hltdc)
{
    (void)hltdc;
    __HAL_RCC_LTDC_CLK_ENABLE();
    BSP_LTDC_GPIO_Init();

    /* STM32H7将行事件/重载和FIFO欠载/传输错误分配到两个NVIC入口 两个入口都必须启用 */
    HAL_NVIC_SetPriority(LTDC_IRQn, 5U, 0U);
    HAL_NVIC_EnableIRQ(LTDC_IRQn);
    HAL_NVIC_SetPriority(LTDC_ER_IRQn, 5U, 0U);
    HAL_NVIC_EnableIRQ(LTDC_ER_IRQn);
}

/**
 * @brief  根据设备层时序初始化 LTDC 整屏 UI 层与帧起始事件，成功后才点亮背光
 */
BSP_LTDC_Status_t BSP_LTDC_LCD_Init(const BSP_LTDC_Config_t *config)
{
    LTDC_LayerCfgTypeDef layer = {0};

    bsp_ltdc_ready = false;
    if (config == NULL || config->width == 0U || config->height == 0U ||
        config->width > BSP_LTDC_FRAMEBUFFER_MAX_WIDTH ||
        config->height > BSP_LTDC_FRAMEBUFFER_MAX_HEIGHT)
    {
        return BSP_LTDC_STATUS_INIT_ERROR;
    }
    if (!BSP_LTDC_Clock_Config(config->pll3_r))
    {
        return BSP_LTDC_STATUS_CLOCK_ERROR;
    }

    bsp_ltdc_handle.Instance = LTDC;
    bsp_ltdc_handle.Init.HSPolarity = LTDC_HSPOLARITY_AL;
    bsp_ltdc_handle.Init.VSPolarity = LTDC_VSPOLARITY_AL;
    bsp_ltdc_handle.Init.DEPolarity = LTDC_DEPOLARITY_AL;
    bsp_ltdc_handle.Init.PCPolarity = config->pixel_clock_polarity;
    bsp_ltdc_handle.Init.HorizontalSync = config->hsw - 1U;
    bsp_ltdc_handle.Init.VerticalSync = config->vsw - 1U;
    bsp_ltdc_handle.Init.AccumulatedHBP = config->hsw + config->hbp - 1U;
    bsp_ltdc_handle.Init.AccumulatedVBP = config->vsw + config->vbp - 1U;
    bsp_ltdc_handle.Init.AccumulatedActiveW = config->hsw + config->hbp + config->width - 1U;
    bsp_ltdc_handle.Init.AccumulatedActiveH = config->vsw + config->vbp + config->height - 1U;
    bsp_ltdc_handle.Init.TotalWidth = config->hsw + config->hbp + config->width + config->hfp - 1U;
    bsp_ltdc_handle.Init.TotalHeigh = config->vsw + config->vbp + config->height + config->vfp - 1U;
    bsp_ltdc_handle.Init.Backcolor.Red = 0U;
    bsp_ltdc_handle.Init.Backcolor.Green = 0U;
    bsp_ltdc_handle.Init.Backcolor.Blue = 0U;
    if (HAL_LTDC_Init(&bsp_ltdc_handle) != HAL_OK)
    {
        return BSP_LTDC_STATUS_INIT_ERROR;
    }

    layer.WindowX0 = 0U;
    layer.WindowX1 = config->width;
    layer.WindowY0 = 0U;
    layer.WindowY1 = config->height;
    layer.PixelFormat = LTDC_PIXEL_FORMAT_RGB565;
    layer.Alpha = 255U;
    layer.Alpha0 = 0U;
    layer.BlendingFactor1 = LTDC_BLENDING_FACTOR1_CA;
    layer.BlendingFactor2 = LTDC_BLENDING_FACTOR2_CA;
    layer.FBStartAdress = (uint32_t)(uintptr_t)bsp_ltdc_ui_framebuffer;
    layer.ImageWidth = config->width;
    layer.ImageHeight = config->height;
    if (HAL_LTDC_ConfigLayer(&bsp_ltdc_handle, &layer, 0U) != HAL_OK)
    {
        return BSP_LTDC_STATUS_LAYER_ERROR;
    }

    __HAL_LTDC_CLEAR_FLAG(&bsp_ltdc_handle, LTDC_FLAG_LI);
    if (HAL_LTDC_ProgramLineEvent(&bsp_ltdc_handle, 0U) != HAL_OK)
    {
        return BSP_LTDC_STATUS_INIT_ERROR;
    }

    bsp_ltdc_frame_count = 0U;
    bsp_ltdc_overlay_present_count = 0U;
    bsp_ltdc_fifo_underrun_count = 0U;
    bsp_ltdc_transfer_error_count = 0U;
    bsp_ltdc_error_irq_count = 0U;
    bsp_ltdc_reload_event_count = 0U;
    bsp_ltdc_last_error_flags = 0U;
    bsp_ltdc_last_error_position = 0U;
    bsp_ltdc_last_error_display_status = 0U;
    bsp_ltdc_last_error_frame_count = 0U;
    bsp_ltdc_display_width = config->width;
    bsp_ltdc_display_height = config->height;
    bsp_ltdc_overlay_width = 0U;
    bsp_ltdc_overlay_height = 0U;
    bsp_ltdc_overlay_stride = 0U;
    bsp_ltdc_overlay_front_address = 0U;
    bsp_ltdc_overlay_back_address = 0U;
    bsp_ltdc_overlay_swap_pending = false;
    bsp_ltdc_overlay_layer_visible = false;
    bsp_ltdc_overlay_layer_masked = false;
    bsp_ltdc_overlay_layer_ready = false;
    bsp_ltdc_ready = true;
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, GPIO_PIN_SET);
    return BSP_LTDC_STATUS_OK;
}

/**
 * @brief  查询LTDC就绪状态
 */
bool BSP_LTDC_LCD_Is_Ready(void)
{
    return bsp_ltdc_ready;
}

/**
 * @brief  取得整屏RGB565帧缓冲
 * @note   只在SDRAM初始化成功后访问
 */
uint16_t *BSP_LTDC_LCD_Get_Framebuffer(void)
{
    return bsp_ltdc_ui_framebuffer;
}

/**
 * @brief  在每帧起始增加帧计数、重新装载行事件并通知显示服务
 */
void HAL_LTDC_LineEventCallback(LTDC_HandleTypeDef *hltdc)
{
    if (hltdc->Instance == LTDC)
    {
        ++bsp_ltdc_frame_count;
        (void)HAL_LTDC_ProgramLineEvent(hltdc, 0U);
        if (bsp_ltdc_frame_callback != NULL)
        {
            bsp_ltdc_frame_callback(bsp_ltdc_frame_callback_context);
        }
    }
}

/**
 * @brief  LTDC 已在垂直消隐期锁存 Layer 1 新地址后触发
 */
void HAL_LTDC_ReloadEventCallback(LTDC_HandleTypeDef *hltdc)
{
    if (hltdc->Instance != LTDC)
    {
        return;
    }

    ++bsp_ltdc_reload_event_count;
    if (bsp_ltdc_overlay_swap_pending)
    {
        const uint32_t old_front_address = bsp_ltdc_overlay_front_address;

        bsp_ltdc_overlay_front_address = bsp_ltdc_overlay_back_address;
        bsp_ltdc_overlay_back_address = old_front_address;
        bsp_ltdc_overlay_layer_visible = true;
        bsp_ltdc_overlay_swap_pending = false;
        ++bsp_ltdc_overlay_present_count;
    }
}

/**
 * @brief  返回累计帧起始事件数，用于显示任务判定帧边界
 */
uint32_t BSP_LTDC_LCD_Get_Frame_Count(void)
{
    return bsp_ltdc_frame_count;
}

/**
 * @brief  返回 HAL 持续锁存的 LTDC FIFO 欠载与传输错误位
 */
uint32_t BSP_LTDC_LCD_Get_Error_Code(void)
{
    /* HAL 在 LTDC 中断中锁存 FU/TE；后续正常换帧不会清除这些诊断位 */
    return bsp_ltdc_handle.ErrorCode;
}

/**
 * @brief  返回初始化后累计的 LTDC FIFO 欠载事件数
 */
uint32_t BSP_LTDC_LCD_Get_Fifo_Underrun_Count(void)
{
    return bsp_ltdc_fifo_underrun_count;
}

/**
 * @brief  返回初始化后累计的 LTDC 总线传输错误事件数
 */
uint32_t BSP_LTDC_LCD_Get_Transfer_Error_Count(void)
{
    return bsp_ltdc_transfer_error_count;
}

/**
 * @brief  在 HAL 清除错误标志前保存错误发生时的像素位置和显示状态
 */
void BSP_LTDC_LCD_Capture_Error_IRQ(void)
{
    const uint32_t flags = LTDC->ISR & LTDC->IER & (LTDC_ISR_FUIF | LTDC_ISR_TERRIF);

    ++bsp_ltdc_error_irq_count;
    if (flags != 0U)
    {
        bsp_ltdc_last_error_flags = flags;
        bsp_ltdc_last_error_position = LTDC->CPSR;
        bsp_ltdc_last_error_display_status = LTDC->CDSR;
        bsp_ltdc_last_error_frame_count = bsp_ltdc_frame_count;
    }
}

/**
 * @brief  关中断后一起读取 LTDC 错误、事件次数和换帧状态
 */
bool BSP_LTDC_LCD_Get_Error_Diagnostic(BSP_LTDC_Error_Diagnostic_t *diagnostic)
{
    uint32_t primask;

    if (!bsp_ltdc_ready || diagnostic == NULL)
    {
        return false;
    }

    primask = __get_PRIMASK();
    __disable_irq();
    diagnostic->fifo_underrun_count = bsp_ltdc_fifo_underrun_count;
    diagnostic->transfer_error_count = bsp_ltdc_transfer_error_count;
    diagnostic->error_irq_count = bsp_ltdc_error_irq_count;
    diagnostic->line_event_count = bsp_ltdc_frame_count;
    diagnostic->reload_event_count = bsp_ltdc_reload_event_count;
    diagnostic->present_count = bsp_ltdc_overlay_present_count;
    diagnostic->hal_error_code = bsp_ltdc_handle.ErrorCode;
    diagnostic->interrupt_flags = LTDC->ISR;
    diagnostic->interrupt_enable = LTDC->IER;
    diagnostic->last_error_flags = bsp_ltdc_last_error_flags;
    diagnostic->last_error_position = bsp_ltdc_last_error_position;
    diagnostic->last_error_display_status = bsp_ltdc_last_error_display_status;
    diagnostic->last_error_frame_count = bsp_ltdc_last_error_frame_count;
    diagnostic->swap_pending = bsp_ltdc_overlay_swap_pending;
    if (primask == 0U)
    {
        __enable_irq();
    }
    return true;
}

/**
 * @brief  统计 HAL 检出的欠载/传输错误并重新打开其错误中断以捕获后续事件
 */
void HAL_LTDC_ErrorCallback(LTDC_HandleTypeDef *hltdc)
{
    uint32_t enabled_interrupts;

    if (hltdc == NULL || hltdc->Instance != LTDC)
    {
        return;
    }

    enabled_interrupts = READ_REG(hltdc->Instance->IER);

    /* HAL会在回调前关闭对应错误中断；计数后重新打开，才能统计后续事件 */
    if ((enabled_interrupts & LTDC_IER_FUIE) == 0U)
    {
        ++bsp_ltdc_fifo_underrun_count;
        __HAL_LTDC_ENABLE_IT(hltdc, LTDC_IT_FU);
    }
    if ((enabled_interrupts & LTDC_IER_TERRIE) == 0U)
    {
        ++bsp_ltdc_transfer_error_count;
        __HAL_LTDC_ENABLE_IT(hltdc, LTDC_IT_TE);
    }
}

/**
 * @brief  原子更新帧起始回调与上下文，传入 NULL 可取消通知
 */
void BSP_LTDC_LCD_Set_Frame_Callback(BSP_LTDC_Frame_Callback_t callback, void *context)
{
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    bsp_ltdc_frame_callback_context = context;
    bsp_ltdc_frame_callback = callback;
    if (primask == 0U)
    {
        __enable_irq();
    }
}

/**
 * @brief  初始化RGB565叠加层双缓冲
 * @note   黑色色键透明，首帧在present后显示
 */
bool BSP_LTDC_LCD_Overlay_Layer_Init(uint16_t x,
                                     uint16_t y,
                                     uint16_t width,
                                     uint16_t height,
                                     uint16_t background_color)
{
    LTDC_LayerCfgTypeDef layer = {0};
    uint32_t overlay_line_bytes;
    uint32_t overlay_stride_bytes;
    uint32_t overlay_buffer_bytes;
    uint32_t front_address;
    uint32_t back_address;

    if (!bsp_ltdc_ready || width == 0U || height == 0U || x >= bsp_ltdc_display_width ||
        y >= bsp_ltdc_display_height || ((uint32_t)x + width) > bsp_ltdc_display_width ||
        ((uint32_t)y + height) > bsp_ltdc_display_height)
    {
        return false;
    }

    /* LTDC以突发方式读取帧缓冲 */
    overlay_line_bytes = (uint32_t)width * BSP_LTDC_PIXEL_SIZE_BYTES;
    overlay_stride_bytes =
        BSP_LTDC_ALIGN_UP(overlay_line_bytes, BSP_LTDC_OVERLAY_LINE_ALIGNMENT_BYTES);
    overlay_buffer_bytes = overlay_stride_bytes * height;

    if (overlay_buffer_bytes > sizeof(bsp_ltdc_overlay_framebuffer_a) ||
        overlay_buffer_bytes > sizeof(bsp_ltdc_overlay_framebuffer_b))
    {
        return false;
    }
    front_address = (uint32_t)(uintptr_t)bsp_ltdc_overlay_framebuffer_a;
    back_address = (uint32_t)(uintptr_t)bsp_ltdc_overlay_framebuffer_b;

    /* 先写背景，再使 Layer 1 可见，避免首次启用时扫描到未初始化 SDRAM */
    BSP_LTDC_Fill_Buffer((uint16_t *)front_address,
                         (overlay_stride_bytes / BSP_LTDC_PIXEL_SIZE_BYTES) * height,
                         background_color);
    BSP_LTDC_Fill_Buffer((uint16_t *)back_address,
                         (overlay_stride_bytes / BSP_LTDC_PIXEL_SIZE_BYTES) * height,
                         background_color);

    layer.WindowX0 = x;
    layer.WindowX1 = (uint32_t)x + width;
    layer.WindowY0 = y;
    layer.WindowY1 = (uint32_t)y + height;
    layer.PixelFormat = LTDC_PIXEL_FORMAT_RGB565;
    /* 启动阶段先完全透明，首次 present 时才在垂直同步中显示叠加图像 Layer */
    layer.Alpha = 0U;
    layer.Alpha0 = 0U;
    /* Layer 2 窗口外使用默认颜色的 Alpha0 */
    layer.BlendingFactor1 = LTDC_BLENDING_FACTOR1_PAxCA;
    layer.BlendingFactor2 = LTDC_BLENDING_FACTOR2_PAxCA;
    layer.FBStartAdress = front_address;
    /* ImageWidth决定CFBP物理步长；WindowX仍保持500像素可见宽度 */
    layer.ImageWidth = overlay_stride_bytes / BSP_LTDC_PIXEL_SIZE_BYTES;
    layer.ImageHeight = height;
    if (HAL_LTDC_ConfigLayer(&bsp_ltdc_handle, &layer, 1U) != HAL_OK)
    {
        return false;
    }
    /* Layer 1 只承载彩色叠加图像线 */
    if (HAL_LTDC_ConfigColorKeying(&bsp_ltdc_handle, 0x00000000UL, 1U) != HAL_OK ||
        HAL_LTDC_EnableColorKeying(&bsp_ltdc_handle, 1U) != HAL_OK)
    {
        return false;
    }

    bsp_ltdc_overlay_width = width;
    bsp_ltdc_overlay_height = height;
    bsp_ltdc_overlay_stride = (uint16_t)(overlay_stride_bytes / BSP_LTDC_PIXEL_SIZE_BYTES);
    bsp_ltdc_overlay_front_address = front_address;
    bsp_ltdc_overlay_back_address = back_address;
    bsp_ltdc_overlay_swap_pending = false;
    bsp_ltdc_overlay_present_count = 0U;
    bsp_ltdc_overlay_layer_visible = false;
    bsp_ltdc_overlay_layer_masked = false;
    bsp_ltdc_overlay_layer_ready = true;
    return true;
}

/**
 * @brief  在垂直消隐期设置叠加图像硬件层的可见性
 */
bool BSP_LTDC_LCD_Overlay_Layer_Set_Visible(bool visible)
{
    bool previous_masked;
    uint32_t primask;

    if (!bsp_ltdc_overlay_layer_ready)
    {
        return false;
    }

    primask = __get_PRIMASK();
    __disable_irq();
    previous_masked = bsp_ltdc_overlay_layer_masked;
    bsp_ltdc_overlay_layer_masked = !visible;
    if (HAL_LTDC_SetAlpha_NoReload(&bsp_ltdc_handle, visible ? 255U : 0U, 1U) != HAL_OK ||
        HAL_LTDC_Reload(&bsp_ltdc_handle, LTDC_RELOAD_VERTICAL_BLANKING) != HAL_OK)
    {
        bsp_ltdc_overlay_layer_masked = previous_masked;
        if (primask == 0U)
        {
            __enable_irq();
        }
        return false;
    }
    if (primask == 0U)
    {
        __enable_irq();
    }
    return true;
}

/**
 * @brief  取得叠加层后缓冲
 * @note   等待换帧期间返回NULL
 */
uint16_t *BSP_LTDC_LCD_Overlay_Layer_Get_Back_Buffer(void)
{
    if (!bsp_ltdc_overlay_layer_ready || bsp_ltdc_overlay_swap_pending)
    {
        return NULL;
    }

    return (uint16_t *)bsp_ltdc_overlay_back_address;
}

/**
 * @brief  返回叠加图像窗口可见宽度，硬件层未初始化时返回 0
 */
uint16_t BSP_LTDC_LCD_Overlay_Layer_Get_Width(void)
{
    return bsp_ltdc_overlay_layer_ready ? bsp_ltdc_overlay_width : 0U;
}

/**
 * @brief  返回叠加图像窗口可见高度，硬件层未初始化时返回 0
 */
uint16_t BSP_LTDC_LCD_Overlay_Layer_Get_Height(void)
{
    return bsp_ltdc_overlay_layer_ready ? bsp_ltdc_overlay_height : 0U;
}

/**
 * @brief  返回叠加图像缓冲物理行距（像素），包含满足 LTDC 突发对齐的行尾填充
 */
uint16_t BSP_LTDC_LCD_Overlay_Layer_Get_Stride(void)
{
    return bsp_ltdc_overlay_layer_ready ? bsp_ltdc_overlay_stride : 0U;
}

/**
 * @brief  提交叠加层后缓冲
 * @note   在下一个垂直消隐期换帧
 */
bool BSP_LTDC_LCD_Overlay_Layer_Present(void)
{
    uint32_t primask;

    if (!bsp_ltdc_overlay_layer_ready || bsp_ltdc_overlay_swap_pending)
    {
        return false;
    }

    /* 防止VBlank重载中断在HAL_LTDC_Reload()返回后、pending置位前插入 否则硬件已换帧 */
    primask = __get_PRIMASK();
    __disable_irq();
    if (HAL_LTDC_SetAddress_NoReload(&bsp_ltdc_handle, bsp_ltdc_overlay_back_address, 1U) != HAL_OK)
    {
        if (primask == 0U)
        {
            __enable_irq();
        }
        return false;
    }
    if (!bsp_ltdc_overlay_layer_visible && !bsp_ltdc_overlay_layer_masked &&
        HAL_LTDC_SetAlpha_NoReload(&bsp_ltdc_handle, 255U, 1U) != HAL_OK)
    {
        if (primask == 0U)
        {
            __enable_irq();
        }
        return false;
    }
    bsp_ltdc_overlay_swap_pending = true;
    if (HAL_LTDC_Reload(&bsp_ltdc_handle, LTDC_RELOAD_VERTICAL_BLANKING) != HAL_OK)
    {
        bsp_ltdc_overlay_swap_pending = false;
        if (primask == 0U)
        {
            __enable_irq();
        }
        return false;
    }
    if (primask == 0U)
    {
        __enable_irq();
    }
    return true;
}

/**
 * @brief  返回已由 LTDC 重载中断确认完成的叠加图像换帧次数
 */
uint32_t BSP_LTDC_LCD_Overlay_Layer_Get_Present_Count(void)
{
    return bsp_ltdc_overlay_present_count;
}

/**
 * @brief  同时读取叠加层缓冲状态和 LTDC Layer2 寄存器，供故障排查
 */
bool BSP_LTDC_LCD_Overlay_Layer_Get_Diagnostic(BSP_LTDC_Overlay_Diagnostic_t *diagnostic)
{
    uint32_t primask;

    if (!bsp_ltdc_overlay_layer_ready || diagnostic == NULL)
    {
        return false;
    }

    primask = __get_PRIMASK();
    __disable_irq();
    diagnostic->software_front_address = bsp_ltdc_overlay_front_address;
    diagnostic->software_back_address = bsp_ltdc_overlay_back_address;
    diagnostic->hardware_frame_address = LTDC_Layer2->CFBAR;
    diagnostic->frame_buffer_length = LTDC_Layer2->CFBLR;
    diagnostic->frame_buffer_line_count = LTDC_Layer2->CFBLNR;
    diagnostic->horizontal_window = LTDC_Layer2->WHPCR;
    diagnostic->vertical_window = LTDC_Layer2->WVPCR;
    diagnostic->control = LTDC_Layer2->CR;
    diagnostic->pixel_format = LTDC_Layer2->PFCR;
    diagnostic->constant_alpha = LTDC_Layer2->CACR;
    diagnostic->blending = LTDC_Layer2->BFCR;
    diagnostic->color_key = LTDC_Layer2->CKCR;
    diagnostic->present_count = bsp_ltdc_overlay_present_count;
    diagnostic->swap_pending = bsp_ltdc_overlay_swap_pending;
    __set_PRIMASK(primask);
    return true;
}
