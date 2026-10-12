/* lv_conf.h：LVGL 9.6.0 在 STM32H743、FreeRTOS 和 LTDC 上的功能配置 */

#ifndef LV_CONF_H
#define LV_CONF_H

/* clang-format off */

/* Keil 的头文件路径需要包含 Code/0_Config */

/* 1. 内存和标准库 */

/* 内存分配和字符串处理使用 LVGL 自带的实现 */
#define LV_USE_STDLIB_MALLOC             LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_STRING             LV_STDLIB_BUILTIN
#define LV_USE_STDLIB_SPRINTF            LV_STDLIB_BUILTIN

/* 给控件、样式和临时绘图数据预留 128 KiB */
#define LV_MEM_SIZE                       (128U * 1024U)

/* 内存池由 LVGL 自行分配，不指定固定地址 */
#define LV_MEM_ADR                        0x0

/* 标准 C 类型头文件 */
#define LV_STDINT_INCLUDE                 "stdint.h"
#define LV_STDDEF_INCLUDE                 "stddef.h"
#define LV_STDBOOL_INCLUDE                "stdbool.h"
#define LV_INTTYPES_INCLUDE               "inttypes.h"
#define LV_LIMITS_INCLUDE                 "limits.h"
#define LV_STDARG_INCLUDE                 "stdarg.h"

/* 2. 操作系统适配 */

/* LVGL 的锁和任务使用 FreeRTOS */
#define LV_USE_OS                         LV_OS_FREERTOS
/* GUI 用任务通知唤醒；LVGL 内部等待使用信号量，避免争用同一个通知 */
#define LV_USE_FREERTOS_TASK_NOTIFY       0
/* CPU 占用率由项目的诊断服务统计 */
#define LV_OS_IDLE_PERCENT_CUSTOM         0

/* 3. 显示格式与刷新周期 */

/* LTDC 帧缓冲和 LVGL 绘图缓冲均采用 RGB565，每个像素占 2 字节 */
#define LV_COLOR_FORMAT_DEFAULT           LV_COLOR_FORMAT_RGB565

/* 颜色混合计算的舍入偏移，保持官方默认值 */
#define LV_COLOR_MIX_ROUND_OFS            0

/* 默认每 16 ms 检查一次刷新、输入和动画；实际时间取决于 GUI 任务调度 */
#define LV_DEF_REFR_PERIOD                16

/* DPI 只影响控件尺寸，不改变屏幕分辨率 */
#define LV_DPI_DEF                        130

/* 绘图缓冲区按字节计算行宽，起始地址按 4 字节对齐 */
#define LV_DRAW_BUF_STRIDE_ALIGN          1
#define LV_DRAW_BUF_ALIGN                 4

/* 4. 软件绘制与硬件加速 */

/* 使用单个 CPU 软件绘制单元 */
#define LV_USE_DRAW_SW                    1
#define LV_DRAW_SW_DRAW_UNIT_CNT          1

/* 屏幕使用 RGB565 格式 */
#define LV_DRAW_SW_SUPPORT_RGB565         1

/* 界面需要圆角、阴影和渐变 */
#define LV_DRAW_SW_COMPLEX                1
/* 界面没有使用多方向渐变 */
#define LV_USE_DRAW_SW_COMPLEX_GRADIENTS  0
/* 不给阴影绘制额外预留缓存 */
#define LV_DRAW_SW_SHADOW_CACHE_SIZE      0
/* 缓存最近 4 项圆形绘图结果，减少重复绘制 */
#define LV_DRAW_SW_CIRCLE_CACHE_SIZE      4

/* Cortex-M7 当前不使用额外汇编绘制后端 */
#define LV_USE_DRAW_SW_ASM                LV_DRAW_SW_ASM_NONE

/* 关闭 LVGL 自带 DMA2D 绘制后端及其中断模式，LVGL 绘制仍由 CPU 执行 */
#define LV_USE_DRAW_DMA2D                 0
#define LV_USE_DRAW_DMA2D_INTERRUPT       0

/* 其他平台或 GPU 绘制后端均不使用 */
#define LV_USE_DRAW_ARM2D_SYNC            0
#define LV_USE_DRAW_VG_LITE               0
#define LV_USE_DRAW_DAVE2D                0
#define LV_USE_NEMA_GFX                   0
#define LV_USE_PXP                        0
#define LV_USE_PPA                        0
#define LV_USE_DRAW_EVE                   0
#define LV_USE_G2D                        0
#define LV_USE_DRAW_NANOVG                0
#define LV_USE_DRAW_OPENGLES              0
#define LV_USE_DRAW_SDL                   0

