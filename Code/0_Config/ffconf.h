/* ffconf.h：FatFs R0.16 文件系统配置 */

/* 这个版本号要与 FatFs 的 ff.h 一致 */
#define FFCONF_DEF 80386

/*---------------------------------------------------------------------------/
/ 功能裁剪
/---------------------------------------------------------------------------*/

/* 0 保留读写能力，1 构建只读库 */
#define FF_FS_READONLY 0

/* 文件管理器需要完整的目录和文件操作 */
#define FF_FS_MINIMIZE 0

/* 不编译 f_findfirst/f_findnext 过滤遍历 API */
#define FF_USE_FIND 0

/* 运行时不格式化 SD 卡 */
#define FF_USE_MKFS 0

/* 文件定位时逐段查找，不额外建立快速索引 */
#define FF_USE_FASTSEEK 0

/* 文件随写入增长，不提前分配空间 */
#define FF_USE_EXPAND 0

/* 不编译 f_chmod/f_utime，当前应用不提供文件属性和时间戳主动修改接口 */
#define FF_USE_CHMOD 0

/* 不编译 f_getlabel/f_setlabel，卷标签读写不是当前文件服务功能 */
#define FF_USE_LABEL 0

/* 不编译 f_forward 回调转发 */
#define FF_USE_FORWARD 0

/* 关闭 f_gets、f_putc、f_puts 和 f_printf 文本辅助 API */
#define FF_USE_STRFUNC 0
#define FF_PRINT_LLI   0
#define FF_PRINT_FLOAT 0
#define FF_STRF_ENCODE 0

/*---------------------------------------------------------------------------/
/ 名称编码与路径规则
/---------------------------------------------------------------------------*/

/* OEM 短文件名转换使用美国代码页 437 */
#define FF_CODE_PAGE 437

/* 长文件名使用堆上的临时缓冲区 */
#define FF_USE_LFN 3
#define FF_MAX_LFN 255

/* API 文件名使用 UTF-8 */
#define FF_LFN_UNICODE 2

/* FILINFO 文件名容量按当前 TCHAR 单位计算 */
#define FF_LFN_BUF 765
#define FF_SFN_BUF 12

/* 关闭当前目录与相对路径功能 */
#define FF_FS_RPATH 0

/* 只有同时启用 exFAT 和相对路径时，这项设置才生效 */
#define FF_PATH_DEPTH 10

/*---------------------------------------------------------------------------/
/ 磁盘、逻辑卷与扇区约束
/---------------------------------------------------------------------------*/

/* 只有一张 SD 卡，对应逻辑卷 0 */
#define FF_VOLUMES 1

/* 使用数字卷前缀，例如 0:/，不启用自定义字符串卷名 */
#define FF_STR_VOLUME_ID 0
#define FF_VOLUME_STRS   "RAM", "NAND", "CF", "SD", "SD2", "USB", "USB2", "USB3"

/* 不使用 VolToPart 显式分区映射 */
#define FF_MULTI_PARTITION 0

/* SD 卡驱动固定按 512 字节扇区读写 */
#define FF_MIN_SS 512
#define FF_MAX_SS 512

/* LBA_t 使用 32 位扇区地址，与当前 SDMMC 端口一致 */
#define FF_LBA64 0

/* GPT 分区格式切换的扇区数量阈值 */
#define FF_MIN_GPT 0x10000000

/* 不发出 CTRL_TRIM，文件删除无需磁盘端口实现 ATA-TRIM 或卡片擦除提示 */
#define FF_USE_TRIM 0

/*---------------------------------------------------------------------------/
/ 对象内存、时间戳与并发控制
/---------------------------------------------------------------------------*/

/* 使用常规模式，每个 FIL 文件对象保留独立的 512 字节扇区缓冲 */
#define FF_FS_TINY 0

/* 开启 exFAT，与 FAT/FAT32 共用挂载入口，依赖长文件名支持和 C99 以上编译能力 */
#define FF_FS_EXFAT 1

/* 没有 RTC 时间；新建或修改文件时使用固定日期 2025-01-01 */
#define FF_FS_NORTC   1
#define FF_NORTC_MON  1
#define FF_NORTC_MDAY 1
#define FF_NORTC_YEAR 2025

/* 不在 FILINFO 中额外提供创建时间字段，当前文件列表使用常规文件信息 */
#define FF_FS_CRTIME 0

/* FAT32 存在有效 FSINFO 时使用其中的空闲簇数量与上次分配位置，以减少挂载后的扫描开销 */
#define FF_FS_NOFSINFO 0

/* 最多跟踪 8 个已打开对象，用于拦截冲突操作 */
#define FF_FS_LOCK 8

/* 开启同卷文件/目录访问互斥 */
#define FF_FS_REENTRANT 1
#define FF_FS_TIMEOUT   1000

/*--- End of configuration options ---*/
