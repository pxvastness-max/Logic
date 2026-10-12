/* FreeRTOSConfig.h：STM32H743 单核 Cortex-M7 调度、内存和中断接口配置 */

/* 使用 FreeRTOS Kernel 11.3.1 的 GCC ARM_CM7/r0p1 普通端口 */

/* 适用芯片：STM32H743 */

#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#include <stdint.h>
#include <stddef.h>
#include "time/platform_dwt.h"
#include "platform_config.h"

/*====================== 0. Cortex-M 内核异常路由（必须核对） ==================*/

/* FreeRTOS 中断函数要与启动文件中的向量名称一致 */
#define vPortSVCHandler SVC_Handler
#define xPortPendSVHandler  PendSV_Handler
#define xPortSysTickHandler  SysTick_Handler

/* 当前 SysTick 仅由 FreeRTOS 端口处理 */

/*========================= 1. 硬件与时基（必须核对） =========================*/

#define configCPU_CLOCK_HZ (CPU_CYCLES_HZ) /* 驱动内核 Tick 定时器的时钟频率，单位为 Hz */
/* DWT 的 CYCCNT 与 CPU 内核同频 */
#define configDWT_CPU_TEST_CLOCK_HZ configCPU_CLOCK_HZ
#define configTICK_RATE_HZ          ((TickType_t)1000) /* 内核 Tick 频率，单位为 Hz */
/* #define configSYSTICK_CLOCK_HZ               ( configCPU_CLOCK_HZ ) */ /* Cortex-M SysTick 输入时钟频率，单位为 Hz */
#define configTICK_TYPE_WIDTH_IN_BITS  TICK_TYPE_WIDTH_32_BITS /* TickType_t 位宽，可取 TICK_TYPE_WIDTH_16/32/64_BITS */
#define configINITIAL_TICK_COUNT ((TickType_t)0) /* 调度器启动时的初始 Tick 值 */

/* STM32H743 实现 4 位 NVIC 优先级，平台使用全部四位作为抢占优先级 */

#ifndef __NVIC_PRIO_BITS
    #define __NVIC_PRIO_BITS 4U /* NVIC 实现的中断优先级位数 */
#endif
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY 15 /* 可供库使用的最低硬件中断优先级 */
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY  5 /* 只有优先级 5～15 的中断能调用 FromISR API */
#define configKERNEL_INTERRUPT_PRIORITY  (configLIBRARY_LOWEST_INTERRUPT_PRIORITY  << (8 - __NVIC_PRIO_BITS)) /* PendSV/SysTick 使用的最低优先级 */
#define configMAX_SYSCALL_INTERRUPT_PRIORITY  (configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY  << (8 - __NVIC_PRIO_BITS)) /* FreeRTOS 临界区屏蔽边界 */
#define configMAX_API_CALL_INTERRUPT_PRIORITY  configMAX_SYSCALL_INTERRUPT_PRIORITY /* 某些 port 使用的同义名称 */
#define configCHECK_HANDLER_INSTALLATION  0 /* 不在启动时检查 SVC 和 PendSV 向量 */

/*============================== 2. 调度器 ==================================*/

