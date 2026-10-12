/**
 * @file   wave_service.c
 * @brief  波形显示与采集数据管理
 */

/* ================================================== */

#include "waveform/wave_service.h"
#include "waveform/wave_raster.h"
#include "gui/channels/gui_channels.h"
#include "waveform/wave_capture_data.h"
#include "memory/memory_placement.h"
#include "memory/platform_memory.h"
#include "display/bsp_dma2d.h"
#include "display/bsp_ltdc_lcd.h"
#include "FreeRTOS.h"
#include "gui_config.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"
#include <string.h>

/* ========================= 配置 ========================= */

#define WAVE_TIME_LABEL_SLOTS        12U
#define WAVE_TIME_TICK_MIN_COUNT     5U
#define WAVE_MICROSECONDS_PER_SECOND 1000000UL
#define WAVE_DEFAULT_TIMESTAMP_HZ    24000000U
#define WAVE_DEFAULT_DURATION_US     200000U
#define WAVE_DEFAULT_CAPTURE_FILTER  8U
#define WAVE_PREPARE_TASK_STACK      768U
#define WAVE_PREPARE_TASK_PRIORITY   1U
#define WAVE_DMA_TASK_STACK          384U
#define WAVE_DMA_TASK_PRIORITY       2U

/* 一块固定容量的片内 RGB565 条带：准备任务与发送任务通过队列交接所有权 */
#define WAVE_DMA_BUFFER_COUNT 1U

#define WAVE_DMA_TILE_LINES             26U
#define WAVE_DMA_TILE_STRIDE_PIXELS     512U
#define WAVE_DMA_BUFFER_CAPACITY_PIXELS (WAVE_DMA_TILE_STRIDE_PIXELS * WAVE_DMA_TILE_LINES)
#define WAVE_BACKGROUND_COLOR           0x0A0F14U
#define WAVE_TIME_TICK_COLOR            0x7D8B99U
#define WAVE_TIME_TEXT_COLOR            0xB9C5D0U
#define WAVE_PROTOCOL_NAME_SIZE         16U

/* ================================================== */

typedef enum
{
    WAVE_STATE_UNINITIALIZED = 0, /* 尚未调用 Wave_Init() */
    WAVE_STATE_CONFIGURED,        /* 已初始化，允许修改和提交配置 */
    WAVE_STATE_CAPTURING          /* 正在采集，只允许写数据和刷新波形 */
} Wave_State_t;

typedef struct
{
    uint8_t display_slot; /* 当前占用的显示槽位，0 表示最上面一行 */
    int16_t top;          /* 当前显示行顶部，相对整个屏幕 */
    int16_t height;       /* 当前显示行高度 */
    int16_t high_offset;  /* 高电平 Y，相对当前行顶部 */
    int16_t low_offset;   /* 低电平 Y，相对当前行顶部 */
} Wave_Channel_Layout_t;

typedef GUI_Channel_Row_t Wave_Channel_Render_t;

typedef struct
{
    uint8_t source_id;            /* 固定物理通道编号，不随显示位置改变 */
    Wave_Channel_Config_t config; /* 不随每帧变化的界面配置 */
    Wave_Channel_Layout_t layout; /* 由启用顺序计算出的内部显示位置 */
    Wave_Channel_Render_t render; /* 左侧 LVGL 对象句柄和本帧坐标缓存 */
} Wave_Channel_t;

typedef struct
{
    lv_obj_t *parent;                 /* 本模块所有界面对象的外部父对象 */
    lv_obj_t *left_root;              /* x=0~99 的左侧静态信息根容器 */
    lv_obj_t *wave_root;              /* x=100~599 的 Layer 0 背景与时间标尺根容器 */
    lv_obj_t *time_ruler;             /* 波形区顶部时间刻度对象 */
    lv_obj_t *protocol_title;         /* 与时间刻度同高的左上角“协议名 + ×”整体按钮 */
    lv_obj_t *protocol_delete_button; /* 与protocol_title指向同一按钮 */
    lv_obj_t *protocol_result[WAVE_PROTOCOL_RESULT_ROWS]; /* 左侧两行解析通道角色 */
    uint16_t protocol_result_count;                       /* Layer 1中待绘制的数据记录数量 */
    /* GUI 保存的待绘制记录，容量为 512 条，准备任务复制后才使用 */
    Wave_Protocol_Result_t protocol_results[WAVE_PROTOCOL_RESULT_LIMIT];
    int32_t display_height;           /* 当前显示器纵向分辨率 */

    Wave_Channel_t channels[WAVE_CHANNEL_COUNT]; /* 六路物理通道的全部状态 */
    Wave_View_t view;                            /* 当前整帧显示的时间范围 */
    Wave_State_t state;                          /* 模块当前所处的生命周期状态 */
    bool configuration_applied;                  /* 是否已按配置创建通道控件 */
    bool protocol_layout_active;                 /* true 时预留顶部两行给协议结果 */
    /* GUI 设置的数值格式，准备任务按帧固化 */
    Wave_Protocol_Display_Format_t protocol_display_format;
    bool protocol_result_channels[WAVE_CHANNEL_COUNT]; /* 顶部只显示数据通道 */
    char protocol_name[WAVE_PROTOCOL_NAME_SIZE];       /* 当前协议名称 */
    uint32_t capture_timer_start_cnt;                  /* 点击开始采集时的原始定时器 CNT */
    uint32_t capture_duration_cnt;                     /* 一轮采集计划持续的 CNT */
    uint32_t capture_duration_us;                      /* 一轮采集计划持续的微秒数 */
    uint32_t capture_timestamp_hz;                     /* 本轮硬件时间戳频率 */
    uint8_t capture_filter;                            /* 下一轮六路统一使用的硬件输入滤波编码 */
    uint32_t captured_end_cnt;                         /* 当前轮已经采集到的最后 CNT */
    uint32_t live_window_size;                         /* 采集过程中使用的实时滑动窗口宽度 */
    uint32_t last_live_range_end_cnt;                  /* 上次读取实时增量范围的右边界 */
    uint32_t pending_view_start_cnt;                   /* 手势请求的目标窗口左端 */
    uint32_t pending_window_size;                      /* 手势请求的目标窗口宽度 */
    bool follow_live;                                  /* true 时采集中窗口自动跟随最新 CNT */
    bool view_dirty;                                   /* 手势有新目标窗口，等待后缓冲可写 */
    bool render_requested;                             /* 下一次帧信号到来时重建 Layer 1 */
    uint8_t clear_frames_remaining;                    /* 配置改变后依次清理两块SDRAM缓冲 */

    /* 绘制任务执行完前，标签字符串必须一直有效 */
    char time_labels[WAVE_TIME_LABEL_SLOTS][16];
} Wave_Context_t;

typedef struct
{
    uint8_t buffer_index;        /* COPY任务占用的源缓冲编号 */
    uint16_t width;              /* 源条带有效行宽，单位为 RGB565 像素 */
    uint16_t height;             /* 本次搬运的有效行数，范围为 1~26 */
    uint16_t destination_stride; /* 目标SDRAM相邻两行的像素步长 */
    const uint16_t *source;      /* COPY子任务在片内条带中的起始地址 */
    uint16_t *destination;       /* 本次搬运的 SDRAM 后缓冲目标起点 */
    bool first_in_frame;         /* 首条任务重置整帧搬运结果 */
    bool last_in_frame;          /* 末条任务负责请求换帧并发送整帧完成通知 */
    bool content_valid;          /* false时完成搬运但不提交本帧 */
} Wave_DMA_Job_t;

/* ================================================== */

static Wave_Context_t wave;
static uint16_t wave_task_columns[WAVE_CAPTURE_DATA_COLUMN_COUNT];
static Wave_Protocol_Result_t wave_protocol_render_results[WAVE_PROTOCOL_RESULT_LIMIT];

/* 512×26 RGB565 条带由队列交接，六路列状态每帧查询一次 */
/* DMA2D 源放在 D2 SRAM1 非缓存区，保持 CPU 与 DMA 一致 */
PLATFORM_DMA_NOCACHE_ZI PLATFORM_CACHE_ALIGNMENT_ATTRIBUTE static uint16_t
    wave_dma_buffers[WAVE_DMA_BUFFER_COUNT][WAVE_DMA_BUFFER_CAPACITY_PIXELS];

static SemaphoreHandle_t wave_frame_semaphore;
static SemaphoreHandle_t wave_dma_done_semaphore;
static SemaphoreHandle_t wave_frame_done_semaphore;
static QueueHandle_t wave_free_buffer_queue;
static QueueHandle_t wave_ready_job_queue;
static TaskHandle_t wave_prepare_task_handle;
static TaskHandle_t wave_dma_task_handle;
static volatile uint32_t wave_latest_frame_tick;
static volatile uint32_t wave_frame_divider_count;
static volatile bool wave_gui_update_pending;
static volatile bool wave_dma_transfer_success;
static volatile bool wave_dma_frame_success;
static bool wave_frame_callback_started;
static const uint32_t wave_default_colors[WAVE_CHANNEL_COUNT] =
    {0x37D67AU, 0x4BA3FFU, 0xFFB84DU, 0xE36BFFU, 0x62E8D5U, 0xFF7A90U};

/* ================================================== */

static void Wave_Init_Default_Config(Wave_Channel_t *channel, uint32_t channel_index);
static void Wave_Assign_Display_Slot(Wave_Channel_t *channel, uint32_t display_slot);
static bool Wave_Create_Roots(lv_obj_t *parent, int32_t display_height);
static void Wave_Reset_Render_Handles(Wave_Channel_t *channel);
static bool Wave_Create_Time_Ruler(void);
static bool Wave_Create_Protocol_Widgets(void);
static void Wave_Protocol_Delete_Event_Callback(lv_event_t *event);
static void Wave_Protocol_Delete_Async_Callback(void *context);
static bool Wave_Create_Channel_Widgets(Wave_Channel_t *channel);
static void Wave_Update_View(uint32_t relative_now_cnt);
static int32_t Wave_Cnt_To_X(uint32_t cnt, const Wave_View_t *view);
static uint32_t Wave_Clamp_View_Start(uint32_t requested_start_cnt,
                                      uint32_t window_cnt,
                                      uint32_t available_end_cnt);
static uint32_t Wave_Get_Zoom_Window(uint32_t current_window_cnt,
                                     uint32_t max_window_cnt,
                                     bool zoom_in);