/* 当前不用图像变形、矢量图、控件截图或 ThorVG */
#define LV_USE_MATRIX                     0
#define LV_DRAW_TRANSFORM_USE_MATRIX      0
#define LV_USE_VECTOR_GRAPHIC             0
#define LV_USE_SNAPSHOT                   0
#define LV_USE_THORVG                     0

/* 5. 日志、断言与调试 */

/* LVGL 日志关闭 */
#define LV_USE_LOG                        0

/* 关闭 LVGL 断言及对象/样式/内存完整性深度检查，保留库支持的轻量参数检查 */
#define LV_USE_ASSERT                     0
#define LV_USE_ASSERT_MALLOC              0
#define LV_USE_ASSERT_NULL                0
#define LV_USE_ASSERT_STYLE               0
#define LV_USE_ASSERT_MEM_INTEGRITY       0
#define LV_USE_ASSERT_OBJ                 0
#define LV_USE_CHECK_ARG                  1

/* 关闭 LVGL 内建性能、内存监视与测试工具，显示及 CPU 周期诊断由项目诊断服务提供 */
#define LV_USE_SYSMON                     0
#define LV_USE_PERF_MONITOR               0
#define LV_USE_MEM_MONITOR                0
#define LV_USE_PROFILER                   0
#define LV_USE_TEST                       0
#define LV_USE_MONKEY                     0
#define LV_USE_REFR_DEBUG                 0
#define LV_USE_LAYER_DEBUG                0
#define LV_USE_PARALLEL_DRAW_DEBUG        0

/* 不开放 LVGL 私有 API，应用只依赖稳定的公共接口 */
#define LV_USE_PRIVATE_API                0

/* 6. 字体与文本 */

/* 文本按 UTF-8 解码，断词字符沿用指定的空格和标点集合 */
#define LV_TXT_ENC                        LV_TXT_ENC_UTF8
#define LV_TXT_BREAK_CHARS                " ,.;:-_)]}"
#define LV_TXT_LINE_BREAK_LONG_LEN        0
#define LV_TXT_COLOR_CMD                  "#"

/* 当前不启用双向文本及阿拉伯语/波斯语字符处理 */
#define LV_USE_BIDI                       0
#define LV_USE_ARABIC_PERSIAN_CHARS       0

/* 只编译 Montserrat 14 默认字体，界面图标使用该字体提供的符号 */
#define LV_FONT_MONTSERRAT_14             1
#define LV_FONT_DEFAULT                   LV_FONT_DEFAULT_MONTSERRAT_14
#define LV_FONT_FMT_TXT_LARGE             0
#define LV_USE_FONT_COMPRESSED            0
#define LV_USE_FONT_PLACEHOLDER           1

/* 当前不使用动态字体管理和外部字体引擎 */
#define LV_USE_FONT_MANAGER               0
#define LV_USE_IMGFONT                    0
#define LV_USE_FREETYPE                   0
#define LV_USE_TINY_TTF                   0

/* 7. 控件和布局 */

/* 主题决定控件的默认颜色、圆角、边框等外观 */
#define LV_USE_THEME_DEFAULT              1   /* 默认彩色主题，普通嵌入式 GUI 最常用 */
#define LV_USE_THEME_SIMPLE               0   /* 简洁主题，样式效果比默认主题少 */
#define LV_USE_THEME_MONO                 0   /* 单色主题，适合黑白屏或极简界面 */

/* 界面通过明确坐标布置控件，不使用 Flex/Grid 自动布局，关闭后可裁剪对应排列逻辑 */
#define LV_USE_FLEX                       0   /* 弹性布局：按行或列自动排列、对齐控件 */
#define LV_USE_GRID                       0   /* 网格布局：按照行列单元格排列控件 */