#define configUSE_PREEMPTION   1 /* 高优先级任务就绪时可抢占当前任务 */
#define configUSE_TIME_SLICING 1 /* 同优先级任务轮流运行 */
#define configUSE_PORT_OPTIMISED_TASK_SELECTION  1 /* 使用 Cortex-M 的快速优先级查找 */
#define configMAX_PRIORITIES 32 /* 任务优先级范围为 0 至 31，数字越大越优先，0 同时为 Idle 优先级 */
#define configMINIMAL_STACK_SIZE  ((configSTACK_DEPTH_TYPE)128) /* Idle 栈为 128 个 StackType_t */
#define configMAX_TASK_NAME_LEN           16 /* 任务名最大字符数，包含结尾 '\0' */
#define configIDLE_SHOULD_YIELD           1 /* 空闲任务会让出 CPU 给同优先级任务 */
#define configUSE_TASK_PREEMPTION_DISABLE 0 /* 逐任务抢占控制开关：1 提供任务级抢占禁用功能 */
#define configNUMBER_OF_CORES             1 /* FreeRTOS 调度器管理的 CPU 核心数量 */
#define configRUN_MULTIPLE_PRIORITIES  0 /* SMP 多优先级并行开关：1 允许不同优先级任务在不同核心同时运行 */
#define configUSE_CORE_AFFINITY          0 /* SMP 核心亲和性开关：1 提供任务核心掩码 API */
#define configTASK_DEFAULT_CORE_AFFINITY tskNO_AFFINITY /* 未显式指定时的默认核心亲和掩码 */
#define configUSE_PASSIVE_IDLE_HOOK 0 /* SMP 被动 Idle Hook 开关：1 调用 configPASSIVE_IDLE_HOOK */

/*=========================== 3. 任务与内核对象 ==============================*/

#define configUSE_TASK_NOTIFICATIONS 1 /* 直接任务通知开关：1 编译任务通知 API */
#define configTASK_NOTIFICATION_ARRAY_ENTRIES  1 /* 每个任务只有一个通知槽；其他等待用途改用队列或信号量 */
#define configUSE_MUTEXES             1 /* 互斥锁开关：1 编译互斥锁和优先级继承功能 */
#define configUSE_RECURSIVE_MUTEXES   1 /* LVGL FreeRTOS OS 适配使用递归互斥锁 */
#define configUSE_COUNTING_SEMAPHORES 1 /* LVGL FreeRTOS 同步对象使用计数信号量 */
#define configUSE_QUEUE_SETS          0 /* 队列集合开关：1 编译队列集合 API */
#define configQUEUE_REGISTRY_SIZE     0 /* 调试器可登记的队列和信号量数量 */
#define configUSE_EVENT_GROUPS        1 /* LVGL FreeRTOS 移植使用事件组进行线程同步 */
#define configUSE_STREAM_BUFFERS      1 /* 启用流缓冲区和消息缓冲区 */
#define configUSE_SB_COMPLETED_CALLBACK  0 /* 不为缓冲区保存完成回调 */
#define configUSE_APPLICATION_TASK_TAG 0 /* 应用任务标签开关：1 在 TCB 中保存标签并编译相关 API */
#define configNUM_THREAD_LOCAL_STORAGE_POINTERS 0 /* 每个任务保留的线程局部存储指针数量 */
#define configUSE_POSIX_ERRNO      0 /* POSIX errno 开关：1 为每个任务保存 FreeRTOS_errno */
#define configUSE_NEWLIB_REENTRANT 0 /* newlib 重入支持开关：1 为每个任务分配 _reent 结构 */
#define configUSE_C_RUNTIME_TLS_SUPPORT  0 /* C 运行库 TLS 开关：1 要求定义 TLS 类型及初始化、切换、释放宏 */
/* #define configTLS_BLOCK_TYPE                  YourTlsBlockType */ /* C 运行库 TLS 块类型 */
/* #define configINIT_TLS_BLOCK( xTLSBlock )     yourTlsInit( xTLSBlock ) */ /* 创建任务时初始化 TLS */
/* #define configSET_TLS_BLOCK( xTLSBlock )      yourTlsSet( xTLSBlock ) */ /* 任务切换时设置活动任务的 TLS 块 */
/* #define configDEINIT_TLS_BLOCK( xTLSBlock )   yourTlsDeinit( xTLSBlock ) */ /* 删除任务时释放 TLS */
#define configSTACK_DEPTH_TYPE           uint32_t /* 任务栈深度参数类型 */
#define configMESSAGE_BUFFER_LENGTH_TYPE size_t   /* Message Buffer 内保存消息长度所用类型 */
#define configUSE_MINI_LIST_ITEM  1 /* MiniListItem 布局选择：1 使用较小结构，0 与 ListItem 使用相同结构 */
#define configRECORD_STACK_HIGH_ADDRESS     0 /* 栈高地址记录开关：1 在 TCB 中保存任务栈高地址 */
#define configENABLE_BACKWARD_COMPATIBILITY 0 /* 旧 API 名称兼容开关：1 提供历史名称映射 */