static uint32_t Wave_Us_To_Cnt(uint32_t duration_us);
static uint32_t Wave_Cnt_To_Us(uint32_t cnt);
static void Wave_Draw_Time_Ruler_Callback(lv_event_t *event);
static bool Wave_Finish_Capture(uint32_t end_cnt);
static uint32_t Wave_Get_Time_Tick_Cnt(uint32_t window_cnt);
static void Wave_Format_Time_Label(char *buffer,
                                   uint32_t buffer_size,
                                   uint32_t cnt,
                                   uint32_t tick_cnt);
static void Wave_Frame_Start_ISR(void *context);
static void Wave_Prepare_Task(void *parameter);
static void Wave_DMA_Send_Task(void *parameter);
static void Wave_DMA_Complete_ISR(void *context, bool success);
static bool Wave_Copy_Job_By_CPU(const Wave_DMA_Job_t *job);
static bool Wave_Render_Frame_At_Tick(uint32_t frame_tick);
static bool Wave_Make_Frame_Snapshot(uint32_t frame_tick, Wave_Frame_Snapshot_t *snapshot);
static bool Wave_Queue_Render_Snapshot(uint16_t *pixels, const Wave_Frame_Snapshot_t *snapshot);

/* ================================================== */

bool Wave_Display_Task_Create(void)
{
    uint8_t buffer_index;

    if (wave_prepare_task_handle != NULL && wave_dma_task_handle != NULL)
    {
        return true;
    }

    wave_frame_semaphore = xSemaphoreCreateBinary();
    wave_dma_done_semaphore = xSemaphoreCreateBinary();
    wave_frame_done_semaphore = xSemaphoreCreateBinary();
    wave_free_buffer_queue = xQueueCreate(WAVE_DMA_BUFFER_COUNT, sizeof(buffer_index));
    wave_ready_job_queue = xQueueCreate(WAVE_DMA_BUFFER_COUNT, sizeof(Wave_DMA_Job_t));
    if (wave_frame_semaphore == NULL || wave_dma_done_semaphore == NULL ||
        wave_frame_done_semaphore == NULL || wave_free_buffer_queue == NULL ||
        wave_ready_job_queue == NULL)
    {
        goto create_failed;
    }

    /* 空闲队列携带缓冲编号，避免两个任务同时取得同一块源缓冲 */
    for (buffer_index = 0U; buffer_index < WAVE_DMA_BUFFER_COUNT; buffer_index++)
    {
        if (xQueueSend(wave_free_buffer_queue, &buffer_index, 0U) != pdPASS)
        {
            goto create_failed;
        }
    }

    if (xTaskCreate(Wave_DMA_Send_Task,
                    "wave_dma",
                    WAVE_DMA_TASK_STACK,
                    NULL,
                    WAVE_DMA_TASK_PRIORITY,
                    &wave_dma_task_handle) != pdPASS)
    {
        goto create_failed;
    }

    if (xTaskCreate(Wave_Prepare_Task,
                    "wave_prep",
                    WAVE_PREPARE_TASK_STACK,
                    NULL,
                    WAVE_PREPARE_TASK_PRIORITY,
                    &wave_prepare_task_handle) != pdPASS)
    {
        goto create_failed;
    }

    return true;

create_failed:
    if (wave_prepare_task_handle != NULL)
    {
        vTaskDelete(wave_prepare_task_handle);
        wave_prepare_task_handle = NULL;
    }
    if (wave_dma_task_handle != NULL)
    {
        vTaskDelete(wave_dma_task_handle);
        wave_dma_task_handle = NULL;
    }
    if (wave_ready_job_queue != NULL)
    {
        vQueueDelete(wave_ready_job_queue);
        wave_ready_job_queue = NULL;
    }
    if (wave_free_buffer_queue != NULL)
    {
        vQueueDelete(wave_free_buffer_queue);
        wave_free_buffer_queue = NULL;
    }
    if (wave_frame_done_semaphore != NULL)
    {
        vSemaphoreDelete(wave_frame_done_semaphore);
        wave_frame_done_semaphore = NULL;
    }
    if (wave_dma_done_semaphore != NULL)
    {
        vSemaphoreDelete(wave_dma_done_semaphore);
        wave_dma_done_semaphore = NULL;
    }
    if (wave_frame_semaphore != NULL)
    {
        vSemaphoreDelete(wave_frame_semaphore);
        wave_frame_semaphore = NULL;
    }
    return false;
}

void Wave_Display_Task_Start(void)
{
    if (wave_frame_semaphore == NULL || wave_frame_callback_started)
    {
        return;
    }

    wave_frame_callback_started = true;
    wave_frame_divider_count = 0U;
    BSP_LTDC_LCD_Set_Frame_Callback(Wave_Frame_Start_ISR, NULL);

    /* 不必等待下一帧才显示初始化结果；首次绘制也走同一个任务入口 */
    wave_latest_frame_tick = (uint32_t)xTaskGetTickCount();
    (void)xSemaphoreGive(wave_frame_semaphore);
}

void Wave_Process_GUI_Updates(void)
{
    bool invalidate;

    taskENTER_CRITICAL();
    invalidate = wave_gui_update_pending;
    wave_gui_update_pending = false;
    taskEXIT_CRITICAL();

    if (invalidate && wave.time_ruler != NULL)
    {
        lv_obj_invalidate(wave.time_ruler);
    }
}

/* ================================================== */

/**
 * @brief  按 LTDC 场周期分频发布最新波形绘制时刻
 * @note   LTDC 中断上下文，使用 FromISR 通知
 */
static void Wave_Frame_Start_ISR(void *context)
{
    BaseType_t higher_priority_task_woken = pdFALSE;

    (void)context;
    if (wave_frame_semaphore == NULL)
    {
        return;
    }

#if WAVE_REFRESH_FRAME_DIVIDER < 1U
    #error "WAVE_REFRESH_FRAME_DIVIDER must be at least 1"
#endif
    /* LTDC继续逐帧扫描，仅降低波形准备和DMA2D的启动频率 */
    wave_frame_divider_count++;
    if (wave_frame_divider_count < WAVE_REFRESH_FRAME_DIVIDER)
    {
        return;
    }
    wave_frame_divider_count = 0U;

    /* 覆盖最新帧时刻；二值信号量已满时不会累计过期帧 */
    wave_latest_frame_tick = (uint32_t)xTaskGetTickCountFromISR();
    (void)xSemaphoreGiveFromISR(wave_frame_semaphore, &higher_priority_task_woken);
    portYIELD_FROM_ISR(higher_priority_task_woken);
}

/**
 * @brief  等待帧通知并生成一致窗口下的波形后缓冲
 * @note   独占列状态和准备中的 RGB565 条带
 */
static void Wave_Prepare_Task(void *parameter)
{
    (void)parameter;

    for (;;)
    {
        uint32_t frame_tick;

        (void)xSemaphoreTake(wave_frame_semaphore, portMAX_DELAY);
        frame_tick = wave_latest_frame_tick;

        if (Wave_Render_Frame_At_Tick(frame_tick))
        {
            taskENTER_CRITICAL();
            wave_gui_update_pending = true;
            taskEXIT_CRITICAL();
        }
    }
}

/**
 * @brief  串行发送条带并在整帧成功后请求垂直消隐换帧
 * @note   任一条带失败时不提交整帧
 */
static void Wave_DMA_Send_Task(void *parameter)
{
    Wave_DMA_Job_t job;
    bool frame_success = false;

    (void)parameter;

    for (;;)
    {
        bool transfer_started;
        bool transfer_success = false;

        (void)xQueueReceive(wave_ready_job_queue, &job, portMAX_DELAY);
        if (job.first_in_frame)
        {
            frame_success = true;
        }
        frame_success = frame_success && job.content_valid;

        wave_dma_transfer_success = false;
#if WAVE_TRANSFER_USE_DMA2D
        transfer_started = BSP_DMA2D_Copy_RGB565_Async(job.source,
                                                       job.destination,
                                                       job.width,
                                                       job.height,
                                                       job.destination_stride,
                                                       Wave_DMA_Complete_ISR,
                                                       NULL);
        if (transfer_started && xSemaphoreTake(wave_dma_done_semaphore, portMAX_DELAY) == pdTRUE)
        {
            transfer_success = wave_dma_transfer_success;
        }
#else
        transfer_started = Wave_Copy_Job_By_CPU(&job);
        transfer_success = transfer_started;
#endif

        /* 搬运完成后才归还源缓冲，准备任务此时才能安全覆盖它 */
        if (job.buffer_index < WAVE_DMA_BUFFER_COUNT)
        {
            (void)xQueueSend(wave_free_buffer_queue, &job.buffer_index, portMAX_DELAY);
        }

        frame_success = frame_success && transfer_success;
        if (job.last_in_frame)
        {
            if (frame_success)
            {
                frame_success = BSP_LTDC_LCD_Overlay_Layer_Present();
            }
            wave_dma_frame_success = frame_success;
            (void)xSemaphoreGive(wave_frame_done_semaphore);
        }
    }
}

/**
 * @brief  同步把紧密排列的 RGB565 条带逐行复制到 SDRAM 后缓冲
 * @note   复制期间源条带归发送任务所有
 */
static bool Wave_Copy_Job_By_CPU(const Wave_DMA_Job_t *job)
{
    const uint16_t *source;
    uint32_t row;

    if (job == NULL || job->buffer_index >= WAVE_DMA_BUFFER_COUNT || job->source == NULL ||
        job->destination == NULL || job->width == 0U || job->height == 0U ||
        job->destination_stride < job->width)
    {
        return false;
    }

    source = job->source;
    for (row = 0U; row < job->height; row++)
    {
        memcpy(&job->destination[row * job->destination_stride],
               &source[row * job->width],
               (size_t)job->width * sizeof(source[0]));
    }

    /* 确保所有CPU写入在提交LTDC垂直消隐换帧之前完成 */
    Platform_Memory_Barrier();
    return true;
}

/**
 * @brief  保存 DMA2D 搬运结果并唤醒发送任务
 * @note   DMA2D 中断只记录结果并唤醒任务
 */
static void Wave_DMA_Complete_ISR(void *context, bool success)
{
    BaseType_t higher_priority_task_woken = pdFALSE;

    (void)context;
    wave_dma_transfer_success = success;
    if (wave_dma_done_semaphore != NULL)
    {
        (void)xSemaphoreGiveFromISR(wave_dma_done_semaphore, &higher_priority_task_woken);
    }
    portYIELD_FROM_ISR(higher_priority_task_woken);
}