/* 常用基础控件：1 表示编译进工程，0 表示裁剪掉 */
#define LV_USE_ANIMIMG                    0   /* 动画图片：按顺序循环显示多张图片 */
#define LV_USE_ARC                        1   /* 圆弧：常用于旋钮、圆形进度和仪表盘 */
#define LV_USE_ARCLABEL                   0   /* 弧形文字：让文字沿圆弧方向排列 */
#define LV_USE_BAR                        0   /* 进度条：只显示数值进度，不能直接拖动 */
#define LV_USE_BUTTON                     1   /* 按钮：可点击的矩形控件，文字通常由 Label 添加 */
#define LV_USE_BUTTONMATRIX               0   /* 按钮矩阵：在一个控件中生成多行多列按钮 */
#define LV_USE_CALENDAR                   0   /* 日历：显示月份和日期，并支持日期选择 */
#define LV_CALENDAR_WEEK_STARTS_MONDAY    0   /* 0：星期日开头 */
/* 日历控件未编译，下方星期和月份字符串仅保留库配置默认值，不构成当前界面语言设置 */
#define LV_MONDAY_STR                     "Mo"
#define LV_TUESDAY_STR                    "Tu"
#define LV_WEDNESDAY_STR                  "We"
#define LV_THURSDAY_STR                   "Th"
#define LV_FRIDAY_STR                     "Fr"
#define LV_SATURDAY_STR                   "Sa"
#define LV_SUNDAY_STR                     "Su"
#define LV_JANUARY_STR                    "January"
#define LV_FEBRUARY_STR                   "February"
#define LV_MARCH_STR                      "March"
#define LV_APRIL_STR                      "April"
#define LV_MAY_STR                        "May"
#define LV_JUNE_STR                       "June"
#define LV_JULY_STR                       "July"
#define LV_AUGUST_STR                     "August"
#define LV_SEPTEMBER_STR                  "September"
#define LV_OCTOBER_STR                    "October"
#define LV_NOVEMBER_STR                   "November"
#define LV_DECEMBER_STR                   "December"
#define LV_USE_CALENDAR_HEADER_ARROW      0   /* 日历顶部使用左右箭头切换月份 */
#define LV_USE_CALENDAR_HEADER_DROPDOWN   0   /* 日历顶部使用下拉框选择年月 */
#define LV_USE_CANVAS                     0   /* 画布：在像素缓冲上自行画点、线、图形或图片 */
#define LV_USE_CHART                      0   /* 图表：绘制折线图、柱状图和散点图等数据曲线 */
#define LV_USE_CHECKBOX                   0   /* 复选框：显示选中/未选中状态和说明文字 */
#define LV_USE_DROPDOWN                   1   /* 下拉列表：协议设置选择通道和参数 */
#define LV_USE_IMAGE                      1   /* 图片：显示 C 数组、文件或符号形式的图片资源 */
#define LV_USE_IMAGEBUTTON                0   /* 图片按钮：用不同图片表示普通、按下等状态 */
#define LV_USE_KEYBOARD                   0   /* 屏幕键盘：通常与 Textarea 配合输入文字 */
#define LV_USE_LABEL                      1   /* 标签：显示文字和 LV_SYMBOL_* 图标，最常用控件之一 */
#define LV_USE_LED                        0   /* 虚拟 LED：使用亮度和颜色模拟指示灯 */
#define LV_USE_LINE                       0   /* 折线：连接一组坐标点，适合静态线段或轮廓 */
#define LV_USE_LIST                       0   /* 列表：纵向排列文本、图标和按钮项目 */
#define LV_USE_MENU                       0   /* 菜单：支持页面、分组和返回导航的多级菜单 */
#define LV_USE_MSGBOX                     0   /* 消息框：显示提示、警告以及确认/取消按钮 */
#define LV_USE_ROLLER                     0   /* 滚轮选择器：上下滚动选择一个文本选项 */
#define LV_USE_SCALE                      0   /* 刻度尺：显示刻度线、刻度文字和指示针 */
#define LV_USE_SLIDER                     0   /* 滑块：可以拖动改变数值，内部基于 Bar */
#define LV_USE_SPAN                       0   /* 富文本：在同一段文字中使用不同字体和颜色 */
#define LV_USE_SPINBOX                    0   /* 数字输入框：逐位增加或减少数值 */
#define LV_USE_SPINNER                    0   /* 加载动画：持续旋转的圆弧，表示正在处理 */
#define LV_USE_SWITCH                     1   /* 开关：协议设置的启用状态 */
#define LV_USE_TABLE                      0   /* 表格：按行列显示文本数据 */
#define LV_USE_TABVIEW                    0   /* 选项卡：通过标签页切换不同内容页面 */
#define LV_USE_TEXTAREA                   0   /* 文本输入框：支持光标、编辑和多行文字 */
#define LV_USE_TILEVIEW                   0   /* 平铺视图：通过滑动在多个页面之间切换 */
#define LV_USE_WIN                        0   /* 窗口：带标题栏和内容区的组合控件 */