/*============================== 4. 内存管理 =================================*/

#define configSUPPORT_STATIC_ALLOCATION  0 /* 任务和内核对象由动态内存创建 */
#define configSUPPORT_DYNAMIC_ALLOCATION 1 /* 使用 xTaskCreate 等动态创建接口 */
#define configTOTAL_HEAP_SIZE  (48U * 1024U) /* 48 KiB，供任务栈和内核对象使用 */
#define configAPPLICATION_ALLOCATED_HEAP  1 /* ucHeap 由平台放在 DTCM，不能再定义第二份 */
#define configSTACK_ALLOCATION_FROM_SEPARATE_HEAP  0 /* 栈分配器选择：1 使用 pvPortMallocStack/vPortFreeStack */
#define configHEAP_CLEAR_MEMORY_ON_FREE 0 /* 释放清零开关：1 在 vPortFree 时清零被释放内存 */
#define configENABLE_HEAP_PROTECTOR 0 /* 堆保护开关：1 在 heap_4/heap_5 中启用边界保护和指针混淆 */
#define configKERNEL_PROVIDED_STATIC_MEMORY  0 /* 静态系统任务内存来源：1 由内核提供，0 由应用回调提供 */

/*============================== 5. 软件定时器 ===============================*/

#define configUSE_TIMERS          1 /* 启用软件定时器任务和命令队列，供平台周期维护使用 */
#define configTIMER_TASK_PRIORITY (configMAX_PRIORITIES - 1) /* Timer Service 任务优先级 */
#define configTIMER_QUEUE_LENGTH  5 /* Timer Service 命令队列可容纳的命令数量 */
#define configTIMER_TASK_STACK_DEPTH  configMINIMAL_STACK_SIZE /* 定时器任务只有 512 字节栈，回调里不要放大数组 */
#define configTIMER_SERVICE_TASK_CORE_AFFINITY  tskNO_AFFINITY /* SMP 下 Timer Service 任务允许运行的核心 */

/*=========================== 6. Hook 与错误检测 =============================*/

#define configUSE_IDLE_HOOK          0 /* Idle Hook 开关：1 要求应用实现 vApplicationIdleHook */
#define configUSE_TICK_HOOK          0 /* Tick Hook 开关：1 要求应用实现 vApplicationTickHook */
#define configUSE_MALLOC_FAILED_HOOK 1 /* 内存分配失败时调用 vApplicationMallocFailedHook */
#define configUSE_DAEMON_TASK_STARTUP_HOOK  0 /* Daemon 启动 Hook 开关：1 调用 vApplicationDaemonTaskStartupHook */
#define configCHECK_FOR_STACK_OVERFLOW  2 /* 同时检查栈指针和栈末尾的填充值 */
#define configASSERT(x) \
    do \
    { \
        if ((x) == 0) \
        { \
            taskDISABLE_INTERRUPTS(); \
            for (;;) \
            { \
            } \
        } \
    } while ( \
        0) /* 断言失败后提高 BASEPRI 并停留循环，不能假定数值 0 至 4 的高优先级中断同时被屏蔽 */

/*============================= 7. 低功耗 Tickless ============================*/

#define configUSE_TICKLESS_IDLE 0 /* Tickless Idle 模式：0 关闭，1 使用 port 实现，2 使用应用实现 */
#define configEXPECTED_IDLE_TIME_BEFORE_SLEEP  2 /* 启用 Tickless 后才使用的最小预计空闲 Tick 数 */
/* #define configPRE_SLEEP_PROCESSING( x )       do { ( void ) ( x ); } while( 0 ) */ /* 睡眠前回调，可修改预计睡眠 Tick */
/* #define configPOST_SLEEP_PROCESSING( x )      do { ( void ) ( x ); } while( 0 ) */ /* 唤醒后回调 */
/* #define configPRE_SUPPRESS_TICKS_AND_SLEEP_PROCESSING( x ) */ /* port 抑制 Tick 前的可选回调 */

