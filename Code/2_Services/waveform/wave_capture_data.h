/**
 * @file   wave_capture_data.h
 * @brief  采集数据查询适配
 */

#ifndef WAVE_CAPTURE_DATA_H
#define WAVE_CAPTURE_DATA_H

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================== */

#include <stdbool.h>
#include <stdint.h>

/* ================================================== */

#define WAVE_CAPTURE_DATA_CHANNEL_COUNT 6U
#define WAVE_CAPTURE_DATA_COLUMN_COUNT  500U

/* ================================================== */

/**
 * @brief  清除显示适配层保存的最近截止 CNT
 * @note   初始化显示缓存，不复位 LA 数据
 */
void Wave_Capture_Data_Init(void);

/**
 * @brief  释放已结束的旧采集并启动六路真实硬件采集
 * @param  timestamp_hz : 非零频率，单位 Hz
 * @param  duration_us : 非零时长，单位微秒
 * @note   立即返回；旧采集数据仍被读取或保留时，启动会失败
 */
bool Wave_Capture_Data_Begin(uint32_t timestamp_hz, uint32_t duration_us, uint8_t input_filter);

/**
 * @brief  查询采集服务是否允许保存下一轮采集配置
 */
bool Wave_Capture_Data_Can_Configure(void);

/**
 * @brief  请求停止六路硬件采集并保留已有数据
 * @note   STOPPING 表示硬件还在停止过程中，此时不能保留采集数据
 */
bool Wave_Capture_Data_Stop(void);

/**
 * @brief  读取采集进度和本次采集是否已经结束
 * @param  elapsed_cnt : 实际采集 CNT
 * @param  finished : DONE 或 ERROR 时为 true
 * @note   采集中读取硬件计数；结束后使用最终的 cutoff_tick
 */
bool Wave_Capture_Data_Get_Progress(uint32_t *elapsed_cnt, bool *finished);

/**
 * @brief  返回当前采集进度，或上一次采集结束时的计数
 * @note   单位 CNT，读取失败保留最近值
 */
uint32_t Wave_Capture_Data_Get_Now_Cnt(void);

/**
 * @brief  按当前采集状态读取波形绘制所使用的 CNT
 * @note   帧 tick 只触发刷新，不重建硬件时间戳
 */
uint32_t Wave_Capture_Data_Get_Cnt_At_Tick(uint32_t rtos_tick);

/**
 * @brief  同步文件导入后的显示侧截止 CNT 缓存
 */
void Wave_Capture_Data_Use_Loaded(uint32_t timestamp_hz, uint32_t end_cnt);

/**
 * @brief  把六路边沿按时间分到屏幕的 500 列，供波形绘制使用
 * @param  columns : 500 元素数组，每通道占 2 bit
 * @param  valid_columns : 有效列数 0~500
 * @note   仅准备任务串行调用；每通道 2 bit：00 无翻转，01 低，10 高
 */
bool Wave_Capture_Data_Request_Window(uint32_t s_tick,
                                      uint32_t e_tick,
                                      uint32_t window_tick,
                                      uint16_t *columns,
                                      uint32_t *valid_columns,
                                      uint8_t *initial_levels);

/* ================================================== */

#ifdef __cplusplus
}
#endif

#endif /* WAVE_CAPTURE_DATA_H */