/**
 * @brief  复制单帧视图、通道布局和协议记录以隔离共享状态
 * @note   临界区内复制视图和最多 512 条协议记录
 */
static bool Wave_Make_Frame_Snapshot(uint32_t frame_tick, Wave_Frame_Snapshot_t *snapshot)
{
    uint32_t available_end_cnt;
    uint32_t channel_index;
    bool view_changed = false;
    bool should_render;

    if (snapshot == NULL)
    {
        return false;
    }

    available_end_cnt = Wave_Capture_Data_Get_Cnt_At_Tick(frame_tick);

    taskENTER_CRITICAL();
    if (wave.state == WAVE_STATE_UNINITIALIZED || !wave.configuration_applied)
    {
        taskEXIT_CRITICAL();
        return false;
    }

    if (wave.state == WAVE_STATE_CAPTURING)
    {
        wave.captured_end_cnt = available_end_cnt;
    }

    if (wave.view_dirty)
    {
        wave.pending_view_start_cnt = Wave_Clamp_View_Start(wave.pending_view_start_cnt,
                                                            wave.pending_window_size,
                                                            available_end_cnt);
        wave.view.start_cnt = wave.pending_view_start_cnt;
        wave.view.window_size = wave.pending_window_size;
        wave.view.now_cnt = wave.view.start_cnt + wave.view.window_size;
        wave.view_dirty = false;
        view_changed = true;
    }
    else if (wave.state == WAVE_STATE_CAPTURING && wave.follow_live)
    {
        Wave_Update_View(available_end_cnt);
        view_changed = true;
    }

    should_render = wave.render_requested || view_changed || wave.clear_frames_remaining != 0U;
    if (!should_render)
    {
        taskEXIT_CRITICAL();
        return false;
    }

    wave.render_requested = false;
    snapshot->view = wave.view;
    snapshot->protocol_layout_active = wave.protocol_layout_active;
    snapshot->protocol_display_format = wave.protocol_display_format;
    snapshot->protocol_result_count = wave.protocol_result_count;
    if (snapshot->protocol_result_count != 0U)
    {
        memcpy(wave_protocol_render_results,
               wave.protocol_results,
               (size_t)snapshot->protocol_result_count * sizeof(wave_protocol_render_results[0]));
    }
    snapshot->clear_background = wave.clear_frames_remaining != 0U;
    for (channel_index = 0U; channel_index < WAVE_CHANNEL_COUNT; channel_index++)
    {
        const Wave_Channel_t *channel = &wave.channels[channel_index];
        Wave_Frame_Channel_t *destination = &snapshot->channels[channel_index];

        destination->enabled = channel->config.enabled;
        destination->source_id = channel->source_id;
        destination->color = channel->config.color;
        destination->high_y = (int16_t)(channel->layout.top + channel->layout.high_offset);
        destination->low_y = (int16_t)(channel->layout.top + channel->layout.low_offset);
    }
    taskEXIT_CRITICAL();
    return true;
}

/**
 * @brief  取得可写后缓冲并执行当前场周期的绘制请求
 * @note   前次换帧完成后才可写后缓冲
 */
static bool Wave_Render_Frame_At_Tick(uint32_t frame_tick)
{
    Wave_Frame_Snapshot_t snapshot;
    uint16_t *pixels;

    /* 换帧尚未完成时不准备数据，也不触碰正在等待锁存的后缓冲 */
    pixels = BSP_LTDC_LCD_Overlay_Layer_Get_Back_Buffer();
    if (pixels == NULL || !Wave_Make_Frame_Snapshot(frame_tick, &snapshot))
    {
        return false;
    }

    if (!Wave_Queue_Render_Snapshot(pixels, &snapshot))
    {
        taskENTER_CRITICAL();
        wave.render_requested = true;
        taskEXIT_CRITICAL();
        return false;
    }

    if (snapshot.clear_background)
    {
        taskENTER_CRITICAL();
        if (wave.clear_frames_remaining != 0U)
        {
            wave.clear_frames_remaining--;
        }
        taskEXIT_CRITICAL();
    }

    return true;
}

/**
 * @brief  查询一次六路列状态并按轨道串行提交固定高度的像素条带
 * @note   取得空闲条带才可写入，提交后等待发送任务归还
 */
static bool Wave_Queue_Render_Snapshot(uint16_t *pixels, const Wave_Frame_Snapshot_t *snapshot)
{
    const uint32_t width = BSP_LTDC_LCD_Overlay_Layer_Get_Width();
    const uint32_t height = BSP_LTDC_LCD_Overlay_Layer_Get_Height();
    const uint32_t stride = BSP_LTDC_LCD_Overlay_Layer_Get_Stride();
    Wave_DMA_Job_t job = {0};
    uint32_t channel_index;
    uint32_t last_enabled = WAVE_CHANNEL_COUNT;
    uint32_t valid_columns = 0U;
    uint8_t initial_levels = 0U;
    bool content_valid = true;
    bool first_channel_job = true;

    if (pixels == NULL || snapshot == NULL || width == 0U || width > WAVE_AREA_WIDTH ||
        height == 0U || stride != WAVE_DMA_TILE_STRIDE_PIXELS)
    {
        return false;
    }

    for (channel_index = 0U; channel_index < WAVE_CHANNEL_COUNT; channel_index++)
    {
        if (snapshot->channels[channel_index].enabled)
        {
            last_enabled = channel_index;
        }
    }
    if (last_enabled == WAVE_CHANNEL_COUNT)
    {
        return false;
    }

    /* 六路通道都使用同一段起止时间，避免这一帧的数据来自不同时刻 */
    if (!Wave_Capture_Data_Request_Window(snapshot->view.start_cnt,
                                          snapshot->view.now_cnt,
                                          snapshot->view.window_size,
                                          wave_task_columns,
                                          &valid_columns,
                                          &initial_levels))
    {
        return false;
    }
    if (valid_columns > WAVE_AREA_WIDTH)
    {
        valid_columns = WAVE_AREA_WIDTH;
    }

    /* 无数据且无需清屏时跳过搬运，避免显示空帧和占用 SDRAM 总线 */
    if (valid_columns == 0U && !snapshot->clear_background && !snapshot->protocol_layout_active)
    {
        return true;
    }

    /* CPU 清空后缓冲，避免大块 DMA2D 连续占用 SDRAM 导致 LTDC 欠载 */
    if (snapshot->clear_background)
    {
        memset(pixels, 0, stride * height * sizeof(pixels[0]));
        Platform_Memory_Barrier();
    }

    /* 协议块与数字直接写入Layer 1；每次缩放/平移都按当前CNT窗口重新映射 */
    if (snapshot->protocol_layout_active)
    {
        Wave_Raster_Render_Protocol_Results(pixels,
                                            width,
                                            height,
                                            stride,
                                            snapshot,
                                            wave_protocol_render_results);
        Platform_Memory_Barrier();
    }

    for (channel_index = 0U; channel_index <= last_enabled; channel_index++)
    {
        const Wave_Frame_Channel_t *channel = &snapshot->channels[channel_index];
        uint32_t tile_y;
        uint32_t tile_height;
        uint8_t buffer_index;
        uint16_t *tile_buffer;

        if (!channel->enabled)
        {
            continue;
        }

        /* 每路发送高低电平间条带，含首尾共 26 行 */
        if (channel->high_y <= channel->low_y)
        {
            tile_y = (uint32_t)(channel->high_y - WAVE_TIME_RULER_HEIGHT);
            tile_height = (uint32_t)(channel->low_y - channel->high_y) + 1U;
        }
        else
        {
            tile_y = (uint32_t)(channel->low_y - WAVE_TIME_RULER_HEIGHT);
            tile_height = (uint32_t)(channel->high_y - channel->low_y) + 1U;
        }
        if (tile_height != WAVE_DMA_TILE_LINES || tile_y >= height || tile_y + tile_height > height)
        {
            content_valid = false;
            tile_y = 0U;
            tile_height = (height < WAVE_DMA_TILE_LINES) ? height : WAVE_DMA_TILE_LINES;
        }

        if (xQueueReceive(wave_free_buffer_queue, &buffer_index, portMAX_DELAY) != pdPASS)
        {
            return false;
        }
        tile_buffer = wave_dma_buffers[buffer_index];
        memset(tile_buffer, 0, stride * tile_height * sizeof(tile_buffer[0]));
        if (content_valid)
        {
            Wave_Raster_Render_Channel_Tile(tile_buffer,
                                            tile_y,
                                            tile_height,
                                            stride,
                                            channel,
                                            wave_task_columns,
                                            valid_columns,
                                            initial_levels);
        }

        job.buffer_index = buffer_index;
        job.width = (uint16_t)stride;
        job.height = (uint16_t)tile_height;
        job.destination_stride = (uint16_t)stride;
        job.source = tile_buffer;
        job.destination = &pixels[tile_y * stride];
        job.first_in_frame = first_channel_job;
        job.last_in_frame = channel_index == last_enabled;
        job.content_valid = content_valid;
        if (xQueueSend(wave_ready_job_queue, &job, portMAX_DELAY) != pdPASS)
        {
            return false;
        }
        first_channel_job = false;
    }

    /* 最后一个开启通道的26行DMA完成并提交VBlank后，才准备下一帧 */
    if (xSemaphoreTake(wave_frame_done_semaphore, portMAX_DELAY) != pdTRUE)
    {
        return false;
    }
    return wave_dma_frame_success && content_valid;
}

/* ================================================== */