/*============================= 8. 运行统计与跟踪 =============================*/

#define configGENERATE_RUN_TIME_STATS  1 /* 按 DWT 64 位 CPU 周期统计任务运行时间，计数单位为周期而非微秒 */
#define configUSE_TRACE_FACILITY 1 /* 跟踪设施开关：1 在内核对象中加入跟踪所需数据 */
#define configUSE_STATS_FORMATTING_FUNCTIONS  0 /* 文本统计 API 开关：1 编译 vTaskList 等格式化函数 */
#define configSTATS_BUFFER_MAX_LENGTH  4096 /* 格式化统计 API 的默认缓冲区上限，当前格式化函数关闭，项目自行计算并输出任务统计 */
#define configRUN_TIME_COUNTER_TYPE uint64_t /* 运行时间统计计数器类型 */
/* DWT 由 Platform_Init 在系统时钟就绪后、调度器启动前初始化 */
#define portCONFIGURE_TIMER_FOR_RUN_TIME_STATS() \
    do \
    { \
    } while (0)
#define portGET_RUN_TIME_COUNTER_VALUE() Platform_DWT_Cycles64()
#define configUSE_LIST_DATA_INTEGRITY_CHECK_BYTES  0 /* 链表完整性字节开关：1 在链表结构中加入校验值 */
#define configPRINTF(X) \
    do \
    { \
    } while (0) /* 内核自带诊断输出为空操作，项目日志服务使用自己的接口，不通过此宏发送 */

/*============================== 9. 协程（旧功能） ============================*/

#define configUSE_CO_ROUTINES           0 /* 协程开关：1 编译 croutine.c 和协程 API */
#define configMAX_CO_ROUTINE_PRIORITIES 2 /* 协程优先级数量，供协程调度器使用 */

/*============================== 10. MPU / 安全 ===============================*/

#define configINCLUDE_APPLICATION_DEFINED_PRIVILEGED_FUNCTIONS  0                             /* 自定义特权函数开关：1 引入应用提供的 privileged_functions.h */
#define configTOTAL_MPU_REGIONS 8 /* MPU 硬件区域数，常见为 8 或 16 */
#define configTEX_S_C_B_FLASH   0x07UL /* Cortex-M MPU 的 Flash TEX/S/C/B 属性 */
#define configTEX_S_C_B_SRAM    0x07UL /* Cortex-M MPU 的 SRAM TEX/S/C/B 属性 */
#define configENFORCE_SYSTEM_CALLS_FROM_KERNEL_ONLY  1 /* MPU v2 权限提升策略：1 限制系统调用只能从内核代码发起 */
#define configALLOW_UNPRIVILEGED_CRITICAL_SECTIONS  0                                               /* 非特权临界区开关：1 允许非特权任务屏蔽中断 */
#define configUSE_MPU_WRAPPERS_V1               0   /* MPU wrapper 版本：0 使用 v2，1 使用 v1 */
#define configPROTECTED_KERNEL_OBJECT_POOL_SIZE 32  /* MPU v2 可保护的内核对象总数 */
#define configSYSTEM_CALL_STACK_SIZE            128 /* MPU v2 每个任务的系统调用栈深度，单位为字 */
#define configENABLE_ACCESS_CONTROL_LIST  0 /* 内核对象 ACL 开关：1 启用 MPU wrapper v2 访问控制列表 */

