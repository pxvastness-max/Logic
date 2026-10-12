/**
 * @file   wave_service.h
 * @brief  波形显示与采集数据管理
 */

#ifndef WAVE_SERVICE_H
#define WAVE_SERVICE_H

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================== */

#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>

/* ================================================== */

/* 显示区域 */

#define WAVE_CHANNEL_COUNT 6U

#define WAVE_LEFT_X     0
#define WAVE_LEFT_WIDTH 100

#define WAVE_AREA_X     100
#define WAVE_AREA_WIDTH 500
#define WAVE_AREA_X_END (WAVE_AREA_X + WAVE_AREA_WIDTH - 1)

#define WAVE_TIME_RULER_HEIGHT 36

/* 文本容量 */

#define WAVE_PIN_TEXT_SIZE    4U
#define WAVE_NAME_TEXT_SIZE   16U
#define WAVE_DETAIL_TEXT_SIZE 16U

/* ================================================== */

typedef enum
{
    WAVE_PROTOCOL_DISPLAY_HEX = 0, /* 按协议位宽显示 HEX */
    WAVE_PROTOCOL_DISPLAY_ASCII    /* 可打印值显示字符，其余显示 [HEX] */
} Wave_Protocol_Display_Format_t;

/**
 * @brief  一路物理通道对应的界面配置
 */
typedef struct
{
    bool enabled;   /* 是否创建左侧信息和波形轨道，不改变 LA 六路采集 */
    uint32_t color; /* 徽标和波形共用的 0xRRGGBB 颜色 */

    char pin_text[WAVE_PIN_TEXT_SIZE];   /* 左侧徽标文字，例如 D1 */
    char name_text[WAVE_NAME_TEXT_SIZE]; /* 左侧名称，例如 Channel 1 */

    bool detail_visible;                     /* 是否显示协议名/别名 */
    char detail_text[WAVE_DETAIL_TEXT_SIZE]; /* 协议名/别名，例如 SCL */
    bool separator_visible;                  /* 是否显示底部下划线 */
} Wave_Channel_Config_t;

typedef struct
{
    uint32_t cnt_start; /* 范围左边界，相对本轮采集起点的 CNT */
    uint32_t cnt_end;   /* 范围右边界，相对本轮采集起点的 CNT */
} Wave_Cnt_Range_t;

/* ================================================== */

/**
 * @brief  创建波形准备、条带发送任务和队列、信号量
 * @note   调度器启动前调用，不创建 LVGL 对象
 */
bool Wave_Display_Task_Create(void);

/**
 * @brief  注册 LTDC 场回调并请求波形首帧
 * @note   GUI 和通道控件就绪后调用
 */
void Wave_Display_Task_Start(void);

/**
 * @brief  在 GUI 任务消费波形准备任务提交的标尺重绘请求
 */
void Wave_Process_GUI_Updates(void);

/* 生命周期 */

/**
 * @brief  初始化波形状态、六路默认配置和独立 LTDC 波形层
 * @note   GUI 任务调用，parent 生命周期覆盖模块使用期
 */
bool Wave_Init(lv_obj_t *parent);

/**
 * @brief  删除波形静态根对象并清空模块上下文
 * @note   停止采集并确保工作任务不再访问模块，不删除 parent
 */
void Wave_Deinit(void);

/* 通道配置 */

/**
 * @brief  复制一路物理通道的当前界面配置
 */
bool Wave_Get_Channel_Config(uint32_t channel_index, Wave_Channel_Config_t *config);

/**
 * @brief  保存一路完整界面配置并保证文本结束符有效
 * @note   采集中不可修改，保存后调用 Wave_Apply_Configuration
 */
bool Wave_Set_Channel_Config(uint32_t channel_index, const Wave_Channel_Config_t *config);

/**
 * @brief  保存一路是否显示左侧信息和波形轨道的选择
 * @note   只控制显示，不能隐藏最后一路
 */
bool Wave_Set_Channel_Enabled(uint32_t channel_index, bool enabled);

/**
 * @brief  按启用顺序重新创建通道信息、时间标尺和协议控件
 * @note   GUI 任务在未采集时调用，保留采集数据
 */
bool Wave_Apply_Configuration(void);

/**
 * @brief  保留顶部两行结果区并按协议角色显示最多四路原始轨道
 * @param  roles : 六路角色文字，空值隐藏该路
 * @param  result_channels : 六路结果行标记
 * @note   GUI 任务调用，复制角色文字并重建布局
 */