bool Wave_Init(lv_obj_t *parent)
{
    lv_display_t *display;
    int32_t display_width;
    int32_t display_height;
    uint32_t channel_index;

    if (parent == NULL || wave.state != WAVE_STATE_UNINITIALIZED)
    {
        return false;
    }

    display = lv_obj_get_display(parent);
    if (display == NULL)
    {
        return false;
    }

    display_height = lv_display_get_vertical_resolution(display);
    display_width = lv_display_get_horizontal_resolution(display);
    if (display_width < (WAVE_AREA_X + WAVE_AREA_WIDTH) ||
        display_height <
            WAVE_TIME_RULER_HEIGHT + (int32_t)(WAVE_CHANNEL_COUNT * WAVE_DMA_TILE_LINES))
    {
        return false;
    }

    memset(&wave, 0, sizeof(wave));
    Wave_Capture_Data_Init();
    wave.parent = parent;
    wave.display_height = display_height;
    wave.capture_timestamp_hz = WAVE_DEFAULT_TIMESTAMP_HZ;
    wave.capture_duration_us = WAVE_DEFAULT_DURATION_US;
    wave.capture_filter = WAVE_DEFAULT_CAPTURE_FILTER;
    wave.capture_duration_cnt =
        (uint32_t)(((uint64_t)wave.capture_timestamp_hz * wave.capture_duration_us) / 1000000ULL);
    /* 实时采集窗口固定从1秒开始，采集完成后再切换到完整总览 */
    wave.view.window_size = wave.capture_timestamp_hz;
    wave.live_window_size = wave.capture_timestamp_hz;

    /* LVGL 使用 RAM 绘制缓冲；波形层使用 SDRAM 双缓冲，VSYNC 换帧 */
    if (!BSP_LTDC_LCD_Overlay_Layer_Init(WAVE_AREA_X,
                                         WAVE_TIME_RULER_HEIGHT,
                                         WAVE_AREA_WIDTH,
                                         (uint16_t)(display_height - WAVE_TIME_RULER_HEIGHT),
                                         0U))
    {
        memset(&wave, 0, sizeof(wave));
        return false;
    }

    for (channel_index = 0U; channel_index < WAVE_CHANNEL_COUNT; channel_index++)
    {
        Wave_Init_Default_Config(&wave.channels[channel_index], channel_index);
    }

    wave.state = WAVE_STATE_CONFIGURED;
    return true;
}

void Wave_Deinit(void)
{
    if (wave.wave_root != NULL)
    {
        lv_obj_delete(wave.wave_root);
    }
    if (wave.left_root != NULL)
    {
        lv_obj_delete(wave.left_root);
    }

    memset(&wave, 0, sizeof(wave));
}

bool Wave_Get_Channel_Config(uint32_t channel_index, Wave_Channel_Config_t *config)
{
    if (config == NULL || channel_index >= WAVE_CHANNEL_COUNT ||
        wave.state == WAVE_STATE_UNINITIALIZED)
    {
        return false;
    }

    *config = wave.channels[channel_index].config;
    return true;
}

bool Wave_Set_Channel_Config(uint32_t channel_index, const Wave_Channel_Config_t *config)
{
    Wave_Channel_Config_t *destination;

    if (config == NULL || channel_index >= WAVE_CHANNEL_COUNT ||
        wave.state != WAVE_STATE_CONFIGURED)
    {
        return false;
    }

    /* 完整配置入口也遵守“至少保留一路”的约束，不能绕过单通道开关 */
    if (!config->enabled && !Wave_Set_Channel_Enabled(channel_index, false))
    {
        return false;
    }

    destination = &wave.channels[channel_index].config;
    *destination = *config;

    /* 无论调用者怎样构造配置，都保证内部字符串有结尾 */
    destination->pin_text[WAVE_PIN_TEXT_SIZE - 1U] = '\0';
    destination->name_text[WAVE_NAME_TEXT_SIZE - 1U] = '\0';
    destination->detail_text[WAVE_DETAIL_TEXT_SIZE - 1U] = '\0';
    return true;
}

bool Wave_Set_Channel_Enabled(uint32_t channel_index, bool enabled)
{
    uint32_t enabled_count = 0U;
    uint32_t index;

    if (channel_index >= WAVE_CHANNEL_COUNT || wave.state != WAVE_STATE_CONFIGURED)
    {
        return false;
    }

    if (!enabled && wave.channels[channel_index].config.enabled)
    {
        for (index = 0U; index < WAVE_CHANNEL_COUNT; index++)
        {
            if (wave.channels[index].config.enabled)
            {
                enabled_count++;
            }
        }

        /* 波形区至少保留一路，避免删除最后一路后失去可用显示轨道 */
        if (enabled_count <= 1U)
        {
            return false;
        }
    }

    wave.channels[channel_index].config.enabled = enabled;
    return true;
}

bool Wave_Apply_Configuration(void)
{
    uint32_t channel_index;
    uint32_t display_slot = 0U;
    bool had_configuration;

    if (wave.state != WAVE_STATE_CONFIGURED || wave.parent == NULL)
    {
        return false;
    }

    /* 第一次提交配置时才创建根容器，使主任务可以先创建右侧静态页面 */
    if (wave.left_root == NULL || wave.wave_root == NULL)
    {
        if (!Wave_Create_Roots(wave.parent, wave.display_height))
        {
            return false;
        }
    }

    /* 配置只在停止状态提交；只清理 LVGL 控件，绝不清空已申请的采集数据 */
    had_configuration = wave.configuration_applied;
    wave.configuration_applied = false;
    lv_obj_clean(wave.left_root);
    lv_obj_clean(wave.wave_root);
    wave.time_ruler = NULL;
    wave.protocol_title = NULL;
    wave.protocol_delete_button = NULL;
    memset(wave.protocol_result, 0, sizeof(wave.protocol_result));
    wave.protocol_result_count = 0U;

    for (channel_index = 0U; channel_index < WAVE_CHANNEL_COUNT; channel_index++)
    {
        Wave_Reset_Render_Handles(&wave.channels[channel_index]);
    }

    if (!Wave_Create_Time_Ruler())
    {
        return false;
    }
    if (wave.protocol_layout_active && !Wave_Create_Protocol_Widgets())
    {
        return false;
    }

    for (channel_index = 0U; channel_index < WAVE_CHANNEL_COUNT; channel_index++)
    {
        Wave_Channel_t *channel = &wave.channels[channel_index];

        if (channel->config.enabled)
        {
            Wave_Assign_Display_Slot(
                channel,
                display_slot + (wave.protocol_layout_active ? WAVE_PROTOCOL_RESULT_ROWS : 0U));
            display_slot++;

            if (!Wave_Create_Channel_Widgets(channel))
            {
                return false;
            }
        }
    }

    wave.configuration_applied = true;
    if (had_configuration)
    {
        /* 两个后续帧分别清理当前前、后波形缓冲，防止关闭通道后残留 */
        wave.clear_frames_remaining = 2U;
    }

    /* Layer 1 由独立波形任务在下一次帧信号到来时重建 */
    wave.render_requested = true;

    /* 一次性清除旧控件残留；后续正常刷新只失效波形对象 */
    lv_obj_invalidate(wave.left_root);
    lv_obj_invalidate(wave.wave_root);
    return true;
}

bool Wave_Apply_Protocol_Layout(const char *protocol_name,
                                const char *const roles[WAVE_CHANNEL_COUNT],
                                const bool result_channels[WAVE_CHANNEL_COUNT])
{
    uint32_t index;
    uint32_t visible_count = 0U;

    if (protocol_name == NULL || protocol_name[0] == '\0' || roles == NULL ||
        result_channels == NULL || wave.state != WAVE_STATE_CONFIGURED)
    {
        return false;
    }

    /* 协议结果固定占两行，先校验轨道数量，避免失败时部分改写原配置 */
    for (index = 0U; index < WAVE_CHANNEL_COUNT; index++)
    {
        if (roles[index] != NULL && roles[index][0] != '\0')
        {
            visible_count++;
        }
    }
    if (visible_count == 0U || visible_count > WAVE_CHANNEL_COUNT - WAVE_PROTOCOL_RESULT_ROWS)
    {
        return false;
    }

    for (index = 0U; index < WAVE_CHANNEL_COUNT; index++)
    {
        Wave_Channel_Config_t *config = &wave.channels[index].config;
        const char *role = roles[index];

        wave.protocol_result_channels[index] = result_channels[index];
        config->enabled = role != NULL && role[0] != '\0';
        config->detail_visible = config->enabled;
        if (config->enabled)
        {
            (void)lv_snprintf(config->detail_text, sizeof(config->detail_text), "%s", role);
        }
        else
        {
            config->detail_text[0] = '\0';
        }
    }

    (void)lv_snprintf(wave.protocol_name, sizeof(wave.protocol_name), "%s", protocol_name);
    wave.protocol_layout_active = true;
    return Wave_Apply_Configuration();
}

bool Wave_Clear_Protocol_Layout(void)
{
    uint32_t index;

    if (wave.state != WAVE_STATE_CONFIGURED)
    {
        return false;
    }

    wave.protocol_layout_active = false;
    wave.protocol_name[0] = '\0';
    memset(wave.protocol_result_channels, 0, sizeof(wave.protocol_result_channels));
    for (index = 0U; index < WAVE_CHANNEL_COUNT; index++)
    {
        Wave_Channel_Config_t *config = &wave.channels[index].config;
        config->enabled = true;
        config->detail_visible = false;
        config->detail_text[0] = '\0';
    }
    return Wave_Apply_Configuration();
}

bool Wave_Protocol_Layout_Is_Active(void)
{
    return wave.protocol_layout_active;
}

bool Wave_Protocol_Set_Display_Format(Wave_Protocol_Display_Format_t format)
{
    if ((uint32_t)format > (uint32_t)WAVE_PROTOCOL_DISPLAY_ASCII)
    {
        return false;
    }

    taskENTER_CRITICAL();
    if (wave.state == WAVE_STATE_UNINITIALIZED)
    {
        taskEXIT_CRITICAL();
        return false;
    }
    if (wave.protocol_display_format != format)
    {
        wave.protocol_display_format = format;
        wave.render_requested = true;
    }
    taskEXIT_CRITICAL();
    return true;
}

void Wave_Protocol_Results_Clear(void)
{
    taskENTER_CRITICAL();
    wave.protocol_result_count = 0U;
    wave.render_requested = true;
    taskEXIT_CRITICAL();
}

bool Wave_Protocol_Result_Add(uint8_t row_index,
                              uint32_t start_cnt,
                              uint32_t end_cnt,
                              uint32_t value,
                              uint8_t digits)
{
    Wave_Protocol_Result_t *record;

    if (!wave.protocol_layout_active || row_index >= WAVE_PROTOCOL_RESULT_ROWS ||
        end_cnt < start_cnt)
    {
        return false;
    }

    taskENTER_CRITICAL();
    if (wave.protocol_result_count >= WAVE_PROTOCOL_RESULT_LIMIT)
    {
        taskEXIT_CRITICAL();
        return false;
    }
    if (digits == 0U)
    {
        digits = 1U;
    }
    if (digits > 8U)
    {
        digits = 8U;
    }
    record = &wave.protocol_results[wave.protocol_result_count];
    memset(record, 0, sizeof(*record));
    record->row_index = row_index;
    record->start_cnt = start_cnt;
    record->end_cnt = end_cnt;
    record->value = value;
    record->digits = digits;
    wave.protocol_result_count++;
    wave.render_requested = true;
    taskEXIT_CRITICAL();
    return true;
}