/* 当前不使用需要额外依赖或资源较多的扩展控件 */
#define LV_USE_3DTEXTURE                  0
#define LV_USE_BARCODE                    0
#define LV_USE_CALENDAR_CHINESE           0
#define LV_USE_FFMPEG                     0
#define LV_USE_GIF                        0
#define LV_USE_GLTF                       0
#define LV_USE_GSTREAMER                  0
#define LV_USE_IME_PINYIN                 0
#define LV_USE_LOTTIE                     0
#define LV_USE_QRCODE                     0
#define LV_USE_RLOTTIE                    0

/* 关闭 LVGL 网格导航与内建手势识别，触摸拖动与双指缩放由自定义输入适配和波形手势模块处理 */
#define LV_USE_GRIDNAV                    0
#define LV_USE_GESTURE_RECOGNITION        0

/* 8. 文件系统、图片与解码库 */

/* 图片数据缓存容量和图片头缓存条目数均为 0，避免为当前少量资源保留额外缓存 */
#define LV_CACHE_DEF_SIZE                 0
#define LV_IMAGE_HEADER_CACHE_DEF_CNT     0
#define LV_USE_RLE                        0
#define LV_USE_LZ4                        0
#define LV_BIN_DECODER_RAM_LOAD           0
#define LV_USE_LODEPNG                    0
#define LV_USE_LIBPNG                     0
#define LV_USE_BMP                        0
#define LV_USE_TJPGD                      0
#define LV_USE_LIBJPEG_TURBO              0
#define LV_USE_LIBWEBP                    0
#define LV_USE_SVG                        0

/* 不向 LVGL 注册 FatFs、LittleFS 或主机文件系统，也不指定默认文件系统驱动字母 */
#define LV_FS_DEFAULT_DRIVER_LETTER       0
#define LV_USE_FS_STDIO                   0
#define LV_USE_FS_POSIX                   0
#define LV_USE_FS_WIN32                   0
#define LV_USE_FS_FATFS                   0
#define LV_USE_FS_LITTLEFS                0
#define LV_USE_FS_ARDUINO_ESP_LITTLEFS    0
#define LV_USE_FS_ARDUINO_SD              0
#define LV_USE_FS_UEFI                    0
#define LV_USE_FS_FROGFS                  0
#define LV_USE_FS_MEMFS                   0

/* 关闭 LVGL 内置显示、输入及桌面平台驱动，避免与工程自己的硬件生命周期重复 */
#define LV_USE_LINUX_DRM                  0
#define LV_USE_LINUX_FBDEV                0
#define LV_USE_ST_LTDC                    0
#define LV_USE_EVDEV                      0
#define LV_USE_LIBINPUT                   0
#define LV_USE_NUTTX                      0
#define LV_USE_OPENGLES                   0
#define LV_USE_GLFW                       0
#define LV_USE_QNX                        0
#define LV_USE_SDL                        0
#define LV_USE_UEFI                       0
#define LV_USE_WAYLAND                    0
#define LV_USE_WINDOWS                    0
#define LV_USE_X11                        0
#define LV_USE_NANOVG                     0

/* 9. 上游示例与演示程序 */

/* 示例和演示构建总开关均关闭，全部具体演示功能也关闭，固件只保留项目界面 */
#define LV_BUILD_EXAMPLES                 0
#define LV_BUILD_DEMOS                    0
#define LV_USE_DEMO_BENCHMARK             0
#define LV_USE_DEMO_GLTF                  0
#define LV_USE_DEMO_KEYPAD_AND_ENCODER    0
#define LV_USE_DEMO_MUSIC                 0
#define LV_USE_DEMO_RENDER                0
#define LV_USE_DEMO_STRESS                0
#define LV_USE_DEMO_VECTOR_GRAPHIC        0
#define LV_USE_DEMO_WIDGETS               0
#define LV_USE_DEMO_FLEX_LAYOUT           0
#define LV_USE_DEMO_MULTILANG             0
#define LV_USE_DEMO_SMARTWATCH            0
#define LV_USE_DEMO_EBIKE                 0
#define LV_USE_DEMO_HIGH_RES              0

/* clang-format on */

#endif /* LV_CONF_H */