bool Wave_Apply_Protocol_Layout(const char *protocol_name,
                                const char *const roles[WAVE_CHANNEL_COUNT],
                                const bool result_channels[WAVE_CHANNEL_COUNT]);

/**
 * @brief  退出协议布局并恢复六路普通通道显示
 * @note   保留通道颜色和采集数据
 */
bool Wave_Clear_Protocol_Layout(void);

/**
 * @brief  查询是否已选择协议解码显示布局
 * @note   布局标记不代表解析已完成
 */
bool Wave_Protocol_Layout_Is_Active(void);

/**
 * @brief  修改协议记录数值的显示格式并请求后续帧重绘
 * @note   ASCII 非打印值显示为完整位宽的 [HEX]
 */
bool Wave_Protocol_Set_Display_Format(Wave_Protocol_Display_Format_t format);

/**
 * @brief  清除所有待绘制协议数据块并请求工作任务重绘
 * @note   仅清空显示记录，不取消解析
 */
void Wave_Protocol_Results_Clear(void);

/**
 * @brief  添加一个按当前 HEX/ASCII 格式绘制的普通协议记录块
 * @param  row_index : 结果行 0 或 1
 * @param  digits : HEX 位数，裁剪为 1~8
 * @note   GUI 任务调用，记录复制后异步绘制
 */
bool Wave_Protocol_Result_Add(uint8_t row_index,
                              uint32_t start_cnt,
                              uint32_t end_cnt,
                              uint32_t value,
                              uint8_t digits);

/**
 * @brief  添加一个不绘制数值的协议密集区块
 * @note   用于密集记录，只绘制覆盖区块
 */
bool Wave_Protocol_Compact_Block_Add(uint8_t row_index, uint32_t start_cnt, uint32_t end_cnt);

/**
 * @brief  添加以 W/R 标识传输方向的 I2C 设备地址块
 * @param  address_bits : 地址位宽 7 或 10
 * @note   地址固定使用 HEX，不随 ASCII 模式变化
 */
bool Wave_Protocol_IIC_Address_Add(uint8_t row_index,
                                   uint32_t start_cnt,
                                   uint32_t end_cnt,
                                   uint32_t address,
                                   uint8_t address_bits,
                                   bool read);

/**
 * @brief  添加以颜色区分读写方向的 I2C 数据字节块
 */
bool Wave_Protocol_IIC_Data_Add(uint8_t row_index,
                                uint32_t start_cnt,
                                uint32_t end_cnt,
                                uint32_t value,
                                bool read);

/**
 * @brief  添加按位宽拆分 START、数据与 STOP 区间的 UART 帧
 * @param  data_bits : 数据位数 5~9
 * @param  stop_bits : 停止位数 1 或 2
 * @note   按整帧时间等比例划分区段，不验证物理时序
 */
bool Wave_Protocol_UART_Result_Add(uint8_t row_index,
                                   uint32_t start_cnt,
                                   uint32_t end_cnt,
                                   uint32_t value,
                                   uint8_t data_bits,
                                   bool parity_enabled,
                                   uint8_t stop_bits);

/* 采集控制 */

/**
 * @brief  在 LA 接受启动后重置视图并开始新一轮六路采集
 * @param  timer_start_cnt : 兼容参数，实际时间轴从 0 开始
 * @note   启动失败保留原视图，成功后重新实时跟随
 */
bool Wave_Start_Capture(uint32_t timer_start_cnt);

/**
 * @brief  使用实际截止 CNT 结束本轮并请求完整范围总览
 * @note   使用实际截止 CNT，显示提交异步完成
 */
bool Wave_Complete_Capture(void);

/**
 * @brief  把已导入 LA 服务的文件范围应用为静态波形视图
 * @param  timestamp_hz : 文件时基，单位 Hz
 * @param  end_cnt : 有效截止 CNT
 * @note   数据已导入 LA 后调用，不启动硬件
 */
bool Wave_Use_Loaded_Capture(uint32_t timestamp_hz, uint32_t end_cnt);

/**
 * @brief  清除文件形成的静态视图并恢复空数据窗口
 * @note   文件服务成功退出后调用；这里不会释放它保留的数据
 */
bool Wave_Exit_Loaded_Capture(void);

/**
 * @brief  停止本轮采集并保留暂停时实际范围的完整总览
 * @note   下一轮重新采集；解析须等待 LA 终态
 */