bool Wave_Protocol_Compact_Block_Add(uint8_t row_index, uint32_t start_cnt, uint32_t end_cnt)
{
    Wave_Protocol_Result_t *record;

    if (!wave.protocol_layout_active || row_index >= WAVE_PROTOCOL_RESULT_ROWS ||
        end_cnt < start_cnt)
    {
        return false;
    }

    taskENTER_CRITICAL();
    if (wave.protocol_result_count >= WAVE_PROTOCOL_RESULT_LIMIT)
    {
        taskEXIT_CRITICAL();
        return false;
    }
    record = &wave.protocol_results[wave.protocol_result_count];
    memset(record, 0, sizeof(*record));
    record->row_index = row_index;
    record->start_cnt = start_cnt;
    record->end_cnt = end_cnt;
    record->compact_block = true;
    wave.protocol_result_count++;
    wave.render_requested = true;
    taskEXIT_CRITICAL();
    return true;
}

bool Wave_Protocol_IIC_Address_Add(uint8_t row_index,
                                   uint32_t start_cnt,
                                   uint32_t end_cnt,
                                   uint32_t address,
                                   uint8_t address_bits,
                                   bool read)
{
    Wave_Protocol_Result_t *record;

    if (!wave.protocol_layout_active || row_index >= WAVE_PROTOCOL_RESULT_ROWS ||
        end_cnt < start_cnt || (address_bits != 7U && address_bits != 10U))
    {
        return false;
    }

    taskENTER_CRITICAL();
    if (wave.protocol_result_count >= WAVE_PROTOCOL_RESULT_LIMIT)
    {
        taskEXIT_CRITICAL();
        return false;
    }
    record = &wave.protocol_results[wave.protocol_result_count];
    memset(record, 0, sizeof(*record));
    record->row_index = row_index;
    record->start_cnt = start_cnt;
    record->end_cnt = end_cnt;
    record->value = address;
    record->digits = address_bits == 7U ? 2U : 3U;
    record->prefix = read ? 'R' : 'W';
    record->iic_address = true;
    wave.protocol_result_count++;
    wave.render_requested = true;
    taskEXIT_CRITICAL();
    return true;
}

bool Wave_Protocol_IIC_Data_Add(uint8_t row_index,
                                uint32_t start_cnt,
                                uint32_t end_cnt,
                                uint32_t value,
                                bool read)
{
    Wave_Protocol_Result_t *record;

    if (!wave.protocol_layout_active || row_index >= WAVE_PROTOCOL_RESULT_ROWS ||
        end_cnt < start_cnt)
    {
        return false;
    }

    taskENTER_CRITICAL();
    if (wave.protocol_result_count >= WAVE_PROTOCOL_RESULT_LIMIT)
    {
        taskEXIT_CRITICAL();
        return false;
    }
    record = &wave.protocol_results[wave.protocol_result_count];
    memset(record, 0, sizeof(*record));
    record->row_index = row_index;
    record->start_cnt = start_cnt;
    record->end_cnt = end_cnt;
    record->value = value;
    record->digits = 2U;
    record->iic_data = true;
    record->iic_read_data = read;
    wave.protocol_result_count++;
    wave.render_requested = true;
    taskEXIT_CRITICAL();
    return true;
}

bool Wave_Protocol_UART_Result_Add(uint8_t row_index,
                                   uint32_t start_cnt,
                                   uint32_t end_cnt,
                                   uint32_t value,
                                   uint8_t data_bits,
                                   bool parity_enabled,
                                   uint8_t stop_bits)
{
    Wave_Protocol_Result_t *record;

    if (!wave.protocol_layout_active || row_index >= WAVE_PROTOCOL_RESULT_ROWS ||
        end_cnt < start_cnt || data_bits < 5U || data_bits > 9U || stop_bits < 1U || stop_bits > 2U)
    {
        return false;
    }

    taskENTER_CRITICAL();
    if (wave.protocol_result_count >= WAVE_PROTOCOL_RESULT_LIMIT)
    {
        taskEXIT_CRITICAL();
        return false;
    }
    record = &wave.protocol_results[wave.protocol_result_count];
    memset(record, 0, sizeof(*record));
    record->row_index = row_index;
    record->start_cnt = start_cnt;
    record->end_cnt = end_cnt;
    record->value = value;
    record->digits = (uint8_t)((data_bits + 3U) / 4U);
    record->uart_data_bits = data_bits;
    record->uart_stop_bits = stop_bits;
    record->uart_frame = true;
    record->uart_parity_enabled = parity_enabled;
    wave.protocol_result_count++;
    wave.render_requested = true;
    taskEXIT_CRITICAL();
    return true;
}

bool Wave_Start_Capture(uint32_t timer_start_cnt)
{
    if (wave.state != WAVE_STATE_CONFIGURED || !wave.configuration_applied)
    {
        return false;
    }

    /* 旧数据仍被其他任务读取或保留时，继续显示原画面并等待重试 */
    if (!Wave_Capture_Data_Begin(wave.capture_timestamp_hz,
                                 wave.capture_duration_us,
                                 wave.capture_filter))
    {
        return false;
    }

    wave.capture_timer_start_cnt = timer_start_cnt;
    wave.view.start_cnt = 0U;
    wave.view.now_cnt = 0U;
    wave.view.window_size = wave.live_window_size;
    wave.captured_end_cnt = 0U;
    wave.pending_view_start_cnt = 0U;
    wave.pending_window_size = wave.live_window_size;
    wave.follow_live = true;
    wave.view_dirty = false;
    if (wave.time_ruler != NULL)
    {
        lv_obj_invalidate(wave.time_ruler);
    }

    wave.last_live_range_end_cnt = 0U;
    wave.state = WAVE_STATE_CAPTURING;
    /* 即使本轮在首个边沿前报错，也不能保留上一轮数据形成的旧画面 */
    wave.clear_frames_remaining = 2U;
    wave.render_requested = true;

    /* 开始新一轮采集时只请求波形重建，左侧配置无需重画 */
    lv_obj_invalidate(wave.wave_root);
    return true;
}

bool Wave_Complete_Capture(void)
{
    if (wave.state != WAVE_STATE_CAPTURING || wave.capture_duration_cnt == 0U)
    {
        return false;
    }

    return Wave_Finish_Capture(Wave_Capture_Data_Get_Now_Cnt());
}

bool Wave_Use_Loaded_Capture(uint32_t timestamp_hz, uint32_t end_cnt)
{
    uint64_t duration_us;

    if (timestamp_hz == 0U || wave.state != WAVE_STATE_CONFIGURED || !wave.configuration_applied)
    {
        return false;
    }
    duration_us = ((uint64_t)end_cnt * 1000000ULL + timestamp_hz - 1U) / timestamp_hz;
    if (duration_us == 0U)
    {
        duration_us = 1U;
    }
    if (duration_us > UINT32_MAX)
    {
        duration_us = UINT32_MAX;
    }

    wave.capture_timestamp_hz = timestamp_hz;
    wave.capture_duration_us = (uint32_t)duration_us;
    wave.capture_duration_cnt = end_cnt == 0U ? 1U : end_cnt;
    wave.live_window_size = timestamp_hz;
    wave.captured_end_cnt = end_cnt;
    wave.view.start_cnt = 0U;
    wave.view.now_cnt = end_cnt;
    wave.view.window_size = end_cnt == 0U ? timestamp_hz : end_cnt;
    wave.pending_view_start_cnt = 0U;
    wave.pending_window_size = wave.view.window_size;
    wave.follow_live = false;
    wave.view_dirty = false;
    wave.last_live_range_end_cnt = end_cnt;
    Wave_Capture_Data_Use_Loaded(timestamp_hz, end_cnt);
    wave.protocol_result_count = 0U;
    wave.clear_frames_remaining = 2U;
    wave.render_requested = true;
    if (wave.time_ruler != NULL)
    {
        lv_obj_invalidate(wave.time_ruler);
    }
    (void)BSP_LTDC_LCD_Overlay_Layer_Set_Visible(true);
    return true;
}

bool Wave_Exit_Loaded_Capture(void)
{
    if (wave.state != WAVE_STATE_CONFIGURED || !wave.configuration_applied)
    {
        return false;
    }

    wave.view.start_cnt = 0U;
    wave.view.now_cnt = 0U;
    wave.view.window_size = wave.live_window_size;
    wave.captured_end_cnt = 0U;
    wave.pending_view_start_cnt = 0U;
    wave.pending_window_size = wave.live_window_size;
    wave.follow_live = false;
    wave.view_dirty = false;
    wave.last_live_range_end_cnt = 0U;
    wave.protocol_result_count = 0U;
    wave.clear_frames_remaining = 2U;
    wave.render_requested = true;
    if (wave.time_ruler != NULL)
    {
        lv_obj_invalidate(wave.time_ruler);
    }
    if (wave.wave_root != NULL)
    {
        lv_obj_invalidate(wave.wave_root);
    }
    return true;
}

bool Wave_Pause_Capture(void)
{
    uint32_t pause_cnt;

    if (wave.state != WAVE_STATE_CAPTURING)
    {
        return false;
    }

    pause_cnt = Wave_Capture_Data_Get_Now_Cnt();
    return Wave_Finish_Capture(pause_cnt);
}

/* ================================================== */

/**
 * @brief  停止真实数据源并把实际截止范围映射为完整总览
 * @note   停止请求可能仍在后台收尾
 */