/* ARMv8-M 与 MVE 专用选项，当前 Cortex-M7 端口不读取这些配置 */
#define configENABLE_TRUSTZONE          0 /* ARMv8-M TrustZone 调用支持开关：1 启用 */
#define configRUN_FREERTOS_SECURE_ONLY  0 /* ARMv8-M 安全域模式：1 让 FreeRTOS 仅运行于安全侧 */
#define configENABLE_MPU                0 /* ARMv8-M MPU 上下文支持开关：1 启用 */
#define configENABLE_FPU                0 /* ARMv8-M FPU 上下文支持开关：1 启用 */
#define configENABLE_MVE                0 /* Cortex-M55/M85 MVE 上下文支持开关：1 启用 */
#define secureconfigMAX_SECURE_CONTEXTS 5 /* 最多可进入安全侧的非安全任务数量 */

/*============================== 11. API 包含选项 ==============================*/

#define INCLUDE_vTaskPrioritySet       0 /* vTaskPrioritySet API 编译开关：1 包含该函数 */
#define INCLUDE_uxTaskPriorityGet      0 /* uxTaskPriorityGet API 编译开关：1 包含该函数 */
#define INCLUDE_vTaskDelete            1 /* vTaskDelete API 编译开关：1 包含该函数 */
#define INCLUDE_vTaskSuspend           0 /* vTaskSuspend/vTaskResume API 编译开关：1 包含这些函数 */
#define INCLUDE_xTaskResumeFromISR     0 /* xTaskResumeFromISR API 编译开关：1 包含该函数 */
#define INCLUDE_xTaskDelayUntil        1 /* xTaskDelayUntil API 编译开关：1 包含该函数 */
#define INCLUDE_vTaskDelay             1 /* vTaskDelay API 编译开关：1 包含该函数 */
#define INCLUDE_xTaskGetSchedulerState 1 /* xTaskGetSchedulerState API 编译开关：1 包含该函数 */
#define INCLUDE_xTaskGetCurrentTaskHandle  1 /* xTaskGetCurrentTaskHandle API 编译开关：1 包含该函数 */
#define INCLUDE_uxTaskGetStackHighWaterMark  0 /* uxTaskGetStackHighWaterMark API 编译开关：1 包含该函数 */
#define INCLUDE_uxTaskGetStackHighWaterMark2  0 /* uxTaskGetStackHighWaterMark2 API 编译开关：1 包含该函数 */
#define INCLUDE_xTaskGetIdleTaskHandle 0 /* xTaskGetIdleTaskHandle API 编译开关：1 包含该函数 */
#define INCLUDE_eTaskGetState          0 /* eTaskGetState API 编译开关：1 包含该函数 */
#define INCLUDE_xEventGroupSetBitFromISR  0 /* xEventGroupSetBitsFromISR API 编译开关：1 包含该函数 */
#define INCLUDE_xTimerPendFunctionCall   0 /* xTimerPendFunctionCall API 编译开关：1 包含该函数 */
#define INCLUDE_xTaskAbortDelay          0 /* xTaskAbortDelay API 编译开关：1 包含该函数 */
#define INCLUDE_xTaskGetHandle           0 /* xTaskGetHandle API 编译开关：1 包含该函数 */
#define INCLUDE_xQueueGetMutexHolder     0 /* xQueueGetMutexHolder API 编译开关：1 包含该函数 */
#define INCLUDE_xSemaphoreGetMutexHolder 0 /* xSemaphoreGetMutexHolder API 编译开关：1 包含该函数 */

/*=========================== 12. 高级/特殊集成开关 ===========================*/

#define configINCLUDE_FREERTOS_TASK_C_ADDITIONS_H  0                               /* tasks.c 扩展头开关：1 包含 freertos_tasks_c_additions.h */
#define configUSE_ALTERNATIVE_API 0 /* 已弃用替代队列 API 的兼容开关 */
/* #define configCONTROL_INFINITE_LOOP() TestShouldContinue() */ /* 内核无限循环继续条件的应用覆盖接口 */
#define configPRECONDITION(x)  configASSERT(x)                   /* 内核前置条件检查动作 */
#define configUSE_PICOLIBC_TLS 0 /* picolibc TLS 开关：1 为每个任务维护运行库 TLS */

#endif /* FREERTOS_CONFIG_H */