bool Wave_Pause_Capture(void);

/**
 * @brief  请求停止采集并保留当前显示窗口和最后画面
 * @note   保留当前窗口，需要总览时调用暂停或完成接口
 */
void Wave_Stop_Capture(void);

/**
 * @brief  停止采集并请求清空波形显示区域
 * @note   保留通道布局，不删除 LA 存储
 */
void Wave_Cancel_Capture(void);

/**
 * @brief  查询波形控制器是否处于采集状态
 * @note   显示状态不代表 LA 后台收尾已完成
 */
bool Wave_Is_Capturing(void);

/**
 * @brief  采集波形时，读取 LA 的实际进度和结束状态
 * @note   进度来自硬件，DONE 和 ERROR 均视为结束
 */
bool Wave_Get_Capture_Progress(uint32_t *elapsed_cnt, bool *finished);

/**
 * @brief  按当前时间戳频率修改计划采集时长
 * @param  duration_us : 非零时长，单位微秒
 */
bool Wave_Set_Capture_Duration(uint32_t duration_us);

/**
 * @brief  返回当前计划采集时长对应的 CNT 数
 * @note   单位为当前时基 CNT
 */
uint32_t Wave_Get_Capture_Duration(void);

/**
 * @brief  保存采集频率和时长并恢复空窗口
 * @param  timestamp_hz : 非零频率，单位 Hz
 * @param  duration_us : 非零时长，单位微秒
 * @note   CNT 须在 1~UINT32_MAX，硬件频率由 LA 启动验证
 */
bool Wave_Set_Capture_Parameters(uint32_t timestamp_hz, uint32_t duration_us);

/**
 * @brief  读取当前显示和采集使用的时间戳频率
 * @note   单位 Hz，文件导入后使用文件时基
 */
uint32_t Wave_Get_Capture_Timestamp_Hz(void);

/**
 * @brief  读取当前计划采集时长
 * @note   单位微秒，不表示当前进度
 */
uint32_t Wave_Get_Capture_Duration_Us(void);

/**
 * @brief  保存下一轮采集使用的六路统一硬件滤波编码
 * @param  input_filter : 0~15，0 关闭滤波
 * @note   下次启动生效，滤波时钟与时间戳预分频独立
 */
bool Wave_Set_Capture_Filter(uint8_t input_filter);

/**
 * @brief  读取下一轮采集使用的六路统一硬件滤波编码
 * @note   文件导入不改变此配置
 */
uint8_t Wave_Get_Capture_Filter(void);

/**
 * @brief  查询当前是否允许修改下一轮采集滤波
 */
bool Wave_Capture_Filter_Is_Configurable(void);

/**
 * @brief  修改下一轮实时跟随所使用的窗口宽度
 * @note   单位 CNT，0 表示忽略
 */
void Wave_Set_Window_Size(uint32_t window_cnt);

/**
 * @brief  把水平像素位移转换为待提交的历史浏览窗口
 * @note   正值查看更早数据，负值查看更晚数据
 */
void Wave_Pan_View_Pixels(int32_t delta_x_pixels);

/**
 * @brief  以给定屏幕位置为时间锚点切换 1/2/5 缩放档位
 * @note   GUI 任务提交目标，绘制任务异步更新像素
 */
void Wave_Zoom_View_At(int32_t anchor_x_pixels, bool zoom_in);

/**
 * @brief  退出历史浏览并恢复采集窗口跟随最新 CNT
 */
void Wave_Return_To_Live(void);

/* CNT 范围 */

/**
 * @brief  取得从上次读取到当前时刻新增的 CNT 区间
 * @note   一次只允许一个任务顺序读取增量数据
 */
bool Wave_Get_Live_Range(Wave_Cnt_Range_t *range);

/**
 * @brief  复制当前已提交显示窗口的 CNT 左右边界
 * @note   返回已提交窗口，不含待应用的手势目标
 */
bool Wave_Get_View_Range(Wave_Cnt_Range_t *range);

/* 显示统计 */

/**
 * @brief  读取 LTDC 波形层已完成的垂直消隐换帧次数
 * @note   统计实际 LTDC 换帧，32 位自然回绕
 */
uint32_t Wave_Get_Refresh_Count(void);

/* ================================================== */

#ifdef __cplusplus
}
#endif

#endif /* WAVE_SERVICE_H */