static bool Wave_Finish_Capture(uint32_t end_cnt)
{
    if (wave.state != WAVE_STATE_CAPTURING)
    {
        return false;
    }

    if (end_cnt > wave.capture_duration_cnt)
    {
        end_cnt = wave.capture_duration_cnt;
    }

    /* stop() 会锁存当前 CNT，之后范围申请只暴露这一时刻之前的数据 */
    if (!Wave_Capture_Data_Stop())
    {
        return false;
    }
    wave.captured_end_cnt = end_cnt;
    wave.view.start_cnt = 0U;
    wave.view.now_cnt = end_cnt;
    wave.view.window_size = (end_cnt == 0U) ? wave.live_window_size : end_cnt;
    wave.pending_view_start_cnt = 0U;
    wave.pending_window_size = wave.view.window_size;
    wave.follow_live = false;
    wave.view_dirty = false;
    wave.last_live_range_end_cnt = end_cnt;

    if (wave.time_ruler != NULL)
    {
        lv_obj_invalidate(wave.time_ruler);
    }
    wave.render_requested = true;
    wave.state = WAVE_STATE_CONFIGURED;
    return true;
}

/* ================================================== */

void Wave_Stop_Capture(void)
{
    if (wave.state == WAVE_STATE_CAPTURING)
    {
        if (Wave_Capture_Data_Stop())
        {
            wave.state = WAVE_STATE_CONFIGURED;
        }
    }
}

void Wave_Cancel_Capture(void)
{
    if (wave.state != WAVE_STATE_CAPTURING)
    {
        return;
    }

    if (!Wave_Capture_Data_Stop())
    {
        return;
    }
    wave.view.start_cnt = 0U;
    wave.view.now_cnt = 0U;
    wave.view.window_size = wave.live_window_size;
    wave.captured_end_cnt = 0U;
    wave.pending_view_start_cnt = 0U;
    wave.pending_window_size = wave.live_window_size;
    wave.follow_live = false;
    wave.view_dirty = false;
    wave.clear_frames_remaining = 2U;
    wave.state = WAVE_STATE_CONFIGURED;

    wave.render_requested = true;

    if (wave.wave_root != NULL)
    {
        lv_obj_invalidate(wave.wave_root);
    }
}

bool Wave_Is_Capturing(void)
{
    return wave.state == WAVE_STATE_CAPTURING;
}

bool Wave_Get_Capture_Progress(uint32_t *elapsed_cnt, bool *finished)
{
    return Wave_Is_Capturing() && Wave_Capture_Data_Get_Progress(elapsed_cnt, finished);
}

bool Wave_Set_Capture_Duration(uint32_t duration_us)
{
    return Wave_Set_Capture_Parameters(wave.capture_timestamp_hz, duration_us);
}

uint32_t Wave_Get_Capture_Duration(void)
{
    return wave.capture_duration_cnt;
}

bool Wave_Set_Capture_Parameters(uint32_t timestamp_hz, uint32_t duration_us)
{
    const uint64_t ticks = ((uint64_t)timestamp_hz * duration_us) / 1000000ULL;

    if (timestamp_hz == 0U || duration_us == 0U || ticks == 0U || ticks > UINT32_MAX ||
        wave.state != WAVE_STATE_CONFIGURED)
    {
        return false;
    }

    wave.capture_timestamp_hz = timestamp_hz;
    wave.capture_duration_us = duration_us;
    wave.capture_duration_cnt = (uint32_t)ticks;
    wave.live_window_size = timestamp_hz; /* 采集中默认显示1秒 */
    wave.captured_end_cnt = 0U;
    wave.protocol_result_count = 0U;
    wave.view.start_cnt = 0U;
    wave.view.now_cnt = 0U;
    wave.view.window_size = timestamp_hz;
    wave.pending_view_start_cnt = 0U;
    wave.pending_window_size = timestamp_hz;
    wave.clear_frames_remaining = 2U;
    wave.render_requested = true;
    if (wave.time_ruler != NULL)
    {
        lv_obj_invalidate(wave.time_ruler);
    }
    return true;
}

uint32_t Wave_Get_Capture_Timestamp_Hz(void)
{
    return wave.capture_timestamp_hz;
}

uint32_t Wave_Get_Capture_Duration_Us(void)
{
    return wave.capture_duration_us;
}

bool Wave_Set_Capture_Filter(uint8_t input_filter)
{
    if (input_filter > 15U || !Wave_Capture_Filter_Is_Configurable())
    {
        return false;
    }
    wave.capture_filter = input_filter;
    return true;
}

uint8_t Wave_Get_Capture_Filter(void)
{
    return wave.capture_filter;
}

bool Wave_Capture_Filter_Is_Configurable(void)
{
    return wave.state == WAVE_STATE_CONFIGURED && Wave_Capture_Data_Can_Configure();
}

void Wave_Set_Window_Size(uint32_t window_cnt)
{
    if (window_cnt != 0U)
    {
        wave.live_window_size = window_cnt;
        if (wave.state != WAVE_STATE_CAPTURING)
        {
            wave.view.window_size = window_cnt;
            wave.render_requested = true;
        }
    }
}

void Wave_Pan_View_Pixels(int32_t delta_x_pixels)
{
    uint32_t available_end_cnt;
    uint32_t base_start_cnt;
    uint32_t base_window_cnt;
    int64_t delta_cnt;
    int64_t requested_start_cnt;

    if (wave.state == WAVE_STATE_UNINITIALIZED || !wave.configuration_applied ||
        wave.view.window_size == 0U || delta_x_pixels == 0)
    {
        return;
    }

    available_end_cnt = (wave.state == WAVE_STATE_CAPTURING) ? Wave_Capture_Data_Get_Now_Cnt() :
                                                               wave.captured_end_cnt;
    base_window_cnt = wave.view_dirty ? wave.pending_window_size : wave.view.window_size;
    if (available_end_cnt <= base_window_cnt)
    {
        return;
    }

    delta_cnt = ((int64_t)delta_x_pixels * base_window_cnt) / WAVE_AREA_WIDTH;
    base_start_cnt = wave.view_dirty ? wave.pending_view_start_cnt : wave.view.start_cnt;
    requested_start_cnt = (int64_t)base_start_cnt - delta_cnt;
    if (requested_start_cnt < 0)
    {
        requested_start_cnt = 0;
    }

    wave.pending_view_start_cnt =
        Wave_Clamp_View_Start((uint32_t)requested_start_cnt, base_window_cnt, available_end_cnt);
    if (wave.pending_view_start_cnt == base_start_cnt)
    {
        return;
    }

    /* 用户开始浏览历史，采集刷新不能再把窗口拉回最新位置 */
    wave.follow_live = false;
    wave.view_dirty = true;
    wave.render_requested = true;
}

void Wave_Zoom_View_At(int32_t anchor_x_pixels, bool zoom_in)
{
    uint32_t available_end_cnt;
    uint32_t base_start_cnt;
    uint32_t base_window_cnt;
    uint32_t new_window_cnt;
    uint32_t anchor_offset_pixels;
    uint32_t anchor_cnt;
    int64_t requested_start_cnt;

    if (wave.state == WAVE_STATE_UNINITIALIZED || !wave.configuration_applied)
    {
        return;
    }

    available_end_cnt = (wave.state == WAVE_STATE_CAPTURING) ? Wave_Capture_Data_Get_Now_Cnt() :
                                                               wave.captured_end_cnt;
    if (available_end_cnt == 0U)
    {
        return;
    }

    base_start_cnt = wave.view_dirty ? wave.pending_view_start_cnt : wave.view.start_cnt;
    base_window_cnt = wave.view_dirty ? wave.pending_window_size : wave.view.window_size;
    new_window_cnt = Wave_Get_Zoom_Window(base_window_cnt, available_end_cnt, zoom_in);
    if (new_window_cnt == base_window_cnt)
    {
        return;
    }

    if (anchor_x_pixels <= WAVE_AREA_X)
    {
        anchor_offset_pixels = 0U;
    }
    else if (anchor_x_pixels >= WAVE_AREA_X_END)
    {
        anchor_offset_pixels = WAVE_AREA_WIDTH;
    }
    else
    {
        anchor_offset_pixels = (uint32_t)(anchor_x_pixels - WAVE_AREA_X);
    }

    anchor_cnt = base_start_cnt +
                 (uint32_t)(((uint64_t)base_window_cnt * anchor_offset_pixels) / WAVE_AREA_WIDTH);
    requested_start_cnt =
        (int64_t)anchor_cnt -
        (int64_t)(((uint64_t)new_window_cnt * anchor_offset_pixels) / WAVE_AREA_WIDTH);
    if (requested_start_cnt < 0)
    {
        requested_start_cnt = 0;
    }

    wave.pending_window_size = new_window_cnt;
    wave.pending_view_start_cnt =
        Wave_Clamp_View_Start((uint32_t)requested_start_cnt, new_window_cnt, available_end_cnt);
    wave.follow_live = false;
    wave.view_dirty = true;
    wave.render_requested = true;
}

void Wave_Return_To_Live(void)
{
    if (wave.state != WAVE_STATE_CAPTURING)
    {
        return;
    }

    wave.follow_live = true;
    wave.view_dirty = false;
    wave.render_requested = true;
}

bool Wave_Get_Live_Range(Wave_Cnt_Range_t *range)
{
    const uint32_t now_cnt = Wave_Capture_Data_Get_Now_Cnt();

    if (range == NULL || wave.state != WAVE_STATE_CAPTURING)
    {
        return false;
    }

    range->cnt_start = wave.last_live_range_end_cnt;
    range->cnt_end = now_cnt;
    wave.last_live_range_end_cnt = now_cnt;
    return true;
}

bool Wave_Get_View_Range(Wave_Cnt_Range_t *range)
{
    if (range == NULL || wave.state == WAVE_STATE_UNINITIALIZED)
    {
        return false;
    }

    range->cnt_start = wave.view.start_cnt;
    range->cnt_end = wave.view.now_cnt;
    return true;
}

uint32_t Wave_Get_Refresh_Count(void)
{
    return BSP_LTDC_LCD_Overlay_Layer_Get_Present_Count();
}

/* 初始化和配置 */

/* ================================================== */

/**
 * @brief  生成一路默认配置并清空其布局和渲染状态
 */
static void Wave_Init_Default_Config(Wave_Channel_t *channel, uint32_t channel_index)
{
    Wave_Channel_Config_t *config = &channel->config;

    memset(channel, 0, sizeof(*channel));

    channel->source_id = (uint8_t)channel_index;
    config->enabled = true;
    config->color = wave_default_colors[channel_index];

    (void)lv_snprintf(config->pin_text,
                      sizeof(config->pin_text),
                      "D%lu",
                      (unsigned long)(channel_index + 1U));
    (void)lv_snprintf(config->name_text,
                      sizeof(config->name_text),
                      "Channel %lu",
                      (unsigned long)(channel_index + 1U));
    config->detail_visible = false;
    config->detail_text[0] = '\0';
    config->separator_visible = true;
}

