# Zephyr 裸机底层详解：从 Reset 到 main 的全链路剖析

> 本文档深入 Zephyr RTOS 的"裸机"底层 —— 从芯片上电复位、汇编启动、C 运行时初始化、设备树驱动模型、链接脚本到单线程模式的全方位教程。适合嵌入式底层开发者和希望透彻理解 Zephyr 启动全过程的读者。
>
> **版本说明**: 基于 Zephyr RTOS 3.7+/main，ARM Cortex-M 为主参考架构。

---

## 目录

1. [概述：什么是 Zephyr 的"裸机"层面](#1-概述什么是-zephyr-的裸机层面)
2. [构建系统：West + CMake + Ninja](#2-构建系统west--cmake--ninja)
3. [链接脚本与内存布局](#3-链接脚本与内存布局)
4. [设备树 (Device Tree) 详解](#4-设备树-device-tree-详解)
5. [启动流程全链路：Reset → main](#5-启动流程全链路reset--main)
6. [汇编启动阶段 (Reset Handler)](#6-汇编启动阶段-reset-handler)
7. [C 启动阶段：z_prep_c 与 z_cstart](#7-c-启动阶段z_prep_c-与-z_cstart)
8. [设备初始化框架：SYS_INIT 与设备模型](#8-设备初始化框架sys_init-与设备模型)
9. [驱动模型 (Device Driver Model)](#9-驱动模型-device-driver-model)
10. [Kconfig 配置系统](#10-kconfig-配置系统)
11. [板级支持包 (BSP) 与 SoC 移植](#11-板级支持包-bsp-与-soc-移植)
12. [中断管理底层：异常向量表与 NVIC](#12-中断管理底层异常向量表与-nvic)
13. [零延迟中断 (ZLI) 与直接中断](#13-零延迟中断-zli-与直接中断)
14. [Tick 与 Timer 底层机制](#14-tick-与-timer-底层机制)
15. [电源管理与休眠唤醒](#15-电源管理与休眠唤醒)
16. [单线程模式：真正的裸机模式](#16-单线程模式真正的裸机模式)
17. [MPU/MMU 内存保护底层](#17-mpummu-内存保护底层)
18. [工具链与编译选项](#18-工具链与编译选项)
19. [调试与 Trace 机制](#19-调试与-trace-机制)
20. [附录：关键文件索引](#20-附录关键文件索引)

---

## 1. 概述：什么是 Zephyr 的"裸机"层面

Zephyr 虽然是一个 RTOS，但其底层大量涉及"裸机"编程概念：

- **启动代码**：汇编级的 Reset → C 环境初始化
- **链接脚本**：自定义段布局、中断向量表放置
- **硬件抽象**：直接操作寄存器、NVIC、MPU
- **设备树**：编译期将硬件描述转换为 C 数据结构
- **单线程模式**：完全关闭多线程，Zephyr 退化为裸机框架
- **中断处理**：异常向量表直接分发，无需操作系统介入

这些内容构成了 Zephyr 的**裸机基础**，是所有上层 RTOS 功能的基石。

---

## 2. 构建系统：West + CMake + Ninja

### 2.1 总体架构

```
west (元工具)
  └─ zephyr (west.yml 管理的模块)
       ├─ CMakeLists.txt (顶层 CMake)
       ├─ cmake/ (CMake 模块)
       ├─ scripts/ (构建脚本)
       ├─ boards/ (板级定义)
       ├─ soc/ (SoC 定义)
       ├─ drivers/ (驱动)
       ├─ dts/ (设备树)
       ├─ kernel/ (内核)
       └─ arch/ (架构相关)
```

### 2.2 West 基础命令

```powershell
# 初始化工作区
west init -m https://github.com/zephyrproject-rtos/zephyr zephyr-workspace
cd zephyr-workspace
west update        # 拉取所有模块

# 构建
west build -b nrf52840dk/nrf52840 app\my_app
west build -t menuconfig   # 图形化配置
west build -t guiconfig    # 更现代的配置界面
west flash         # 烧录
west debug         # 调试
```

### 2.3 CMake 构建流程

Zephyr 的 CMake 构建分三个阶段：

```
阶段1: 准备 (CMake configure)
  ├─ boards/<BOARD>/*.dts.pre → 设备树预处理
  ├─ 生成 devicetree_unfixed.h
  ├─ 生成 include/generated/ 目录
  ├─ 解析 Kconfig → autoconf.h
  └─ 生成 linker.cmd / linker.ld

阶段2: 编译 (Ninja build)
  ├─ 每个 .c → .o
  └─ .o + 链接脚本 → zephyr.elf

阶段3: 后处理
  ├─ zephyr.hex / .bin / .uf2
  └─ 签名、合并等
```

核心 CMake 入口：

```cmake
# CMakeLists.txt (应用)
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(my_app)

target_sources(app PRIVATE src/main.c)
```

### 2.4 CMakePresets / 多配置

```json
{
  "version": 3,
  "configurePresets": [
    {
      "name": "debug",
      "binaryDir": "build/debug",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Debug"
      }
    }
  ]
}
```

通过 `--preset` 或 `west build -d build/debug` 切换。

---

## 3. 链接脚本与内存布局

### 3.1 链接脚本位置

Zephyr 使用 CMake 生成链接脚本，而非固定的 `.ld` 文件。核心模板位于：

```
zephyr/arch/arm/core/cortex_m/linker.ld
zephyr/include/linker/linker-tool.h
```

生成的链接脚本位于：`build/zephyr/linker.cmd` 或 `build/zephyr/linker.ld`

### 3.2 常见内存区域

```
rom (只读区域):
  ├─ .vector_table    ← 中断向量表
  ├─ .text            ← 代码段
  ├─ .rodata          ← 只读数据
  ├─ .ARM.exidx       ← 异常展开表
  ├─ .devconfig       ← 设备初始化条目
  ├─ .z_init_*        ← SYS_INIT 函数指针数组
  └─ .shell_*         ← Shell 命令表

ram (可读写区域):
  ├─ .data            ← 初始化数据 (从 ROM 拷贝)
  ├─ .bss             ← 零初始化数据
  ├─ .noinit          ← 不复位的数据 (RTC 备份等)
  ├─ stack            ← 主栈/中断栈区域
  └─ thread stacks    ← 各线程栈
```

### 3.3 关键内存区域的链接脚本实现

```ld
/* 向量表 */
SECTION_PROLOGUE(.vector_table,,)
{
    . = ALIGN(4);
    _vector_start = .;
    KEEP(*(.vector_table))
    _vector_end = .;
} > ROM

/* SYS_INIT 函数表 (各级别: EARLY, PRE_KERNEL_1, PRE_KERNEL_2, ...) */
/* 段名格式: .z_init_<LEVEL>_P_<PRIO>_SUB_<SUB>_ */
/* 链接器按级别和优先级自动排序 */
SECTION_PROLOGUE(.z_init_EARLY,,)
{
    __init_EARLY_start = .;
    KEEP(*(SORT(.z_init_EARLY_P_*)));
    __init_EARLY_end = .;
} > ROM

/* 重定位数据 */
SECTION_PROLOGUE(.data,,)
{
    _data_start = .;
    *(.data)
    _data_end = .;
} > RAM AT> ROM
```

### 3.4 自定义内存区域

在应用中使用 `ZEPHYR_SECTION` 定义自定义段：

```c
// 在链接脚本中注册
ZEPHYR_SECTION(my_custom_data, ".my_data")

// 在代码中使用
__section__(".my_data") uint32_t my_var = 0x1234;
```

---

## 4. 设备树 (Device Tree) 详解

### 4.1 设备树文件体系

```
boards/<vendor>/<board>/<board>.dts        ← 板级设备树
dts/arm/<vendor>/<soc>.dtsi                 ← SoC 设备树
dts/bindings/<subsystem>/<vendor>,<dev>.yaml ← 绑定定义
boards/<vendor>/<board>/<board>.dts.overlay ← 覆盖层
```

### 4.2 基本语法

```dts
/dts-v1/;
#include <nrf52840_qiaa.dtsi>

/ {
    model = "Nordic nRF52840 DK";
    compatible = "nordic,nrf52840-dk";

    chosen {
        zephyr,console = &uart0;
        zephyr,flash = &flash0;
        zephyr,sram = &sram0;
    };

    /* 板级外设 */
    leds {
        compatible = "gpio-leds";
        led0: led_0 {
            gpios = <&gpio0 13 GPIO_ACTIVE_LOW>;
            label = "Green LED 0";
        };
    };
};

&uart0 {
    status = "okay";
    current-speed = <115200>;
};
```

### 4.3 生成的头文件

设备树在构建时被处理为 C 头文件：

```
build/zephyr/include/generated/zephyr/devicetree_generated.h  (新版本)
build/zephyr/include/generated/devicetree_unfixed.h          (旧版本)
build/zephyr/include/generated/devicetree_fixups.h
```

生成的关键宏：

```c
// 节点标识符
#define DT_NODELABEL_uart0     DT_N_S_nordic_nRF_UART_40002000

// 属性访问
#define DT_PROP(DT_N_S_soc_uart_40002000, reg)          0x40002000
#define DT_PROP(DT_N_S_soc_uart_40002000, current_speed) 115200
#define DT_IRQN(DT_N_S_soc_uart_40002000)               39

// 标签访问
#define DT_LABEL(DT_N_S_gpio_leds_led_0) "Green LED 0"

// 状态检查
#define DT_NODE_HAS_STATUS(DT_N_S_soc_uart_40002000, okay) 1

// Compatible 匹配
#define DT_COMPAT_NORDIC_NRF_UART 1
```

### 4.4 绑定 (Bindings) YAML

绑定定义了设备树节点的属性和类型：

```yaml
# dts/bindings/gpio/nordic,nrf-gpio.yaml
description: Nordic nRF GPIO controller

compatible: "nordic,nrf-gpio"

include: [gpio-controller.yaml, pinctrl-device.yaml]

properties:
  reg:
    required: true
    type: array
    description: MMIO base address and size

  gpio-count:
    type: int
    default: 32
    description: Number of GPIO pins
```

### 4.5 覆盖层 (Overlay)

覆盖层在不修改原始 `.dts` 的情况下添加/修改节点：

```dts
// app/boards/nrf52840dk_nrf52840.overlay
&i2c1 {
    status = "okay";
    sda-pin = <26>;
    scl-pin = <27>;

    bme280@76 {
        compatible = "bosch,bme280";
        reg = <0x76>;
        label = "BME280";
    };
};
```

---

## 5. 启动流程全链路：Reset → main

### 5.1 全链路概览

```
CPU Reset
  │
  ├─[汇编] 架构启动入口 __start / z_arm_reset
  │   ├─ soc_early_reset_hook()    ← 无栈时极早期（可接管系统）
  │   ├─ 初始化栈指针 MSP
  │   ├─ soc_reset_hook()          ← MSP 就绪后
  │   ├─ 切换到 PSP (Process Stack Pointer)
  │   └─ bl z_prep_c
  │
  ├─[C] z_prep_c()
  │   ├─ soc_prep_hook()           ← C 运行时第一件事
  │   ├─ relocate_vector_table()   ← VTOR 重定位
  │   ├─ z_arm_enable_fpu()        ← FPU 使能
  │   └─ z_arm_interrupt_init()    ← NVIC 基本配置
  │
  ├─[C] z_cstart()
  │   ├─ gcov_static_init()
  │   ├─ z_sys_init_run_level(EARLY)        ← 极早期初始化
  │   ├─ arch_kernel_init()                 ← 架构初始化
  │   ├─ z_device_state_init()              ← 设备状态初始化
  │   ├─ soc_early_init_hook()              ← SoC 早期 Hook
  │   ├─ board_early_init_hook()            ← 板级早期 Hook
  │   ├─ z_sys_init_run_level(PRE_KERNEL_1) ← 无 OS 依赖设备
  │   ├─ arch_smp_init()                    ← SMP 初始化
  │   ├─ z_sys_init_run_level(PRE_KERNEL_2) ← 基础系统设备
  │   │
  │   ├─ [多线程模式] ─────────────────
  │   │   prepare_multithreading()
  │   │     ├─ 初始化调度器
  │   │     ├─ 创建 main 线程
  │   │     └─ 创建 idle 线程
  │   │   switch_to_main_thread() → 上下文切换到 main()
  │   │
  │   └─ [单线程模式] ─────────────────
  │       bg_thread_main()
  │         ├─ z_sys_init_run_level(POST_KERNEL)
  │         ├─ soc_late_init_hook()
  │         ├─ board_late_init_hook()
  │         ├─ z_sys_init_run_level(APPLICATION)
  │         ├─ z_init_static_threads()
  │         ├─ [SMP] z_smp_init()
  │         └─ main() ← 直接调用
  │
  └─[C] main()
      └─ 用户应用程序入口
```

### 5.2 关键阶段的时间线

```
 时间 ──────────────────────────────────────────────>
       | 汇编 | z_prep_c |  z_cstart  | bg_thread | main |
       |  极短  |  ~1ms   |  ~3-30ms  | ~2-20ms   | ∞    |
       | 关中断 | 关中断   | 逐步开中断 | 开中断    | 开中断 |
```

---

## 6. 汇编启动阶段 (Reset Handler)

### 6.1 向量表与入口

ARM Cortex-M 上电后从向量表加载 MSP 和 Reset Handler：

```asm
/* arch/arm/core/cortex_m/reset.S - 简化 */

.section .vector_table, "a"
__vector_table:
    .word _main_stack + CONFIG_MAIN_STACK_SIZE    /* 初始 MSP */
    .word z_arm_reset                             /* Reset Handler */
    .word z_arm_nmi                               /* NMI Handler */
    .word z_arm_hard_fault                        /* Hard Fault */
    /* ... 其余异常向量 ... */
```

### 6.2 Reset Handler 详细流程

```asm
z_arm_reset:
    /* 1. SOC_EARLY_RESET_HOOK — 在栈设置前调用 */
#if defined(CONFIG_SOC_EARLY_RESET_HOOK)
    bl soc_early_reset_hook
#endif

    /* 2. PM_S2RAM 恢复处理 */
#if defined(CONFIG_PM_S2RAM)
    ldr r0, =z_interrupt_stacks + CONFIG_ISR_STACK_SIZE + MPU_GUARD_ALIGN_AND_SIZE
    msr msp, r0
    bl arch_pm_s2ram_resume
#endif

    /* 3. 设置主栈指针 MSP */
    ldr r0, =z_main_stack + CONFIG_MAIN_STACK_SIZE
    msr MSP, r0

    /* 4. SOC_RESET_HOOK */
#if defined(CONFIG_SOC_RESET_HOOK)
    bl soc_reset_hook
#endif

    /* 5. (可选) 架构硬件重初始化 */
#if defined(CONFIG_INIT_ARCH_HW_AT_BOOT)
    movs.n r0, #0
    msr CONTROL, r0
    isb
#if defined(CONFIG_CPU_CORTEX_M_HAS_SPLIM)
    movs.n r0, #0
    msr MSPLIM, r0
    msr PSPLIM, r0
#endif
    /* 关闭 MPU */
    movs.n r0, #0
    ldr r1, =_SCS_MPU_CTRL
    str r0, [r1]
    dsb
    bl z_arm_init_arch_hw_at_boot
#endif

    /* 6. 关中断 (BASEPRI 方式, ARMv7-M/ARMv8-M Mainline) */
#if defined(CONFIG_ARMV7_M_ARMV8_M_MAINLINE)
    movs.n r0, #_EXC_IRQ_DEFAULT_PRIO
    msr BASEPRI, r0
#else
    cpsid i
#endif

    /* 7. 填充中断栈 (调试用) */
#if defined(CONFIG_INIT_STACKS)
    ldr r0, =z_interrupt_stacks
    ldr r1, =0xaa
    ldr r2, =CONFIG_ISR_STACK_SIZE + MPU_GUARD_ALIGN_AND_SIZE
    bl arch_early_memset
#endif

    /* 8. 切换到 PSP (线程模式使用 PSP，中断使用 MSP) */
    ldr r0, =z_interrupt_stacks
    ldr r1, =CONFIG_ISR_STACK_SIZE + MPU_GUARD_ALIGN_AND_SIZE
    adds r0, r0, r1
    msr PSP, r0
    mrs r0, CONTROL
    movs r1, #2
    orrs r0, r1
    msr CONTROL, r0
    isb

    /* 9. 跳转到 C 入口 */
    bl z_prep_c
    /* 永不返回 */
.Lloop:
    wfi
    b .Lloop
```

### 6.3 栈指针切换的意义

```
复位后 → MSP (Main Stack Pointer)
  ├─ MSP 始终指向主栈
  ├─ 中断/异常仍使用 MSP
  └─ 线程模式使用 PSP

z_prep_c → 切换到 PSP
  ├─ MSP = 主栈 (用于中断)
  ├─ PSP = 中断栈 (用于线程上下文)
  └─ 这样设计确保中断不会破坏线程栈
```

---

## 7. C 启动阶段：z_prep_c 与 z_cstart

### 7.1 z_prep_c() — 架构级 C 初始化

```c
// arch/arm/core/cortex_m/prep_c.c
void z_prep_c(void)
{
    /* 1. SoC 复位 Hook（在栈可用后但C运行时前） */
    soc_prep_hook();

    /* 2. 重定位向量表地址到 VTOR */
    relocate_vector_table();

    /* 3. 启用浮点单元 */
    if (IS_ENABLED(CONFIG_CPU_HAS_FPU)) {
        z_arm_enable_fpu();
    }

    /* 4. 中断控制器初始化 */
    z_soc_irq_init();    // 若有 SoC 特定实现
    // 或默认:
    z_arm_interrupt_init();

    /* 5. 进入内核初始化 */
    z_cstart();
}
```

> Hook 执行阶段梳理：
> - `soc_early_reset_hook()` — reset.S 汇编中，无栈时
> - `soc_reset_hook()` — reset.S 中，MSP 已设置后
> - `soc_prep_hook()` — z_prep_c 第一件事
> - `soc_early_init_hook()` — z_cstart, PRE_KERNEL_1 前
> - `board_early_init_hook()` — z_cstart, PRE_KERNEL_1 前
> - `soc_late_init_hook()` — bg_thread_main, POST_KERNEL 后
> - `board_late_init_hook()` — bg_thread_main, POST_KERNEL 后

### 7.2 relocate_vector_table() — VTOR 设置

```c
static void relocate_vector_table(void)
{
    /* 获取链接脚本中定义的向量表基址 */
    uint32_t vtor = (uint32_t)_vector_start;

    /* 写入 VTOR 寄存器 */
#if defined(CONFIG_CPU_CORTEX_M_HAS_VTOR)
    SCB->VTOR = vtor & SCB_VTOR_TBLOFF_Msk;
#endif

    /* 确保写完成 */
    __DSB();
    __ISB();
}
```

### 7.3 z_cstart() — 内核初始化主入口

```c
// kernel/init.c - 精确还原
FUNC_NORETURN void z_cstart(void)
{
    /* gcov 覆盖率钩子 */
    gcov_static_init();

    /* === EARLY 级别 === */
    z_sys_init_run_level(INIT_LEVEL_EARLY);

    /* === 架构内核初始化 === */
    arch_kernel_init();

    /* 日志系统内核初始化 */
    LOG_CORE_INIT();

    /* 多线程模式需初始化 dummy 线程 */
#if defined(CONFIG_MULTITHREADING)
    z_dummy_thread_init(&_thread_dummy);
#endif

    /* 静态设备状态初始化 */
    z_device_state_init();

    /* === SoC & 板级 Hook === */
    soc_early_init_hook();
    board_early_init_hook();

    /* === PRE_KERNEL_1: 无 OS 依赖的设备 === */
    z_sys_init_run_level(INIT_LEVEL_PRE_KERNEL_1);

    /* SMP 初始化 */
#if defined(CONFIG_SMP)
    arch_smp_init();
#endif

    /* === PRE_KERNEL_2: 基础系统设备 === */
    z_sys_init_run_level(INIT_LEVEL_PRE_KERNEL_2);

#if defined(CONFIG_MULTITHREADING)
    /* === 多线程模式：创建 main 线程并切换到它 === */
    switch_to_main_thread(prepare_multithreading());
#else
    /* === 单线程模式：直接调用 bg_thread_main() === */
#if defined(ARCH_SWITCH_TO_MAIN_NO_MULTITHREADING)
    ARCH_SWITCH_TO_MAIN_NO_MULTITHREADING(bg_thread_main, NULL, NULL, NULL);
#else
    bg_thread_main(NULL, NULL, NULL);
    irq_lock();
    while (true) { __WFI(); }
#endif
#endif
}

/* 注: PRE_KERNEL_2 之后的 POST_KERNEL 和 APPLICATION 级别
 * 实际在 bg_thread_main() 中执行，不在 z_cstart() 中。
 */
```

### 7.4 prepare_multithreading() — 多线程准备

```c
// kernel/init.c - 精确还原
static char *prepare_multithreading(void)
{
    char *stack_ptr;

    /* 初始化调度器就绪队列 */
    z_sched_init();

    /* 创建主线程 (bg_thread_main 是入口) */
    stack_ptr = z_setup_new_thread(
        &z_main_thread, z_main_stack,
        K_THREAD_STACK_SIZEOF(z_main_stack),
        bg_thread_main,     // → 内部调 main()
        NULL, NULL, NULL,
        CONFIG_MAIN_THREAD_PRIORITY,
        K_ESSENTIAL, "main");

    z_mark_thread_as_not_sleeping(&z_main_thread);
    z_ready_thread(&z_main_thread);

    /* 初始化 CPU0 (含 idle 线程) */
    z_init_cpu(0);

    return stack_ptr;
}

/* 上下文切换至主线程 */
static FUNC_NORETURN void switch_to_main_thread(char *stack_ptr)
{
#if defined(CONFIG_ARCH_HAS_CUSTOM_SWAP_TO_MAIN)
    arch_switch_to_main_thread(&z_main_thread, stack_ptr, bg_thread_main);
#else
    /* 通过 z_swap 切换到 main 线程 */
    ARG_UNUSED(stack_ptr);
    z_swap_unlocked();
#endif
    CODE_UNREACHABLE;
}
```

### 7.5 bg_thread_main() — 真正的内核完成阶段

```c
// kernel/init.c
static void bg_thread_main(void *unused1, void *unused2, void *unused3)
{
    z_sys_post_kernel = true;

    /* === POST_KERNEL: 需要内核服务的设备 === */
    z_sys_init_run_level(INIT_LEVEL_POST_KERNEL);

    /* 后期 SoC & 板级 Hook */
    soc_late_init_hook();
    board_late_init_hook();

    /* === APPLICATION: 应用组件 === */
    z_sys_init_run_level(INIT_LEVEL_APPLICATION);

    /* 启动静态线程 */
    z_init_static_threads();

    /* SMP 最终初始化 */
#if defined(CONFIG_SMP)
    z_smp_init();
    z_sys_init_run_level(INIT_LEVEL_SMP);
#endif

    /* === 调用 main() === */
    (void)main();

    /* main 返回后标记非 essential */
    z_thread_essential_clear(&z_main_thread);
}
```

> **关键区别**: `z_cstart()` 只负责 EARLY、PRE_KERNEL_1、PRE_KERNEL_2 三个级别。POST_KERNEL 和 APPLICATION 级别在 `bg_thread_main()` 中执行。这在多线程模式下是一个独立的内核线程，在单线程模式下被直接调用。

---

## 8. 设备初始化框架：SYS_INIT 与设备模型

### 8.1 SYS_INIT 宏展开

```c
#define SYS_INIT(init_fn, level, prio) \
    Z_INIT_ENTRY_DEFINE(init_fn, init_fn, level, prio)
```

展开后等价于：

```c
static const struct init_entry Z_REF(init_##init_fn) \
__used __section__(".z_init_" #level "_P_" #prio "_SUB_0_") \
= { .init_fn = init_fn, ... };
```

### 8.2 初始化级别与顺序

| 级别 | 时机 | 可用服务 | 典型用途 |
|------|------|---------|----------|
| `EARLY` | z_cstart 最顶部 | 无 | 架构/SoC 极早期 |
| `PRE_KERNEL_1` | z_cstart 中段 | 无 | 时钟、电源、基础外设 |
| `PRE_KERNEL_2` | z_cstart 尾部 | 部分硬件 | 中断控制器、SysTick |
| `POST_KERNEL` | bg_thread_main 中 | 全部内核 API | 信号量、消息队列等 |
| `APPLICATION` | bg_thread_main 中 | 全部内核 API | 应用组件初始化 |
| `SMP` | bg_thread_main 尾部 | 全部内核 API | 多核初始化 |

### 8.3 初始化函数的链接器布局

链接脚本将所有 `.z_init_*` 段按级别和优先级排序：

```ld
__init_EARLY_start = .;
KEEP(*(SORT(.z_init_EARLY_P_*)));
__init_EARLY_end = .;

__init_PRE_KERNEL_1_start = .;
KEEP(*(SORT(.z_init_PRE_KERNEL_1_P_*)));
__init_PRE_KERNEL_1_end = .;
/* ... 以此类推 */
```

### 8.4 运行时遍历

```c
// kernel/init.c - 实际实现
enum init_level {
    INIT_LEVEL_EARLY = 0,
    INIT_LEVEL_PRE_KERNEL_1,
    INIT_LEVEL_PRE_KERNEL_2,
    INIT_LEVEL_POST_KERNEL,
    INIT_LEVEL_APPLICATION,
#ifdef CONFIG_SMP
    INIT_LEVEL_SMP,
#endif
};

static void z_sys_init_run_level(enum init_level level)
{
    /* 按顺序排列各级别起始指针 + 末尾哨兵 */
    static const struct init_entry *levels[] = {
        __init_EARLY_start,
        __init_PRE_KERNEL_1_start,
        __init_PRE_KERNEL_2_start,
        __init_POST_KERNEL_start,
        __init_APPLICATION_start,
#ifdef CONFIG_SMP
        __init_SMP_start,
#endif
        __init_end,     /* 哨兵 */
    };

    const struct init_entry *entry;
    for (entry = levels[level]; entry < levels[level + 1]; entry++) {
        int result = 0;
        sys_trace_sys_init_enter(entry, level);

        if (entry->dev != NULL) {
            /* 这是一个设备对象：通过 ops.init 初始化 */
            if ((entry->dev->flags & DEVICE_FLAG_INIT_DEFERRED) == 0U) {
                result = do_device_init(entry->dev);
            }
        } else {
            /* 这是纯 SYS_INIT 函数 */
            result = entry->init_fn();
        }

        sys_trace_sys_init_exit(entry, level, result);
    }
}
```

> 注意链接脚本布局与 `levels[]` 数组的对应关系：各 `__init_*_start` 符号之间在内存中是连续的，`levels[]` 按级别顺序排列后，`levels[level]` 到 `levels[level+1]` 正好对应一个完整级别。

### 8.5 Zephyr 模块 (Module) 机制

Zephyr 3.x 引入了更现代的 `ZEPHYR_INIT` 模块机制：

```c
// 定义模块
ZEPHYR_INIT_MODULE(my_subsystem);

// 注册初始化函数
SYS_INIT(my_init, POST_KERNEL, 50);

// 在模块中:
void my_init(void)
{
    my_subsystem_init();
}

// 模块间依赖
ZEPHYR_INIT_MODULE_DEPENDS(my_subsystem, gpio, uart);
```

---

## 9. 驱动模型 (Device Driver Model)

### 9.1 struct device 三部件结构

```c
struct device {
    const char *name;              // 设备名称
    const void *config;            // 只读配置 (ROM)
    const void *api;               // API 函数指针表 (ROM)
    struct device_state *state;    // 运行时状态 (RAM)
    void *data;                    // 运行时数据 (RAM)
    struct device_ops ops;         // 设备操作 (init/deinit 函数指针)
    device_flags_t flags;          // 标志位 (如 DEVICE_FLAG_INIT_DEFERRED)
#if defined(CONFIG_DEVICE_DEPS)
    device_handle_t *deps;         // 依赖关系 (设备句柄数组)
#endif
#if defined(CONFIG_PM_DEVICE)
    union {
        struct pm_device_base *pm_base;
        struct pm_device *pm;
        struct pm_device_isr *pm_isr;
    };                             // 电源管理资源
#endif
};
```

> 3.7+ 版本中 `ops.init` 取代了旧的初始化函数注册方式。初始化时内核通过 `entry->dev->ops.init(dev)` 调用。

### 9.2 一个典型的 UART 驱动

```c
/* === 1. 配置结构 (ROM) === */
struct uart_nrfx_config {
    uint32_t reg_base;
    uint32_t irq_line;
    const struct pinctrl_dev_config *pinctrl;
};

/* === 2. 运行时数据 (RAM) === */
struct uart_nrfx_data {
    struct k_sem sync_sem;
    uint8_t *rx_buf;
    uint32_t rx_len;
};

/* === 3. API 函数表 (ROM) === */
static const struct uart_driver_api uart_nrfx_api = {
    .poll_in = uart_nrfx_poll_in,
    .poll_out = uart_nrfx_poll_out,
    .fifo_fill = uart_nrfx_fifo_fill,
    .fifo_read = uart_nrfx_fifo_read,
    .configure = uart_nrfx_configure,
    .callback_set = uart_nrfx_callback_set,
};

/* === 4. 初始化函数 === */
static int uart_nrfx_init(const struct device *dev)
{
    const struct uart_nrfx_config *config = dev->config;

    /* 映射 MMIO */
    DEVICE_MMIO_MAP(dev, K_MEM_CACHE_NONE);

    /* 配置引脚 */
    pinctrl_apply_config(config->pinctrl);

    /* 使能外设时钟 */
    clock_control_on(CLOCK_CONTROL_NRF_CLOCK,
                     (clock_control_subsys_t)&config->reg_base);

    /* 注册中断 (IRQ_CONNECT 参数需为编译期常量) */
    /* 实际驱动中通过宏将 inst 参数传递进来 */
    irq_enable(config->irq_line);

    return 0;
}

/* 在带 inst 参数的宏中注册中断: */
#define UART_NRFX_IRQ_INST(n) \
    IRQ_CONNECT(DT_INST_IRQN(n), 0, uart_nrfx_isr, \
                DEVICE_DT_INST_GET(n), 0)

/* === 5. 设备实例化 (通过 DT) === */
#define UART_NRFX_INST(n) \
    static const struct uart_nrfx_config uart_nrfx_config_##n = { \
        .reg_base = DT_INST_REG_ADDR(n), \
        .irq_line = DT_INST_IRQN(n), \
        .pinctrl = PINCTRL_DT_INST_DEV_CONFIG(n), \
    }; \
    static struct uart_nrfx_data uart_nrfx_data_##n; \
    DEVICE_DT_INST_DEFINE(n, \
        uart_nrfx_init, \
        NULL,                  /* deinit 函数 */ \
        &uart_nrfx_data_##n, \
        &uart_nrfx_config_##n, \
        POST_KERNEL,           /* 初始化级别 */ \
        CONFIG_UART_INIT_PRIORITY, \
        &uart_nrfx_api);

DT_INST_FOREACH_STATUS_OKAY(UART_NRFX_INST)
```

### 9.3 驱动实例化宏展开

`DEVICE_DT_INST_DEFINE(n, ...)` 大致展开为：

```c
/* 设备对象 (ROM) */
const struct device DEVICE_NAME_GET(uart_nrfx_##n) = {
    .name = DT_INST_LABEL(n),
    .config = &uart_nrfx_config_##n,
    .api = &uart_nrfx_api,
    .state = &DEVICE_STATE_GET(uart_nrfx_##n),
    .data = &uart_nrfx_data_##n,
    .ops = { .init = uart_nrfx_init },
    .flags = 0,
};

/* 同时注册 init_entry 到链接脚本的 .z_init_* 段 */
static const struct init_entry Z_INIT_ENTRY_NAME(uart_nrfx_##n) \
__used __section__(".z_init_POST_KERNEL_P_30_SUB_0_") = {
    .dev = &DEVICE_NAME_GET(uart_nrfx_##n),
};
```

### 9.4 设备依赖与初始化顺序

```c
// 在设备树中表达依赖
&uart0 {
    status = "okay";
    /* 隐含依赖：gpio0 (UART 引脚)、时钟控制器 */
    /* device deps 由 gen_device_deps.py 自动从 DT 生成 */
};
```

运行时通过 `device_required_foreach()` 遍历依赖 (回调模式)：

```c
/* 访问者回调类型 */
typedef int (*device_visitor_callback_t)(const struct device *dev,
                                         void *context);

/* 遍历 dev 直接依赖的所有设备 */
int device_required_foreach(const struct device *dev,
                            device_visitor_callback_t visitor_cb,
                            void *context);

/* 使用示例 */
static int check_dep_ready(const struct device *dep_dev, void *ctx)
{
    if (!device_is_ready(dep_dev)) {
        printk("Dependency %s not ready!", dep_dev->name);
        return -1;  // 停止遍历
    }
    return 0;  // 继续遍历
}

void my_init(void)
{
    const struct device *my_dev = DEVICE_DT_GET(DT_NODELABEL(mydev));
    device_required_foreach(my_dev, check_dep_ready, NULL);
}
```

依赖句柄通过 `device_required_handles_get()` 获取：

```c
const device_handle_t *device_required_handles_get(
    const struct device *dev, size_t *count);
const struct device *device_from_handle(device_handle_t handle);
```

### 9.5 设备模型关键宏总结

| 宏 | 用途 |
|----|------|
| `DEVICE_DEFINE()` | 通用设备定义 (非 DT) |
| `DEVICE_DEINIT_DEFINE()` | 通用设备定义 (带去初始化) |
| `DEVICE_DT_DEFINE(node_id)` | 从 DT 节点定义设备 |
| `DEVICE_DT_DEINIT_DEFINE(node_id)` | 从 DT 节点定义设备 (带去初始化) |
| `DEVICE_DT_INST_DEFINE(inst)` | 从 DT 实例号定义 |
| `DEVICE_DT_INST_DEINIT_DEFINE(inst)` | 从 DT 实例号定义 (带去初始化) |
| `DEVICE_DT_GET(node_id)` | 获取设备指针 |
| `DEVICE_DT_INST_GET(inst)` | 获取实例设备指针 |
| `DEVICE_DT_GET_ANY(compat)` | 按 compatible 获取任意设备 |
| `DEVICE_DT_GET_OR_NULL(node_id)` | 获取设备指针 (不存在返回 NULL) |
| `device_is_ready(dev)` | 检查设备是否已初始化 |
| `device_init(dev)` | 手动初始化延迟初始化的设备 |
| `device_deinit(dev)` | 去初始化设备 |

---

## 10. Kconfig 配置系统

### 10.1 Kconfig 文件层级

```
Kconfig.zephyr (顶层)
  ├─ arch/<ARCH>/Kconfig
  ├─ soc/<VENDOR>/<SOC>/Kconfig
  │    └─ Kconfig.defconfig (默认配置)
  ├─ boards/<VENDOR>/<BOARD>/Kconfig
  │    └─ Kconfig.defconfig
  ├─ drivers/Kconfig
  ├─ subsystems/Kconfig
  └─ app/Kconfig (应用配置)
```

### 10.2 Kconfig 基本语法

```kconfig
# drivers/gpio/Kconfig.nrfx
menuconfig GPIO_NRFX
    bool "nRF GPIO driver"
    depends on SOC_FAMILY_NRF
    default y
    help
      Enable the nRF GPIO driver.

config GPIO_NRFX_PORT_COUNT
    int "Number of GPIO ports"
    depends on GPIO_NRFX
    range 1 4
    default 1
    help
      Number of GPIO port instances.
```

### 10.3 配置生成流程

```
Kconfig 文件
    │  menuconfig / guiconfig / 默认配置
    v
.config (构建目录)
    │
    v
include/generated/autoconf.h
    │
    ├─ #define CONFIG_GPIO_NRFX 1
    ├─ #define CONFIG_GPIO_NRFX_PORT_COUNT 2
    └─ ...
```

### 10.4 应用配置

```kconfig
# prj.conf
CONFIG_GPIO=y
CONFIG_SERIAL=y
CONFIG_UART_INTERRUPT_DRIVEN=y
CONFIG_HEAP_MEM_POOL_SIZE=4096
CONFIG_MAIN_STACK_SIZE=2048
```

条件配置（按板型）：

```kconfig
# boards/nrf52840dk_nrf52840.conf
CONFIG_BOARD_NRF52840DK=y
CONFIG_NFCT_PINS_AS_GPIOS=y
```

### 10.5 Kconfig 与 CMake 的配合

```cmake
# 在 CMake 中读取 Kconfig 值
if(CONFIG_GPIO_NRFX)
    target_sources(app PRIVATE src/gpio_extra.c)
endif()
```

---

## 11. 板级支持包 (BSP) 与 SoC 移植

### 11.1 板级文件结构

```
boards/<vendor>/<board>/
  ├─ <board>.dts          ← 设备树
  ├─ <board>.yaml         ← 板级元信息
  ├─ <board>.conf         ← Kconfig 默认配置
  ├─ <board>.defconfig    ← 默认构建配置
  ├─ CMakeLists.txt       ← 构建文件
  ├─ doc/                 ← 文档
  └─ pre_dt.sh / support/ ← 预设备树处理脚本
```

### 11.2 标准板级 YAML

```yaml
# boards/arm/nrf52840dk_nrf52840/nrf52840dk_nrf52840.yaml
identifier: nrf52840dk/nrf52840
name: nRF52840 DK
type: mcu
arch: arm
toolchain:
  - zephyr
  - gnuarmemb
  - xtools
ram: 256
flash: 1024
supported:
  - adc
  - ble
  - counter
  - gpio
  - i2c
  - spi
  - uart
  - usb
  - watchdog
```

### 11.3 SoC 文件结构

```
soc/<vendor>/<family>/
  ├─ <soc>.dtsi                 ← SoC 级设备树
  ├─ <soc>.dtsi.pre             ← 预处理
  ├─ linker.ld                  ← 链接脚本模板
  ├─ CMakeLists.txt             ← 构建
  ├─ Kconfig                    ← SoC 配置选项
  ├─ Kconfig.defconfig          ← 默认值
  ├─ Kconfig.soc                ← SoC 系列选择
  └─ soc.c                      ← SoC 初始化代码
```

### 11.4 SoC 初始化代码

```c
// soc/arm/nordic_nrf/nrf52/soc.c
void soc_early_init_hook(void)
{
    /* 配置振荡器 */
    NRF_CLOCK->EVENTS_HFCLKSTARTED = 0;
    NRF_CLOCK->TASKS_HFCLKSTART = 1;
    while (!NRF_CLOCK->EVENTS_HFCLKSTARTED) {}

    /* 配置 Flash 等待周期 */
    NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Wen;
    while (!NRF_NVMC->READY) {}
    NRF_NVMC->CONFIG = 0;
}

void soc_early_reset_hook(void)
{
    /* 复位后极早期操作（可接管启动） */
}
```

### 11.5 添加新板级步骤

1. 创建 `boards/<vendor>/<board>/` 目录
2. 编写设备树 `board.dts`（include SoC dtsi）
3. 编写 `board.yaml` 元信息
4. 编写 `board.defconfig` 默认配置
5. 可选：`board.conf`、`CMakeLists.txt`
6. 测试构建：`west build -b <board> samples/hello_world`

---

## 12. 中断管理底层：异常向量表与 NVIC

### 12.1 ARM Cortex-M 异常模型

```
异常类型:
  ├─ #1 Reset          优先级 -3 (最高，不可屏蔽)
  ├─ #2 NMI            优先级 -2
  ├─ #3 HardFault      优先级 -1
  ├─ #4-6 MemFault/BusFault/UsageFault
  ├─ #7-10 保留
  ├─ #11 SVCall        优先级可编程
  ├─ #12 DebugMonitor
  ├─ #13-14 保留
  ├─ #15 PendSV        优先级可编程 (上下文切换)
  ├─ #16 SysTick       优先级可编程 (系统 Tick)
  └─ #16+ 外部中断 (IRQ 0..n)
```

### 12.2 Zephyr 的向量表布局

```c
// arch/arm/core/cortex_m/vector_table.S
.section .vector_table, "a"
.word _main_stack + CONFIG_MAIN_STACK_SIZE  // MSP 初始值
.word z_arm_reset                           // Reset
.word z_arm_nmi                             // NMI
.word z_arm_hard_fault                      // HardFault
.word z_arm_mpu_fault                       // MemManage (MPU)
.word z_arm_bus_fault                       // BusFault
.word z_arm_usage_fault                     // UsageFault
.word 0, 0, 0, 0                           // 保留
.word z_arm_svc                             // SVCall
.word z_arm_debug_monitor                   // DebugMon
.word 0                                     // 保留
.word z_arm_pendsv                          // PendSV
.word z_arm_sys_tick                        // SysTick
.rept IRQ_COUNT
.word _isr_wrapper                          // 外部中断 (统一入口)
.endr
```

### 12.3 中断进入与退出

```asm
/* arch/arm/core/cortex_m/isr_wrapper.S - 简化 */
_isr_wrapper:
    /* 硬件已自动: 压栈 xPSR, PC, LR, R12, R3-R0 */
    /* 硬件已自动: LR = EXC_RETURN */

    /* 软件压栈: R4-R11, LR */
    push {r4-r11, lr}

    /* 获取 IRQ 号 */
    mrs r0, ipsr

    /* 从 _sw_isr_table 查找 ISR 函数和参数 */
    /*
     * _sw_isr_table 条目布局: arg 在前, isr 在后
     * 使用 ldmia 一条指令加载: r0 = arg, r3 = isr
     * (arg→r0, isr→r3 是特定选择，与 Thumb2 调用约定兼容)
     */
    ldr r1, =_sw_isr_table
    sub r0, #16                 /* 减去系统异常的基数 */
    lsl r0, #3                  /* 每个条目 8 字节 */
    add r1, r0
    ldmia r1!, {r0, r3}         /* r0 = arg, r3 = isr 函数指针 */
    blx r3                      /* 调用 ISR */

    /* 检查是否需要重新调度 (当前线程被更高优先级抢占) */
    /* 此处省略架构细节，核心逻辑: 比较当前线程优先级和就绪队列缓存 */

.Lno_reschedule:
    /* 恢复 R4-R11, LR */
    pop {r4-r11, lr}
    /* 硬件自动: 弹出 xPSR, PC, LR, R12, R3-R0 */
    bx lr                      /* LR = EXC_RETURN 恢复线程或返回 main */
```

> **注意**: 在 Zephyr 3.x+ 中某些架构可能使用 `arch_switch` 接口而不是 PendSV 进行上下文切换，但 ISR 包装器的整体结构与此类似。

### 12.4 中断注册表

```c
// 每个中断占用 8 字节 (arg 在前, isr 在后 → ldmia 优化)
struct _isr_table_entry {
    const void *arg;           // 传递给 ISR 的参数
    void (*isr)(const void *); // ISR 函数指针
};

// 软件中断表 (由 gen_isr_tables.py 在构建时生成)
extern struct _isr_table_entry _sw_isr_table[];
```

`IRQ_CONNECT` 的展开过程分两步：

**① 编译时**: 创建 `struct _isr_list` 条目放入 `.intList` 段：

```c
// 由 Z_ISR_DECLARE(irq, flags, func, param) 宏生成
static Z_DECL_ALIGN(struct _isr_list)
Z_GENERIC_SECTION(.intList) __used
_MK_ISR_NAME(func, __COUNTER__) = {
    .irq = irq,
    .flags = flags,
    .func = (void *)&func,
    .param = (const void *)param,
};
```

**② 构建时**: `gen_isr_tables.py` 读取 `.intList` 段 → 为每个条目创建 `_isr_table_entry` 并生成 `_sw_isr_table[]` 数组。

也可在驱动中通过 `ARCH_IRQ_CONNECT` 或 `IRQ_CONNECT` 宏（均展开为 `Z_ISR_DECLARE`）注册：

### 12.5 NVIC 直接操作

```c
// 手动操作 NVIC 寄存器 (无需 Zephyr API)
void enable_irq_raw(uint32_t irq)
{
    NVIC_EnableIRQ((IRQn_Type)irq);
}

void set_irq_priority_raw(uint32_t irq, uint32_t prio)
{
    NVIC_SetPriority((IRQn_Type)irq, prio);
}

void set_pendsv(void)
{
    SCB->ICSR = SCB_ICSR_PENDSVSET_Msk;
}
```

---

## 13. 零延迟中断 (ZLI) 与直接中断

### 13.1 标准中断路径 vs ZLI

```
标准中断路径:
  硬件入栈 → IRQ Entry → 软件入栈 → 查找 ISR 表 → 执行 ISR
  → 检查调度 → 软件出栈 → 硬件出栈 → 返回线程

零延迟中断 (ZLI) 路径:
  硬件入栈 → 执行 ISR → 硬件出栈 → 返回线程
  (完全绕过 Zephyr 软件中断框架)
```

### 13.2 ZLI 配置与使用

```kconfig
CONFIG_ZERO_LATENCY_IRQS=y
```

```c
// 声明 ZLI ISR
void fast_isr(const void *arg)
{
    uint32_t *reg = (uint32_t *)arg;
    *reg = 0;  // 直接操作硬件
    // ⚠️ 不能调用任何内核 API!
    // ❌ k_sem_give()
    // ❌ k_event_post()
    // ❌ k_msgq_put()
}

// 注册 ZLI 中断
IRQ_CONNECT(TIMER0_IRQ, 0, fast_isr,
            (void *)TIMER0_BASE, IRQ_ZERO_LATENCY);
irq_enable(TIMER0_IRQ);
```

### 13.3 直接中断 (Direct Interrupts)

Zephyr 还支持"直接中断"——ISR 的地址被直接填入硬件向量表，跳过 `_isr_wrapper` 和软件 ISR 表查找：

```c
// 使用 IRQ_DIRECT_CONNECT 而非 IRQ_CONNECT
// 参数中不再需要 arg (因为绕过 _sw_isr_table)
#define MY_IRQ_LINE  DT_IRQN(DT_NODELABEL(uart0))
#define MY_ISR       uart0_isr

IRQ_DIRECT_CONNECT(MY_IRQ_LINE, 0, MY_ISR, 0);
irq_enable(MY_IRQ_LINE);
```

直接中断与 ZLI 的区别在于：直接中断的向量表条目直接指向 ISR（跳转减到最少），但仍遵循标准异常返回流程；ZLI 则完全绕过了内核的中断框架。

### 13.4 ZLI 的限制与风险

| 特性 | 标准 ISR | ZLI | 直接中断 |
|------|---------|-----|---------|
| 内核 API 调用 | ✅ 有限制 | ❌ 禁止 | ❌ 禁止 |
| 延迟 | 中等 | 极低 | 低 |
| 上下文保存 | 完整 | 最小 | 最小 |
| 调度触发 | ✅ 自动 | ❌ 手动 | ❌ 手动 |
| 适用场景 | 通用外设 | 高频定时器、DMA | 时间敏感外设 |

---

## 14. Tick 与 Timer 底层机制

### 14.1 SysTick 作为系统 Tick

```c
// arch/arm/core/cortex_m/systick.c
void z_arm_sys_tick_handler(const void *arg)
{
    /* 递增系统 Tick 计数 */
    z_clock_announce();

    /* 如果有 Tickless idle，可能会屏蔽后续 Tick */
}

// SysTick 初始化
int z_clock_driver_init(const struct device *dev)
{
    uint32_t ticks = CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC / CONFIG_SYS_CLOCK_TICKS_PER_SEC;
    SysTick->LOAD = ticks - 1;
    SysTick->VAL = 0;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk |
                    SysTick_CTRL_TICKINT_Msk |
                    SysTick_CTRL_ENABLE_Msk;
    return 0;
}
```

### 14.2 Tickless 空闲模式

```c
// Tickless 核心逻辑 (arch/arm/core/cortex_m/tickless.c)
void z_clock_idle_enter(int32_t ticks)
{
    if (!IS_ENABLED(CONFIG_TICKLESS_KERNEL))
        return;

    /* 计算下次到期时间 */
    uint32_t next_cycles = sys_clock_announce_get_next_cycles();

    if (next_cycles == 0) {
        /* 没有定时器到期，无限期休眠 */
        SysTick->CTRL &= ~SysTick_CTRL_ENABLE_Msk;
    } else {
        /* 设置 SysTick 为单次模式 */
        uint32_t load = next_cycles - sys_clock_elapsed();
        SysTick->LOAD = load;
        SysTick->VAL = 0;
        SysTick->CTRL = SysTick_CTRL_ENABLE_Msk;
    }

    /* CPU 进入 WFI/WFE */
    __WFI();
}

void z_clock_idle_exit(void)
{
    if (!IS_ENABLED(CONFIG_TICKLESS_KERNEL))
        return;

    /* 恢复周期性 Tick */
    SysTick->LOAD = CYCLES_PER_TICK - 1;
    SysTick->VAL = 0;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk |
                    SysTick_CTRL_TICKINT_Msk |
                    SysTick_CTRL_ENABLE_Msk;
}
```

### 14.3 Tickless vs Tickful 对比

```
Tickful (CONFIG_TICKLESS_KERNEL=n):
  时间: |--T--|--T--|--T--|--T--|--T--|--T--|
  唤醒: |__|  |__|  |__|  |__|  |__|  |__|   ← 固定间隔唤醒
  功耗: 高 (频繁唤醒)

Tickless:
  时间: |-----T-----|--T--|----------T----------|
  唤醒: |__|        |__|  |                    |__|  ← 仅工作需要唤醒
  功耗: 低 (长时间休眠)
```

### 14.4 使用 RTC 替代 SysTick

对于低功耗应用，常用 RTC 替代 SysTick：

```kconfig
CONFIG_RTC=y
CONFIG_TIMER_RTC=n
CONFIG_SYS_CLOCK_TICKS_PER_SEC=100
```

```c
// drivers/timer/nrf_rtc_timer.c
static void rtc_timer_isr(const void *arg)
{
    if (NRF_RTC0->EVENTS_TICK) {
        NRF_RTC0->EVENTS_TICK = 0;
        z_clock_announce();  // 通知内核 Tick 到达
    }
    if (NRF_RTC0->EVENTS_COMPARE[0]) {
        NRF_RTC0->EVENTS_COMPARE[0] = 0;
        /* 处理到期定时器 */
    }
}
```

---

## 15. 电源管理与休眠唤醒

### 15.1 电源管理状态

```text
Zephyr 电源状态:
  ┌─────────────────────────────────────┐
  │  Active (运行)                       │
  └──────┬──────────────────────────────┘
         │  PM 策略
         v
  ┌──────────────────┐  ┌──────────────────┐
  │  Suspend-to-Idle  │  │  Suspend-to-RAM  │
  │  (浅睡眠)         │  │  (深度睡眠)       │
  │  CPU WFI/WFE     │  │  RAM 保持         │
  │  SysTick 停止    │  │  CPU 掉电         │
  │  唤醒: 中断       │  │  唤醒: RTC/GPIO  │
  └──────────────────┘  └──────────────────┘
```

### 15.2 PM 框架代码

```c
#include <zephyr/pm/pm.h>

/* 系统进入空闲时自动调用 */
void pm_state_set(enum pm_state state, uint8_t substate_id)
{
    switch (state) {
    case PM_STATE_SUSPEND_TO_IDLE:
        /* 浅睡眠 */
        SysTick->CTRL &= ~SysTick_CTRL_ENABLE_Msk;
        __WFI();
        break;

    case PM_STATE_SUSPEND_TO_RAM:
        /* 深度睡眠 (保留 RAM) */
        /* 配置唤醒源 (RTC、GPIO 等) */
        /* 关闭外设时钟，进入 SoC 特定深度睡眠模式 */
        /* 例如 nRF: NRF_POWER->SYSTEMOFF = 1; */
        /* 例如 STM32: HAL_PWR_EnterSTOPMode(...); */
        break;

    default:
        break;
    }
}

/* 唤醒后恢复 */
void pm_state_exit_post_ops(enum pm_state state, uint8_t substate_id)
{
    switch (state) {
    case PM_STATE_SUSPEND_TO_IDLE:
        /* 恢复 SysTick */
        SysTick->CTRL |= SysTick_CTRL_ENABLE_Msk;
        break;
    case PM_STATE_SUSPEND_TO_RAM:
        /* 全系统恢复: 重新初始化时钟、恢复外设状态 */
        /* SoC 特定实现，例如恢复 PLL、重新配置 Flash 时序等 */
        break;
    }
}
```

### 15.3 自定义约束

```c
#include <zephyr/pm/pm.h>

/* 强制系统进入指定状态 */
pm_state_force(0, &(struct pm_state_info){
    .state = PM_STATE_SUSPEND_TO_IDLE,
    .substate_id = 0,
});

/* 注册/注销 PM 通知器 (在状态变化前/后得到通知) */
struct pm_notifier notifier = {
    .state_entry = my_state_entry_cb,
    .state_exit = my_state_exit_cb,
};
pm_notifier_register(&notifier);
/* ... */
pm_notifier_unregister(&notifier);
```

### 15.4 唤醒计数器

```c
// 记录唤醒源
struct pm_wakeup pm_wakeup;

void my_isr(const void *arg)
{
    if (/* 检测到唤醒 */) {
        pm_wakeup_record(&pm_wakeup);
    }
    /* 处理中断 */
}

void main(void)
{
    while (1) {
        /* 获取唤醒次数 */
        uint32_t wake_cnt = pm_wakeup_get_count(&pm_wakeup);
        printk("Woken up %u times\n", wake_cnt);
        k_sleep(K_FOREVER);
    }
}
```

---

## 16. 单线程模式：真正的裸机模式

### 16.1 概念

Zephyr 可以完全关闭多线程调度，退化为**裸机框架**：

```kconfig
# 禁用多线程 (需要 ARCH_HAS_SINGLE_THREAD_SUPPORT=y)
# 支持此选项的架构: ARM Cortex-M, ARC, RISC-V, x86
# 此时 Zephyr 没有调度器、没有上下文切换
# 只有设备初始化 + 中断 + 硬件抽象
```

### 16.2 单线程模式下的启动流程

```
Reset → z_prep_c → z_cstart
  ├─ EARLY 设备初始化
  ├─ PRE_KERNEL_1 设备初始化
  ├─ PRE_KERNEL_2 设备初始化
  ├─ 跳过 prepare_multithreading() (因 CONFIG_MULTITHREADING=n)
  └─ 直接调用 bg_thread_main()
       ├─ POST_KERNEL 设备初始化
       ├─ APPLICATION 设备初始化
       └─ main() ← 直接调用 (非线程切换)
```

```c
// kernel/init.c - 单线程路径简化示意
static void bg_thread_main(void *unused1, void *unused2, void *unused3)
{
    z_sys_post_kernel = true;

    /* === 这两阶段在单线程模式下同样会执行 === */
    z_sys_init_run_level(INIT_LEVEL_POST_KERNEL);
    z_sys_init_run_level(INIT_LEVEL_APPLICATION);

    /* === 直接调用 main() === */
    (void)main();

    /* main 返回后，进入无限循环 */
    irq_lock();
    while (true) {
        __WFI();
    }
}
```

### 16.3 单线程模式的使用场景

- **超级循环 (Super Loop)** 架构
- **Bootloader** — 不需要 RTOS
- **极简传感器节点** — 中断驱动的采集
- **安全关键系统** — 避免调度不确定性
- **裸机迁移过渡** — 逐步引入 RTOS 特性

### 16.4 单线程 + 中断驱动示例

```c
// prj.conf
CONFIG_MULTITHREADING=n
CONFIG_GPIO=y
CONFIG_SERIAL=y

// src/main.c
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>

static const struct device *gpio;
static volatile bool flag;

void button_isr(const struct device *dev,
                struct gpio_callback *cb,
                uint32_t pins)
{
    flag = true;  // 设置标志，主循环中轮询
}

int main(void)
{
    gpio = DEVICE_DT_GET(DT_NODELABEL(gpio0));
    gpio_pin_configure(gpio, 13, GPIO_INPUT);
    gpio_pin_interrupt_configure(gpio, 13, GPIO_INT_EDGE_RISING);

    struct gpio_callback cb;
    gpio_init_callback(&cb, button_isr, BIT(13));
    gpio_add_callback(gpio, &cb);

    while (1) {
        if (flag) {
            flag = false;
            printk("Button pressed!\n");
        }
        __WFI();  // 等待中断 (低功耗)
    }
    return 0;
}
```

### 16.5 单线程模式下的 API 可用性

| API 类别 | 多线程模式 | 单线程模式 |
|---------|-----------|-----------|
| 设备驱动 (GPIO/UART/SPI/I2C) | ✅ | ✅ |
| 中断 (IRQ_CONNECT) | ✅ | ✅ |
| `k_sem_give()` | ✅ | ✅ |
| `k_sem_take()` | ✅ | ✅ (不阻塞，仅轮询) |
| `k_mutex_lock()` | ✅ | ✅ (始终立即获得) |
| `k_msgq_put/get()` | ✅ | ✅ (不阻塞) |
| `k_thread_create()` | ✅ | ❌ |
| `k_sleep()` / `k_sem_take(K_FOREVER)` | ✅ | ❌ (永远阻塞) |
| 调度相关 | ✅ | ❌ |

---

## 17. MPU/MMU 内存保护底层

### 17.1 ARM v7m/v8m MPU

```c
// 直接操作 MPU 寄存器
void mpu_configure_region(uint32_t region, uint32_t base, uint32_t attr)
{
    /* 确保 MPU 禁用 */
    MPU->CTRL = 0;

    /* 配置区域 */
    MPU->RBAR = base | region | MPU_RBAR_VALID_Msk;
    MPU->RASR = attr;

    /* 启用 MPU */
    MPU->CTRL = MPU_CTRL_ENABLE_Msk | MPU_CTRL_PRIVDEFENA_Msk;
    __DSB();
    __ISB();
}

// Zephyr MPU 区域属性
#define MPU_ATTR_FLASH_REGION \
    (MPU_RASR_AP_RO_USER_RO | MPU_RASR_XN_Msk | \
     MPU_RASR_CACHE_NONE | MPU_RASR_SIZE_1M | MPU_RASR_ENABLE_Msk)
```

### 17.2 Zephyr 内存域 (Memory Domain)

```c
/* 定义分区 */
K_APPMEM_PARTITION_DEFINE(sensor_part);
K_APP_DMEM(sensor_part) volatile int16_t sensor_data;

/* 定义内存域 */
struct k_mem_domain dom;
k_mem_domain_init(&dom, 0, NULL);

/* 分区添加到域 */
k_mem_domain_add_partition(&dom, &sensor_part);

/* 线程分配到域 */
k_mem_domain_add_thread(&dom, my_thread_id);
```

### 17.3 用户空间 (User Mode)

```c
// 系统调用实现
void syscall_handler(k_thread_t *thread, uintptr_t arg1,
                      uintptr_t arg2, uintptr_t ret)
{
    /* 验证参数来自用户空间 */
    if (!z_syscall_cross_check(thread, arg1)) {
        z_fatal_error(K_ERR_KERNEL_OOPS, thread);
        return;
    }

    /* 执行实际操作（提升到内核权限） */
    *ret = do_real_work((void *)arg1);
}
```

### 17.4 MPU 编程模型对比

```
无 MPU (CONFIG_MPU=n):
  所有代码 → 所有内存: 无限制访问

有 MPU (CONFIG_MPU=y):
  内核空间 → 全部内存: 完全访问
  用户空间 → 仅授权区域: 受限访问
  访问违规 → MemManage Fault: 硬件捕获
```

---

## 18. 工具链与编译选项

### 18.1 支持的 ARM 工具链

| 工具链 | 描述 | 配置 |
|--------|------|------|
| `zephyr` | Zephyr SDK (默认) | `ZEPHYR_SDK_INSTALL_DIR` |
| `gnuarmemb` | ARM GNU Embedded | `GNUARMEMB_TOOLCHAIN_PATH` |
| `armclang` | ARM Compiler 6 | `ARMCLANG_TOOLCHAIN_PATH` |
| `llvm` | LLVM/clang 原生 | `ZEPHYR_TOOLCHAIN=llvm` |

### 18.2 链接优化

```cmake
# CMakeLists.txt
target_link_options(app PRIVATE
    -Wl,--gc-sections       # 删除未引用段
    -Wl,-Map=zephyr.map     # 生成映射文件
    -specs=nano.specs       # 使用 newlib-nano
    -u_printf_float         # 启用浮点数打印
)
```

### 18.3 自定义编译标志

```kconfig
# 通过 Kconfig 控制
CONFIG_COMPILER_OPT="-O2 -ffast-math"
```

```cmake
# 或在 CMake 中设置
zephyr_cc_option(-ffunction-sections -fdata-sections)
zephyr_cc_option_ifdef(CONFIG_ARM_MPU -mno-unaligned-access)
```

### 18.4 生成文件分析

```bash
# 查看各段大小
arm-zephyr-eabi-size build/zephyr/zephyr.elf

# 符号分析
arm-zephyr-eabi-nm -S build/zephyr/zephyr.elf | sort -k2

# 反汇编
arm-zephyr-eabi-objdump -d build/zephyr/zephyr.elf > disasm.txt

# 查看内存占用
arm-zephyr-eabi-objdump -h build/zephyr/zephyr.elf
```

---

## 19. 调试与 Trace 机制

### 19.1 硬件调试

```bash
# 使用 west debug (OpenOCD)
west debug --runner openocd

# 或 pyOCD
west debug --runner pyocd

# 或 JLink
west debug --runner jlink
```

### 19.2 GDB 调试

```bash
# 启动 GDB 会话
west debug --runner openocd --gdb

# 或手动:
arm-zephyr-eabi-gdb build/zephyr/zephyr.elf
(gdb) target remote :3333
(gdb) monitor reset halt
(gdb) b main
(gdb) continue

# 查看启动过程
(gdb) b z_cstart
(gdb) b z_arm_reset
(gdb) info registers
(gdb) monitor reg r0
```

### 19.3 日志与 Trace

```kconfig
# 控制台日志
CONFIG_LOG=y
CONFIG_LOG_DEFAULT_LEVEL=3    # 0=OFF, 1=ERR, 2=WRN, 3=INF, 4=DBG

# 早期串口输出 (在设备初始化前)
CONFIG_EARLY_CONSOLE=y
CONFIG_EARLY_PRINTK=y

# 函数追踪
CONFIG_TRACING=y
CONFIG_TRACING_CTF=y           # CTF 格式 (需收集器)
CONFIG_TRACING_SHELL=y
```

### 19.4 硬件 Trace 支持 (ETM/ITM)

```kconfig
CONFIG_ITM=y                  # Instrumentation Trace Macrocell
CONFIG_ETM=y                  # Embedded Trace Macrocell (Cortex-M3/4/7)
```

```c
// ITM 直接输出 (极低开销)
void itm_putchar(char c)
{
    ITM->PORT[0].u32 = c;
}

// 使用 DWT_CYCCNT (Data Watchpoint Trace Cycle Counter) 做时间测量
// DWT 基址 0xE0001000, CYCCNT 偏移 0x04
#define DWT_CYCCNT  (*((volatile uint32_t *)0xE0001004))
// 使能 DWT_CYCCNT (需要先使能 DWT)
CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

uint32_t start = DWT_CYCCNT;
/* 被测量代码 */
uint32_t elapsed = DWT_CYCCNT - start;
```

---

## 20. 附录：关键文件索引

### 20.1 启动与初始化

| 文件 | 用途 |
|------|------|
| `arch/arm/core/cortex_m/reset.S` | Reset handler 汇编 |
| `arch/arm/core/cortex_m/prep_c.c` | C 运行时初始化 |
| `arch/arm/core/cortex_m/vector_table.S` | 中断向量表 |
| `arch/arm/core/cortex_m/isr_wrapper.S` | 中断包装器 |
| `arch/arm/core/cortex_m/systick.c` | SysTick 驱动 |
| `arch/arm/core/cortex_m/tickless.c` | Tickless 实现 |
| `arch/arm/core/cortex_m/fault.c` | 异常处理 |
| `kernel/init.c` | 内核初始化 (z_cstart) |
| `kernel/sched.c` | 调度器 |
| `kernel/sem.c` | 信号量 |
| `kernel/mutex.c` | 互斥量 |
| `kernel/msg_q.c` | 消息队列 |
| `kernel/queue.c` | 队列 |
| `kernel/pipe.c` | 管道 |
| `kernel/event.c` | 事件 |
| `kernel/timer.c` | 定时器 |
| `kernel/timeout.c` | 超时管理 |

### 20.2 硬件抽象与驱动框架

| 文件 | 用途 |
|------|------|
| `include/zephyr/device.h` | 设备模型头文件 |
| `include/zephyr/init.h` | SYS_INIT 宏定义 |
| `include/zephyr/kernel.h` | 内核 API 头文件 |
| `include/zephyr/sys/atomic.h` | 原子操作 |
| `include/zephyr/sys/__assert.h` | 断言 |
| `include/zephyr/sys/printk.h` | 调试输出 |
| `include/zephyr/drivers/gpio.h` | GPIO API 定义 |
| `include/zephyr/drivers/uart.h` | UART API 定义 |
| `include/linker/linker-defs.h` | 链接脚本符号定义 |
| `include/linker/sections.h` | 自定义段定义 |

### 20.3 设备树与配置

| 文件 | 用途 |
|------|------|
| `scripts/dts/gen_defines.py` | DTS → C 头文件生成器 |
| `dts/bindings/` | 设备树绑定定义 |
| `dts/common/` | 通用设备树包含文件 |
| `cmake/modules/dts.cmake` | 设备树 CMake 模块 |
| `cmake/modules/kconfig.cmake` | Kconfig CMake 模块 |
| `scripts/kconfig/` | Kconfig 处理工具 |

### 20.4 板级与 SoC

| 文件 | 用途 |
|------|------|
| `boards/arm/nrf52840dk_nrf52840/nrf52840dk_nrf52840.dts` | 板级 DTS 示例 |
| `boards/arm/nrf52840dk_nrf52840/nrf52840dk_nrf52840.yaml` | 板级元信息 |
| `boards/arm/nrf52840dk_nrf52840/nrf52840dk_nrf52840.defconfig` | 板级默认配置 |
| `soc/arm/nordic_nrf/nrf52/soc.c` | SoC 初始化 |
| `soc/arm/nordic_nrf/nrf52/Kconfig` | SoC 配置 |

---

## 附录：快速参考卡片

### 启动各阶段 Hook 汇总

```text
soc_early_reset_hook()      — [汇编] reset.S 最顶部，无栈时，可接管系统
soc_reset_hook()            — [汇编] reset.S 中，MSP 就绪后
soc_prep_hook()             — [C] z_prep_c() 第一件事
soc_early_init_hook()       — [C] z_cstart(), PRE_KERNEL_1 前
board_early_init_hook()     — [C] z_cstart(), PRE_KERNEL_1 前
SYS_INIT(fn, LEVEL, prio)   — [C] 按 LEVEL/PRIO 顺序调用
soc_late_init_hook()        — [C] bg_thread_main(), POST_KERNEL 后
board_late_init_hook()      — [C] bg_thread_main(), POST_KERNEL 后
```

### 常用 Kconfig 开关

| 配置 | 默认 | 说明 |
|------|------|------|
| `MULTITHREADING` | y | 关闭则退化为裸机 |
| `TICKLESS_KERNEL` | y | 低功耗 Tickless |
| `ZERO_LATENCY_IRQS` | n | 零延迟中断 |
| `MPU` | n | 内存保护单元 |
| `USERSPACE` | n | 用户/内核空间隔离 |
| `INIT_ARCH_HW_AT_BOOT` | n | 复位时重初始化硬件 |
| `EARLY_CONSOLE` | n | 设备初始化前串口输出 |
| `HEAP_MEM_POOL_SIZE` | 0 | 系统堆大小 (0=禁用) |

### 链接脚本关键符号

| 符号 | 含义 |
|------|------|
| `_vector_start / _vector_end` | 向量表起止 |
| `__bss_start / __bss_end` | BSS 段 |
| `__data_rom_start / __data_ram_start` | .data 拷贝源/目标 |
| `_main_stack` | 主栈起始地址 |
| `_interrupt_stack` | 中断栈起始地址 |
| `__init_*_start / __init_*_end` | 各初始化级别函数表 |
| `_sw_isr_table` | 软件中断表基址 |

---

> **参考资源**
>
> - Zephyr 官方文档: https://docs.zephyrproject.org/
> - Zephyr 源码树: https://github.com/zephyrproject-rtos/zephyr
> - ARM Cortex-M 参考手册 (ARMv7-M / ARMv8-M Architecture Reference Manual)
