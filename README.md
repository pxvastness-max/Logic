# Logic：基于 STM32H743 的六通道逻辑分析仪

这是一个运行在 STM32H743 上的嵌入式逻辑分析仪工程。它采集六路数字信号的电平变化时间，使用触摸屏查看波形，并对 UART、I²C（代码中称 IIC）和 SPI 数据进行解析。采集结果可以保存到 SD 卡，之后再加载查看

基于 STM32 HAL、FreeRTOS、LVGL 和 FatFs。它面向配有 RGB LCD、触摸屏、SD 卡接口及外部 SDRAM 的目标板

## 主要功能

- **六路采集**：TIM2、TIM5 的六个输入捕获通道记录双边沿时间戳，DMA 将记录写入循环缓冲区；采集服务负责整理数据、停止收尾及异常状态
- **波形查看**：LVGL 提供触摸界面；波形服务将采集记录绘制到 LCD 图层，支持在界面中浏览采集结果
- **协议解析**：后台服务从已完成的采集中解析 UART、IIC、SPI，并将结果标注到波形界面
- **文件管理**：通过 FatFs 访问 SD 卡，支持保存、列出、加载和删除采集文件。文件采用工程内定义的 DTFH 2.0 格式，保存时可按通道选择原始时间戳、差值编码或 Huffman 编码；加载前会校验文件内容

这些能力分别可以从 [采集驱动](Code/4_BSP/la/la_ic.c)、[采集服务](Code/2_Services/la/la_service.c)、[协议服务](Code/2_Services/protocol/protocol_service.c) 和 [文件服务](Code/2_Services/filesystem/file_manager.c) 开始阅读

## 实现思路

```text
数字输入 → TIM 输入捕获 → DMA 循环缓冲 → 采集服务 → 波形绘制 → LTDC / LCD
                                           ├→ UART / IIC / SPI 解析
                                           └→ DTFH 编解码 ↔ FatFs / SD 卡
触摸输入 ────────────────────────────────→ LVGL 界面
```

采集层保存的是**电平翻转的时间戳**，界面和协议解析从同一份采集数据读取各自需要的时间范围。采集结束后，服务会先完成 DMA 尾部数据的处理，再将数据交给保存或解析流程。文件加载也通过采集服务导入，因此加载后的数据可以继续用于波形查看和协议解析

GUI 由一个 FreeRTOS 任务集中调用 LVGL；采集、解析和文件操作由各自的后台任务处理，界面通过服务状态获取进度与结果。初始化顺序及任务创建见 [app_init.c](Code/1_App/main/app_init.c)

内存布局在 [链接脚本](Code/0_Config/STM32H743II.sct) 中明确划分：外部 32 MiB SDRAM 分配给显示缓冲、协议结果和采集记录；DMA 缓冲及其他数据按片内存储区的访问要求放置

## 工程结构

| 路径                                                 | 职责                                                |
| ---------------------------------------------------- | --------------------------------------------------- |
| [`User/`](User/)                                     | 程序入口、系统初始化文件、统一的中断入口和 HAL 配置 |
| [`Code/0_Config/`](Code/0_Config/)                   | FreeRTOS、LVGL、FatFs 等配置与链接脚本              |
| [`Code/1_App/`](Code/1_App/)                         | 初始化顺序、任务启动及界面业务流程                  |
| [`Code/2_Services/`](Code/2_Services/)               | 采集、波形、协议、文件、触摸等服务                  |
| [`Code/3_Devices/`](Code/3_Devices/)                 | LCD、SD 卡、触摸芯片和 SDRAM 器件驱动               |
| [`Code/4_BSP/`](Code/4_BSP/)                         | 定时器、DMA、LTDC、SDMMC 等板级外设适配             |
| [`Code/5_Platform/`](Code/5_Platform/)               | 时钟、MPU、时基及运行时基础设施                     |
| [`Drivers/`](Drivers/)、[`Middleware/`](Middleware/) | CMSIS、STM32 HAL、FreeRTOS、LVGL、FatFs 等依赖      |
| [`MDK-ARM/`](MDK-ARM/)                               | Keil µVision 工程和启动文件                         |

程序从 [User/main.c](User/main.c) 进入，中断服务函数集中在 [stm32h7xx_it.c](User/stm32h7xx_it.c)

## 编译与运行

1. 准备与工程外设连接相符的 STM32H743IITx 目标板，以及 RGB LCD、触摸屏和外部 SDRAM。使用 SD 卡功能还需要接入 SD 卡
2. 在 Keil µVision 中打开 [`MDK-ARM/Logic.uvprojx`](MDK-ARM/Logic.uvprojx)，选择 `Logic` target。工程配置为 **ARM Compiler 6.24** 和 **Keil.STM32H7xx_DFP 4.1.3**；缺少对应工具链或设备包时，先在 Keil 中安装
3. 重新编译并通过适用于目标板的调试器下载程序。启动后应进入 LCD 触摸界面；接入数字信号后，可在界面中启动采集并查看波形。SD 卡相关操作需要可正常挂载的存储卡

工程的时钟、引脚、LCD 识别和外部 SDRAM 初始化都与现有硬件连接相关。移植到其他板卡时，应从 [`Code/4_BSP/`](Code/4_BSP/) 和 [`Code/3_Devices/`](Code/3_Devices/) 的配置入手核对

## 演示视频

[项目演示](https://www.bilibili.com/video/BV1Fipp64EWb/?spm_id_from=333.1387.upload.video_card.click)