/**
 * @brief  按固定六槽布局计算一路通道的高低电平位置
 */
static void Wave_Assign_Display_Slot(Wave_Channel_t *channel, uint32_t display_slot)
{
    const int32_t usable_height = wave.display_height - WAVE_TIME_RULER_HEIGHT;
    const int32_t row_height = usable_height / (int32_t)WAVE_CHANNEL_COUNT;
    Wave_Channel_Layout_t *layout = &channel->layout;

    layout->display_slot = (uint8_t)display_slot;
    layout->top = WAVE_TIME_RULER_HEIGHT + (int16_t)((int32_t)display_slot * row_height);
    layout->height = (display_slot == (WAVE_CHANNEL_COUNT - 1U)) ?
                         (int16_t)(wave.display_height - layout->top) :
                         (int16_t)row_height;
    /* 条带容量固定为 26 行；换面板分辨率时只居中，不扩大高低电平间距 */
    layout->high_offset = (int16_t)((layout->height - WAVE_DMA_TILE_LINES) / 2);
    layout->low_offset = (int16_t)(layout->high_offset + WAVE_DMA_TILE_LINES - 1U);
}

/**
 * @brief  创建左侧信息区和波形标尺区两个长期根容器
 */
static bool Wave_Create_Roots(lv_obj_t *parent, int32_t display_height)
{
    wave.left_root = lv_obj_create(parent);
    if (wave.left_root == NULL)
    {
        return false;
    }

    wave.wave_root = lv_obj_create(parent);
    if (wave.wave_root == NULL)
    {
        lv_obj_delete(wave.left_root);
        wave.left_root = NULL;
        return false;
    }

    /* 左侧静态区先创建，波形根容器后创建并位于更高层；两者不重叠 */
    lv_obj_remove_style_all(wave.left_root);
    lv_obj_set_pos(wave.left_root, WAVE_LEFT_X, 0);
    lv_obj_set_size(wave.left_root, WAVE_LEFT_WIDTH, display_height);
    lv_obj_set_style_bg_color(wave.left_root, lv_color_hex(WAVE_BACKGROUND_COLOR), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(wave.left_root, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_scrollable(wave.left_root, false);

    lv_obj_remove_style_all(wave.wave_root);
    lv_obj_set_pos(wave.wave_root, WAVE_AREA_X, 0);
    lv_obj_set_size(wave.wave_root, WAVE_AREA_WIDTH, display_height);
    lv_obj_set_style_bg_color(wave.wave_root, lv_color_hex(WAVE_BACKGROUND_COLOR), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(wave.wave_root, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_scrollable(wave.wave_root, false);
    return true;
}

/**
 * @brief  在父容器清理后清空一路失效的 LVGL 对象引用
 * @note   先删除控件，再复位失效句柄
 */
static void Wave_Reset_Render_Handles(Wave_Channel_t *channel)
{
    memset(&channel->render, 0, sizeof(channel->render));
}

/**
 * @brief  创建覆盖波形顶端的时间标尺自绘对象
 */
static bool Wave_Create_Time_Ruler(void)
{
    wave.time_ruler = lv_obj_create(wave.wave_root);
    if (wave.time_ruler == NULL)
    {
        return false;
    }

    lv_obj_remove_style_all(wave.time_ruler);
    lv_obj_set_pos(wave.time_ruler, 0, 0);
    lv_obj_set_size(wave.time_ruler, WAVE_AREA_WIDTH, WAVE_TIME_RULER_HEIGHT);
    lv_obj_set_style_bg_color(wave.time_ruler, lv_color_hex(WAVE_BACKGROUND_COLOR), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(wave.time_ruler, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_scrollable(wave.time_ruler, false);
    lv_obj_add_event_cb(wave.time_ruler, Wave_Draw_Time_Ruler_Callback, LV_EVENT_DRAW_MAIN, NULL);
    return true;
}

/**
 * @brief  创建协议名称、删除按钮和顶部两行结果提示
 */
static bool Wave_Create_Protocol_Widgets(void)
{
    static const uint32_t text_color = 0xE6EDF7U;
    static const uint32_t accent_color = 0x3765F5U;
    const char *first = NULL;
    const char *second = NULL;
    uint32_t index;

    for (index = 0U; index < WAVE_CHANNEL_COUNT; index++)
    {
        const char *role = wave.channels[index].config.detail_text;
        if (!wave.channels[index].config.enabled || !wave.protocol_result_channels[index])
        {
            continue;
        }
        /* 只排列数据通道；时钟和片选只在下方原始波形中显示 */
        if (first == NULL)
        {
            first = role;
        }
        else if (second == NULL)
        {
            second = role;
        }
    }

    wave.protocol_title = lv_button_create(wave.left_root);
    if (wave.protocol_title == NULL)
    {
        return false;
    }
    wave.protocol_delete_button = wave.protocol_title;
    lv_obj_set_pos(wave.protocol_title, 5, (WAVE_TIME_RULER_HEIGHT - 32) / 2);
    lv_obj_set_size(wave.protocol_title, WAVE_LEFT_WIDTH - 10, 32);
    lv_obj_set_style_bg_color(wave.protocol_title, lv_color_hex(accent_color), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(wave.protocol_title, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(wave.protocol_title, 9, LV_PART_MAIN);
    lv_obj_set_style_border_width(wave.protocol_title, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(wave.protocol_title, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(wave.protocol_title, 0, LV_PART_MAIN);
    lv_obj_set_scrollable(wave.protocol_title, false);
    lv_obj_add_event_cb(wave.protocol_title,
                        Wave_Protocol_Delete_Event_Callback,
                        LV_EVENT_CLICKED,
                        NULL);
    {
        lv_obj_t *name = lv_label_create(wave.protocol_title);
        lv_obj_t *close = lv_label_create(wave.protocol_title);
        if (name == NULL || close == NULL)
        {
            return false;
        }
        lv_label_set_text(name, wave.protocol_name);
        lv_obj_set_pos(name, 10, 8);
        lv_obj_set_style_text_color(name, lv_color_hex(0xFFFFFFU), LV_PART_MAIN);
        lv_label_set_text(close, LV_SYMBOL_CLOSE);
        lv_obj_align(close, LV_ALIGN_RIGHT_MID, -8, 0);
        lv_obj_set_style_text_color(close, lv_color_hex(0xFFFFFFU), LV_PART_MAIN);
    }

    for (index = 0U; index < WAVE_PROTOCOL_RESULT_ROWS; index++)
    {
        const char *role = index == 0U ? first : second;
        lv_obj_t *label = lv_label_create(wave.left_root);
        int32_t result_center_y;
        int32_t text_height;
        if (label == NULL)
        {
            return false;
        }
        wave.protocol_result[index] = label;
        /* 与纯栅格模块共用纵向布局，字体或面板高度改变也不会偏离解析色块 */
        result_center_y =
            WAVE_TIME_RULER_HEIGHT + WAVE_PROTOCOL_FIRST_ROW_Y + WAVE_PROTOCOL_BLOCK_HEIGHT / 2 +
            (int32_t)index *
                ((wave.display_height - WAVE_TIME_RULER_HEIGHT) / (int32_t)WAVE_CHANNEL_COUNT);
        text_height = lv_font_get_line_height(lv_obj_get_style_text_font(label, LV_PART_MAIN));
        lv_obj_set_pos(label, 4, result_center_y - text_height / 2);
        lv_obj_set_width(label, WAVE_LEFT_WIDTH - 8);
        lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
        lv_obj_set_style_text_color(label, lv_color_hex(text_color), LV_PART_MAIN);
        if (role == NULL)
        {
            lv_label_set_text(label, "");
        }
        else
        {
            /* 角色留在左侧静态栏，右侧独立硬件层绘制协议色块，不互相覆盖 */
            lv_label_set_text(label, role);
        }
    }
    wave.protocol_result_count = 0U;
    return true;
}

/**
 * @brief  异步安排退出协议布局以避免删除当前事件的祖先
 * @note   当前事件退出后再重建布局
 */
static void Wave_Protocol_Delete_Event_Callback(lv_event_t *event)
{
    (void)event;
    (void)lv_async_call(Wave_Protocol_Delete_Async_Callback, NULL);
}

/**
 * @brief  在事件处理结束后恢复普通六路波形布局
 */
static void Wave_Protocol_Delete_Async_Callback(void *context)
{
    (void)context;
    (void)Wave_Clear_Protocol_Layout();
}

/**
 * @brief  按通道配置创建一路左侧静态信息行
 */
static bool Wave_Create_Channel_Widgets(Wave_Channel_t *channel)
{
    GUI_Channel_Config_t config = {.pin_text = channel->config.pin_text,
                                   .name_text = channel->config.name_text,
                                   .detail_text = channel->config.detail_text,
                                   .color = channel->config.color,
                                   .detail_visible = channel->config.detail_visible,
                                   .separator_visible = channel->config.separator_visible};

    return GUI_Channel_Create(wave.left_root,
                              channel->layout.top,
                              channel->layout.height,
                              &config,
                              &channel->render);
}

/**
 * @brief  把候选窗口左边界限制在当前数据范围内
 */
static uint32_t Wave_Clamp_View_Start(uint32_t requested_start_cnt,
                                      uint32_t window_cnt,
                                      uint32_t available_end_cnt)
{
    uint32_t max_start_cnt;

    if (available_end_cnt <= window_cnt)
    {
        return 0U;
    }

    max_start_cnt = available_end_cnt - window_cnt;
    return (requested_start_cnt > max_start_cnt) ? max_start_cnt : requested_start_cnt;
}

/**
 * @brief  选择相邻的 1/2/5 时间档位并限制在完整采集范围内
 */
static uint32_t Wave_Get_Zoom_Window(uint32_t current_window_cnt,
                                     uint32_t max_window_cnt,
                                     bool zoom_in)
{
    static const uint32_t zoom_windows_us[] = {1U,
                                               2U,
                                               5U,
                                               10U,
                                               20U,
                                               50U,
                                               100U,
                                               200U,
                                               500U,
                                               1000U,
                                               2000U,
                                               5000U,
                                               10000U,
                                               20000U,
                                               50000U,
                                               100000U,
                                               200000U,
                                               500000U,
                                               1000000U,
                                               2000000U,
                                               5000000U,
                                               10000000U,
                                               20000000U,
                                               50000000U,
                                               60000000U,
                                               120000000U,
                                               300000000U};
    const uint32_t min_window_cnt = Wave_Us_To_Cnt(1U);
    uint32_t index;

    if (max_window_cnt < min_window_cnt)
    {
        return max_window_cnt;
    }

    if (zoom_in)
    {
        for (index = sizeof(zoom_windows_us) / sizeof(zoom_windows_us[0]); index > 0U; index--)
        {
            const uint32_t candidate_cnt = Wave_Us_To_Cnt(zoom_windows_us[index - 1U]);

            if (candidate_cnt < current_window_cnt)
            {
                return (candidate_cnt > max_window_cnt) ? max_window_cnt : candidate_cnt;
            }
        }
        return min_window_cnt;
    }

    for (index = 0U; index < (sizeof(zoom_windows_us) / sizeof(zoom_windows_us[0])); index++)
    {
        const uint32_t candidate_cnt = Wave_Us_To_Cnt(zoom_windows_us[index]);
        if (candidate_cnt > current_window_cnt)
        {
            return (candidate_cnt > max_window_cnt) ? max_window_cnt : candidate_cnt;
        }
    }

    return max_window_cnt;
}

/**
 * @brief  按当前时间戳频率把微秒时长换算为 CNT
 */
static uint32_t Wave_Us_To_Cnt(uint32_t duration_us)
{
    const uint64_t cnt = ((uint64_t)wave.capture_timestamp_hz * duration_us) / 1000000ULL;
    if (duration_us != 0U && cnt == 0U)
    {
        return 1U;
    }
    return cnt > UINT32_MAX ? UINT32_MAX : (uint32_t)cnt;
}

/**
 * @brief  按当前时间戳频率把 CNT 换算为微秒
 */
static uint32_t Wave_Cnt_To_Us(uint32_t cnt)
{
    if (wave.capture_timestamp_hz == 0U)
    {
        return 0U;
    }
    return (uint32_t)(((uint64_t)cnt * 1000000ULL) / wave.capture_timestamp_hz);
}

/**
 * @brief  按实时截止 CNT 更新跟随窗口的左右边界
 * @note   调用者持有波形状态临界区
 */
static void Wave_Update_View(uint32_t relative_now_cnt)
{
    wave.view.now_cnt = relative_now_cnt;
    wave.view.start_cnt =
        (relative_now_cnt > wave.view.window_size) ? relative_now_cnt - wave.view.window_size : 0U;
}

/**
 * @brief  把相对 CNT 线性映射到波形区域的屏幕横坐标
 */
static int32_t Wave_Cnt_To_X(uint32_t cnt, const Wave_View_t *view)
{
    uint64_t pixel;

    if (view->window_size == 0U || cnt <= view->start_cnt)
    {
        return WAVE_AREA_X;
    }

    pixel = ((uint64_t)(cnt - view->start_cnt) * WAVE_AREA_WIDTH) / view->window_size;
    if (pixel >= WAVE_AREA_WIDTH)
    {
        return WAVE_AREA_X_END;
    }

    return WAVE_AREA_X + (int32_t)pixel;
}

/* Layer 0 时间标尺 */

/**
 * @brief  在 GUI 绘制事件中生成主刻度和时间文字
 */
static void Wave_Draw_Time_Ruler_Callback(lv_event_t *event)
{
    Wave_View_t view;
    uint32_t tick_cnt;
    uint64_t first_tick_cnt;
    lv_draw_line_dsc_t tick_dsc;
    lv_draw_label_dsc_t label_dsc;
    lv_area_t coords;
    uint64_t current_tick_cnt;
    uint32_t label_index = 0U;
    int32_t last_label_right = -32768;

    taskENTER_CRITICAL();
    view = wave.view;
    taskEXIT_CRITICAL();

    tick_cnt = Wave_Get_Time_Tick_Cnt(view.window_size);
    first_tick_cnt = (((uint64_t)view.start_cnt + tick_cnt - 1U) / tick_cnt) * tick_cnt;
    if (first_tick_cnt > view.now_cnt)
    {
        return;
    }

    lv_obj_get_content_coords(lv_event_get_current_target(event), &coords);

    lv_draw_line_dsc_init(&tick_dsc);
    tick_dsc.color = lv_color_hex(WAVE_TIME_TICK_COLOR);
    tick_dsc.width = 1U;
    tick_dsc.opa = LV_OPA_COVER;

    lv_draw_label_dsc_init(&label_dsc);
    label_dsc.color = lv_color_hex(WAVE_TIME_TEXT_COLOR);
    label_dsc.opa = LV_OPA_COVER;
    label_dsc.flag = LV_TEXT_FLAG_EXPAND;

    for (current_tick_cnt = first_tick_cnt;
         current_tick_cnt <= view.now_cnt && label_index < WAVE_TIME_LABEL_SLOTS;
         current_tick_cnt += tick_cnt, label_index++)
    {
        const int32_t x = Wave_Cnt_To_X((uint32_t)current_tick_cnt, &view);
        lv_area_t label_coords;
        lv_point_t label_size;

        tick_dsc.p1.x = x;
        tick_dsc.p1.y = coords.y1;
        tick_dsc.p2.x = x;
        tick_dsc.p2.y = coords.y1 + 6;
        lv_draw_line(lv_event_get_layer(event), &tick_dsc);

        Wave_Format_Time_Label(wave.time_labels[label_index],
                               sizeof(wave.time_labels[label_index]),
                               (uint32_t)current_tick_cnt,
                               tick_cnt);
        label_dsc.text = wave.time_labels[label_index];
        lv_text_get_size(&label_size,
                         label_dsc.text,
                         label_dsc.font,
                         label_dsc.letter_space,
                         label_dsc.line_space,
                         LV_COORD_MAX,
                         LV_TEXT_FLAG_EXPAND);
        label_dsc.align = LV_TEXT_ALIGN_LEFT;

        if (x == coords.x1)
        {
            label_coords.x1 = x;
            label_coords.x2 = x + label_size.x - 1;
        }
        else if (x == coords.x2)
        {
            label_coords.x1 = x - label_size.x + 1;
            label_coords.x2 = x;
        }
        else
        {
            label_coords.x1 = x - (label_size.x / 2);
            label_coords.x2 = label_coords.x1 + label_size.x - 1;
        }

        label_coords.y1 = coords.y1 + 9;
        label_coords.y2 = coords.y2;
        /* 数值变长或刻度变密时保留刻度线，只跳过会与前一项重叠的文字 */
        if (label_coords.x1 > last_label_right + 4)
        {
            lv_draw_label(lv_event_get_layer(event), &label_dsc, &label_coords);
            last_label_right = label_coords.x2;
        }
    }
}

/**
 * @brief  根据窗口宽度选择 1/2/5 时间标尺主刻度间隔
 */
static uint32_t Wave_Get_Time_Tick_Cnt(uint32_t window_cnt)
{
    static const uint8_t multipliers[] = {1U, 2U, 5U};
    const uint32_t window_us = Wave_Cnt_To_Us(window_cnt);
    uint32_t best_tick_us = 1U;
    uint32_t decade_us = 1U;
    uint32_t multiplier_index;

    while (decade_us != 0U)
    {
        for (multiplier_index = 0U;
             multiplier_index < (sizeof(multipliers) / sizeof(multipliers[0]));
             multiplier_index++)
        {
            const uint64_t candidate_us = (uint64_t)decade_us * multipliers[multiplier_index];

            if (candidate_us > UINT32_MAX || (candidate_us * WAVE_TIME_TICK_MIN_COUNT) > window_us)
            {
                return Wave_Us_To_Cnt(best_tick_us);
            }

            best_tick_us = (uint32_t)candidate_us;
        }

        if (decade_us > (UINT32_MAX / 10U))
        {
            break;
        }
        decade_us *= 10U;
    }

    return Wave_Us_To_Cnt(best_tick_us);
}

/**
 * @brief  按刻度精度把相对 CNT 格式化为标尺文字
 */
static void Wave_Format_Time_Label(char *buffer,
                                   uint32_t buffer_size,
                                   uint32_t cnt,
                                   uint32_t tick_cnt)
{
    const uint32_t cnt_us = Wave_Cnt_To_Us(cnt);
    const uint32_t tick_us = Wave_Cnt_To_Us(tick_cnt);
    const uint32_t seconds = cnt_us / WAVE_MICROSECONDS_PER_SECOND;
    const uint32_t fraction = cnt_us % WAVE_MICROSECONDS_PER_SECOND;

    if (tick_us < 1000U)
    {
        (void)lv_snprintf(buffer, buffer_size, "%luus", (unsigned long)cnt_us);
    }
    else if ((tick_us % WAVE_MICROSECONDS_PER_SECOND) == 0U)
    {
        (void)lv_snprintf(buffer, buffer_size, "%lus", (unsigned long)seconds);
    }
    else if ((tick_us % 100000U) == 0U)
    {
        (void)lv_snprintf(buffer,
                          buffer_size,
                          "%lu.%lus",
                          (unsigned long)seconds,
                          (unsigned long)(fraction / 100000U));
    }
    else if ((tick_us % 10000U) == 0U)
    {
        (void)lv_snprintf(buffer,
                          buffer_size,
                          "%lu.%02lus",
                          (unsigned long)seconds,
                          (unsigned long)(fraction / 10000U));
    }
    else if ((tick_us % 1000U) == 0U)
    {
        (void)lv_snprintf(buffer,
                          buffer_size,
                          "%lu.%03lus",
                          (unsigned long)seconds,
                          (unsigned long)(fraction / 1000U));
    }
    else if ((tick_us % 100U) == 0U)
    {
        (void)lv_snprintf(buffer,
                          buffer_size,
                          "%lu.%04lus",
                          (unsigned long)seconds,
                          (unsigned long)(fraction / 100U));
    }
    else if ((tick_us % 10U) == 0U)
    {
        (void)lv_snprintf(buffer,
                          buffer_size,
                          "%lu.%05lus",
                          (unsigned long)seconds,
                          (unsigned long)(fraction / 10U));
    }
    else
    {
        (void)lv_snprintf(buffer,
                          buffer_size,
                          "%lu.%06lus",
                          (unsigned long)seconds,
                          (unsigned long)fraction);
    }
}
