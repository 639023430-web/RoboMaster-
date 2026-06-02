# Zephyr RTOS 实时操作系统核心机制详解

> 本文档深入剖析 Zephyr RTOS 内核中与实时性相关的所有核心机制，包括调度策略、同步原语、IPC、内存管理、中断处理等。适合嵌入式 RTOS 开发者阅读。
> 
> **版本说明**: 本文档基于 Zephyr RTOS 3.x，API 以主线稳定版为准。

---

## 目录

1. [Zephyr RTOS 概述](#1-zephyr-rtos-概述)
2. [线程模型与调度](#2-线程模型与调度)
3. [信号量 (Semaphore)](#3-信号量-semaphore)
4. [互斥量 (Mutex) 与优先级继承](#4-互斥量-mutex-与优先级继承)
5. [消息队列 (Message Queue)](#5-消息队列-message-queue)
6. [邮箱 (Mailbox)](#6-邮箱-mailbox)
7. [栈 (Stack)](#7-栈-stack)
8. [FIFO (先进先出) k_fifo](#8-fifo-先进先出-k_fifo)
9. [LIFO (后进先出) k_lifo](#9-lifo-后进先出-k_lifo)
10. [队列 (Queue)](#10-队列-queue)
11. [管道 (Pipe) k_pipe](#11-管道-pipe-k_pipe)
12. [条件变量 (Condition Variable)](#12-条件变量-condition-variable)
13. [事件对象 (Event)](#13-事件对象-event)
14. [定时器 (Timer)](#14-定时器-timer)
15. [中断管理 (Interrupts)](#15-中断管理-interrupts)
16. [内存管理](#16-内存管理)
17. [原子操作与自旋锁](#17-原子操作与自旋锁)
18. [轮询 API (Polling)](#18-轮询-api-polling)
19. [多核处理 (SMP)](#19-多核处理-smp)
20. [时间管理与 Tick](#20-时间管理与-tick)
21. [工作队列 (Workqueue)](#21-工作队列-workqueue)
22. [线程安全与协作示例](#22-线程安全与协作示例)

---

## 1. Zephyr RTOS 概述

Zephyr 是一个面向资源受限设备的实时操作系统（RTOS），由 Linux 基金会托管。其内核具备以下实时特性：

- **确定性调度** — 完全抢占式、基于优先级的调度器
- **丰富的同步原语** — 信号量、互斥量、条件变量、事件
- **灵活的消息传递** — 消息队列、邮箱、管道
- **低延迟中断** — 中断处理不经过内核调度点（零延迟中断模型）
- **Tickless 空闲** — 空闲时停止系统 Tick 以减少功耗
- **SMP 支持** — 对称多处理，任务可在多核间迁移

---

## 2. 线程模型与调度

### 2.1 线程优先级

Zephyr 使用**数值越小优先级越高**的规则：

| 类型 | 范围 | 说明 |
|------|------|------|
| 协作线程 | `-CONFIG_NUM_COOP_PRIORITIES` 到 `-1` | 不被抢占，除非主动 yield 或阻塞 |
| 抢占线程 | `0` 到 `CONFIG_NUM_PREEMPT_PRIORITIES - 1` | 可被更高优先级线程随时抢占 |
| 空闲线程 | `CONFIG_NUM_PREEMPT_PRIORITIES` | 最低优先级，无工作时运行 |

```c
// 创建线程示例
#define MY_STACK_SIZE 1024                      // 定义线程栈大小为1024字节
K_THREAD_STACK_DEFINE(my_stack, MY_STACK_SIZE); // 分配线程栈空间
struct k_thread my_thread;                      // 声明线程控制块

k_thread_create(&my_thread, my_stack, MY_STACK_SIZE,  // 创建线程：传入控制块、栈和栈大小
                thread_entry, NULL, NULL, NULL,        // 线程入口函数及三个参数（均为NULL）
                5, 0, K_NO_WAIT);                      // 优先级5，不延时，立即加入就绪队列
```

### 2.2 调度算法

Zephyr 支持四种调度算法，通过 Kconfig 选择：

| 算法 | Kconfig | 特点 |
|------|---------|------|
| 简单优先级调度 | `CONFIG_SCHED_DUMB` | 选择就绪队列中最高优先级线程运行，同优先级轮转，复杂度 O(n) |
| 多队列调度 | `CONFIG_SCHED_MULTIQ` | 每个优先级独立队列，调度复杂度 O(1) |
| 可扩展调度 | `CONFIG_SCHED_SCALABLE` | 基于位图搜索，兼顾性能和灵活性（多数平台的默认选择） |
| 截止时间调度 (EDF) | `CONFIG_SCHED_DEADLINE` | 根据截止时间选择下一个运行线程 |

```c
// 时间片轮转（同优先级线程间）
k_sched_time_slice_set(SLICE_MS, SLICE_THREAD_PRIO);  // 设置时间片长度（毫秒）及适用线程优先级
```

### 2.3 线程状态机

```
                    ┌──────────────┐
                    │   New (创建)  │
                    └──────┬───────┘
                           │ k_thread_start()
                           v
                 ┌───────────────────┐
          ┌─────▶│   Ready (就绪)    │◀────┐
          │      └────────┬──────────┘     │
          │               │ 调度选中        │ 资源可用/超时
          │               v                │
          │      ┌───────────────────┐     │
          │      │  Running (运行)   │─────┘
          │      └────────┬──────────┘ 被抢占(yield/时间片到)
          │               │ 阻塞(k_sem_take等)
          │               v
          │      ┌───────────────────┐
          └──────│  Waiting (等待)   │
                 └───────────────────┘
```

---

## 3. 信号量 (Semaphore)

### 3.1 原理

信号量是一个非负整数计数器，支持 **take**（P 操作）和 **give**（V 操作）两个原子操作。

**适用场景**：
- 任务同步（生产者-消费者）
- 资源计数（有限资源池管理）
- ISR 通知线程

### 3.2 API

```c
// 定义
K_SEM_DEFINE(my_sem, 0, 10);   // 初始值0，最大值10

// 或动态定义
struct k_sem my_sem;                    // 声明信号量结构体
k_sem_init(&my_sem, 0, 10);             // 动态初始化信号量，初始值0，上限10

// 获取信号量（P操作 —— 减1，如为0则阻塞）
k_sem_take(&my_sem, K_MSEC(100));     // 带超时等待（100毫秒）
k_sem_take(&my_sem, K_FOREVER);       // 无限等待，直到获得信号量
k_sem_take(&my_sem, K_NO_WAIT);       // 不等待，立即返回

// 释放信号量（V操作 —— 加1，唤醒等待者）
k_sem_give(&my_sem);                    // 释放信号量，唤醒一个等待线程

// 复位信号量（清零）
k_sem_reset(&my_sem);                   // 将信号量计数值复位为0

// 查询信号量计数值
unsigned int count = k_sem_count_get(&my_sem);  // 获取当前信号量计数值
```

### 3.3 典型用法：生产者-消费者

```c
#define BUFFER_SIZE 5                                     // 缓冲池容量

K_SEM_DEFINE(sem_empty, BUFFER_SIZE, BUFFER_SIZE);        // 空槽位信号量（初始=缓冲池大小）
K_SEM_DEFINE(sem_filled, 0, BUFFER_SIZE);                 // 已填槽位信号量（初始=0，无数据）

void producer(void *a, void *b, void *c)                  // 生产者线程入口函数
{
    while (1) {
        k_sem_take(&sem_empty, K_FOREVER);                // 等待一个空槽位
        /* 生产数据，放入缓冲池 */
        k_sem_give(&sem_filled);                           // 通知消费者：有新数据可用
    }
}

void consumer(void *a, void *b, void *c)                  // 消费者线程入口函数
{
    while (1) {
        k_sem_take(&sem_filled, K_FOREVER);               // 等待一个数据
        /* 消费数据，从缓冲池取出 */
        k_sem_give(&sem_empty);                            // 通知生产者：释放一个空槽位
    }
}
```

### 3.4 ISR 中的信号量

Zephyr 允许在 ISR 中调用 `k_sem_give()`。ISR 释放信号量时，如果等待的线程优先级高于当前被中断的线程，`k_sem_give` 会触发上下文切换（在退出 ISR 后执行）。

```c
void my_isr(const void *arg)   // ISR中断服务函数
{
    /* ISR 上下文中释放信号量 */
    k_sem_give(&my_sem);        // 在ISR中释放信号量，唤醒等待线程
    /* 标记需要重新调度 */
}
```

> **注意**: `k_sem_take()` 不能在 ISR 中使用（可能导致阻塞）。

### 3.5 信号量内部结构

```c
struct k_sem {
    _WAIT_Q_FUNC(wait_q);         // 等待队列（按优先级排序）
    struct sys_snode_t poll_events;  // 轮询事件链表节点
    unsigned int count;           // 当前计数值
    unsigned int limit;           // 最大值
};
```

---

## 4. 互斥量 (Mutex) 与优先级继承

### 4.1 Mutex 与 Semaphore 的区别

| 特性 | Mutex | Semaphore |
|------|-------|-----------|
| 所有权 | 有（只能由持有者释放） | 无（任何线程/ISR 均可 give） |
| 优先级继承 | **支持** | 不支持 |
| 递归锁定 | 支持（同一线程可重复 take） | N/A |
| 初始值 | 始终为 1（二进制） | 可配置 |
| ISR 中使用 | ❌ 不可 | ✅ 可 give |

### 4.2 API

```c
// 定义
K_MUTEX_DEFINE(my_mutex);               // 静态定义互斥量

// 或动态定义
struct k_mutex my_mutex;                // 声明互斥量结构体
k_mutex_init(&my_mutex);                 // 动态初始化互斥量

// 锁定
k_mutex_lock(&my_mutex, K_FOREVER);     // 阻塞等待，直到获得互斥量
k_mutex_lock(&my_mutex, K_MSEC(100));   // 带超时等待互斥量（100毫秒）

// 解锁
k_mutex_unlock(&my_mutex);              // 释放互斥量
```

### 4.3 优先级反转 (Priority Inversion) 与优先级继承

#### 优先级反转问题

```
高优先级任务 H ────────┬──── 等待释放 Mutex ────▶ 被低优 L 阻塞
                      │
中优先级任务 M   ──────┼───────────────▶ 抢占 L，继续运行
                      │
低优先级任务 L   ──────┴───▶ 持有 Mutex ──▶ 被 M 抢占，无法释放 Mutex
 时间 ▶
```

**结果**: 高优先级任务 H 被非直接相关的中优先级任务 M 间接阻塞，响应时间不可预测——这是纯优先级抢占系统中最严重的实时问题之一。

#### Zephyr 的优先级继承

Zephyr 对每个 mutex 实现了**优先级继承协议**：

1. 当高优先级线程 H 尝试获取被低优先级线程 L 持有的 mutex 时
2. 内核**暂时将 L 的优先级提升至与 H 相同**
3. L 释放 mutex 后，其优先级恢复为原始值
4. 这样一来，中优先级 M 无法再抢占 L，H 的阻塞时间被限制在 L 的临界区执行时间内

```
H ────────┬── 等待 Mutex ──────────▶ 获得 Mutex ▶ 运行
          │                          ▲  释放
L ────┬───┤──▶ 持有 Mutex ──────▶  │
      │   │        (优先级从低→高)   │  (优先级恢复)
      │   └─── 内核提升 L 优先级 ───┘
M ────┼────────────────────────────────── 无法再抢占 L
      │
      │  ◀── L 临界区较短，H 快速获得释放
```

```c
K_MUTEX_DEFINE(lock);                       // 定义互斥量

void low_prio_thread(void *a, void *b, void *c)   // 低优先级线程入口
{
    k_mutex_lock(&lock, K_FOREVER);                // 获取互斥量，此时若高优线程等待则触发优先级继承
    /* 临界区：执行期间，若被高优线程等待，优先级被提升 */
    k_busy_wait(1000);                             // 模拟临界区耗时操作（忙等待1000微秒）
    k_mutex_unlock(&lock);  /* 此处优先级恢复原值 */
}

void high_prio_thread(void *a, void *b, void *c)   // 高优先级线程入口
{
    k_mutex_lock(&lock, K_FOREVER);  /* 触发 L 的优先级继承 */
    /* 临界区 */
    k_mutex_unlock(&lock);                        // 释放互斥量
}
```

> **注**: Zephyr Mutex 的优先级继承机制是**内置且始终启用**的，无需额外配置。`CONFIG_PRIORITY_CEILING` 控制的是优先级天花板协议，是优先级继承的增强补充（默认启用）。

### 4.4 递归互斥量

Zephyr 的 mutex 支持递归锁定——同一线程可以多次锁定同一个 mutex，但解锁次数必须匹配：

```c
void recursive_example(void)   // 递归互斥量示例函数
{
    k_mutex_lock(&my_mutex, K_FOREVER);          // 第1次锁定互斥量（进入第1层临界区）
    /* 临界区第 1 层 */
    k_mutex_lock(&my_mutex, K_FOREVER);  /* 递归锁定，成功 */
    /* 临界区第 2 层 */
    k_mutex_unlock(&my_mutex);                   // 退出第2层临界区，解锁一次
    k_mutex_unlock(&my_mutex);                   // 退出第1层临界区，完全释放互斥量
}
```

---

## 5. 消息队列 (Message Queue)

### 5.1 原理

消息队列是**内核管理的固定大小消息缓冲区**。线程可将消息发送到队列，也可从队列接收消息。当队列满或空时，线程可选择阻塞等待。

**特点**：
- 每个消息是数据**副本**（传递值，非引用）
- 消息大小和队列深度在编译时确定
- 支持 ISR 发送（但不能在 ISR 接收）
- 通过链表管理等待者

### 5.2 API

```c
// 定义消息队列：消息大小 32 字节，队列深度 10
K_MSGQ_DEFINE(my_msgq, 32, 10, 4);     // 静态定义消息队列（消息大小32字节，容量10条，对齐4字节）

// 发送
void send_data(void)                     // 消息发送函数
{
    const char *data = "Hello Zephyr";   // 待发送的数据
    k_msgq_put(&my_msgq, data, K_FOREVER);         // 阻塞发送（队列满则等待）
    k_msgq_put(&my_msgq, data, K_NO_WAIT);          // 非阻塞发送（队列满立即返回）
    k_msgq_put(&my_msgq, data, K_MSEC(50));         // 超时发送（等待最多50毫秒）
}

// 接收
void receive_data(void)                  // 消息接收函数
{
    char buf[32];                        // 接收缓冲区
    k_msgq_get(&my_msgq, buf, K_FOREVER);           // 阻塞接收（队列空则等待）
    k_msgq_get(&my_msgq, buf, K_NO_WAIT);           // 非阻塞接收（队列空立即返回）
}

// 查询与清空
unsigned int count = k_msgq_num_free_get(&my_msgq); // 查询空闲空间数
unsigned int used  = k_msgq_num_used_get(&my_msgq); // 查询已用消息数
k_msgq_purge(&my_msgq);                             // 清空队列中所有消息
```

### 5.3 多发送者-多接收者场景

```c
K_MSGQ_DEFINE(cmd_queue, sizeof(struct command), 20, 4);  // 定义命令消息队列，容量20条命令

struct command {                        // 命令消息结构体
    uint8_t  type;                      // 命令类型
    uint8_t  data[16];                  // 命令数据（16字节）
};

void sender_thread(void *a, void *b, void *c)   // 发送者线程入口
{
    struct command cmd = { .type = 1, .data = {0} };  // 初始化命令：类型为1，数据清零
    while (1) {
        k_msgq_put(&cmd_queue, &cmd, K_FOREVER);  // 将命令放入消息队列（满则阻塞等待）
        k_sleep(K_SECONDS(1));                     // 每秒发送一条命令
    }
}

void receiver_thread(void *a, void *b, void *c)   // 接收者线程入口
{
    struct command cmd;                            // 接收缓冲区
    while (1) {
        k_msgq_get(&cmd_queue, &cmd, K_FOREVER);  // 从消息队列接收命令（空则阻塞等待）
        /* 处理命令 */
    }
}
```

### 5.4 消息队列内部结构

```c
struct k_msgq {
    _WAIT_Q_FUNC(wait_q);          // 等待线程队列（发送/接收等待者）
    struct sys_snode_t poll_events;    // 轮询事件链表节点
    char *ring_buf;                // 环形缓冲区（存储消息数据）
    uint32_t msg_size;             // 单条消息大小（字节）
    uint32_t max_msgs;             // 最大消息数（队列容量）
    uint32_t used_msgs;            // 当前消息数（已使用数）
    uint32_t flags;                // 标志位
};
```

---

## 6. 邮箱 (Mailbox)

### 6.1 原理

邮箱提供了一种**同步消息传递**机制，发送者和接收者可以**直接交换数据**，允许可变长度的消息。相比于消息队列，邮箱更灵活但开销更大。

**关键特性**：
- **同步语义**：发送者可以等待接收者确认
- **可变长度消息**：通过链表链接消息块
- **一对一、一对多、多对一通信**
- **支持 TX 描述符**：直接传递数据而不复制

### 6.2 API

```c
// 定义邮箱
K_MBOX_DEFINE(my_mbox);                 // 静态定义邮箱

struct k_mbox_msg send_msg, recv_msg;   // 声明发送和接收消息描述符

void sender(void)                        // 发送者函数
{
    char *data = "important data";       // 待发送的数据

    /* 发送消息（阻塞直到被接收） */
    send_msg.info = 0;              // 自定义信息值（如消息类型）
    send_msg.size = strlen(data) + 1;    // 数据大小（包含字符串结尾空字符）
    send_msg.tx_data = data;             // 指向待发送数据的指针
    send_msg.tx_block.data = NULL;       // 不使用内存块方式传递数据
    send_msg.tx_target_thread = K_ANY;   // 允许任意线程接收
    /* 发送方不设置 rx_source_thread，留待内核填充实际接收方地址 */

    k_mbox_put(&my_mbox, &send_msg, K_FOREVER);  // 发送消息，阻塞直到被接收
}

void receiver(void)                      // 接收者函数
{
    char buffer[64];                     // 接收数据缓冲区
    recv_msg.info = sizeof(buffer); // 告诉发送方缓冲区大小
    recv_msg.size = sizeof(buffer);      // 设置接收缓冲区大小
    recv_msg.rx_source_thread = K_ANY;   // 允许从任意发送线程接收

    /* 接收消息（阻塞直到有发送方），buffer 参数指定数据拷贝目标 */
    k_mbox_get(&my_mbox, &recv_msg, buffer, K_FOREVER);  // 阻塞接收消息，数据拷贝到buffer

    /* 接收完成后 recv_msg.info = 发送方的 info 值，recv_msg.size = 实际接收字节数 */
    printk("Received: %s\n", buffer);    // 打印接收到的消息
}
```

### 6.3 邮箱 VS 消息队列

| 特性 | 邮箱 (Mailbox) | 消息队列 (MsgQ) |
|------|----------------|-----------------|
| 消息长度 | 可变（不限） | 固定 |
| 同步性 | 同步（发送者可等待确认） | 异步 |
| 数据传递 | 引用传递或复制 | 值传递（复制） |
| 性能 | 较低（更灵活） | 较高（固定大小） |
| 使用复杂度 | 高 | 低 |

---

## 7. 栈 (Stack)

### 7.1 原理

Zephyr 的栈是 **LIFO（后进先出）** 数据结构，用于在线程和 ISR 之间传递**指针**（而非数据副本）。

### 7.2 API

```c
// 定义栈
K_STACK_DEFINE(my_stack, 10);     // 容量 10（存储 10 个指针）

// 或动态定义
struct k_stack my_stack;            // 声明栈对象
k_stack_init(&my_stack, stack_buf, STACK_SIZE);  // 初始化栈，指定存储缓冲区与容量

// 推入指针
void *data = k_malloc(64);                    // 从系统堆分配 64 字节内存块（返回NULL表示失败）
k_stack_push(&my_stack, (stack_data_t)data);  // 将数据指针推入栈中

// 弹出指针（阻塞等待）
void *popped;                         // 定义接收弹出指针的变量
k_stack_pop(&my_stack, &popped, K_FOREVER);  // 从栈弹出指针，若无数据则永久阻塞等待

// ISR 中推入
void my_isr(void)                     // 中断服务函数
{
    k_stack_push(&my_stack, (stack_data_t)some_data);  // 中断中向栈推入数据指针
}
```

### 7.3 典型场景：内存池回收

```c
K_STACK_DEFINE(free_blocks, 20);     // 定义栈，容量 20 个指针，用于管理空闲内存块

/* 生产者线程释放内存块 */
void producer(void)                   // 生产者线程函数
{
    void *block = allocate_buffer();  // 分配一个缓冲区
    /* 使用... */
    k_stack_push(&free_blocks, (stack_data_t)block);  // 将释放的内存块指针推入空闲栈
}

/* 消费者线程获取已释放的块 */
void consumer(void)                   // 消费者线程函数
{
    void *block;                      // 定义接收内存块指针的变量
    k_stack_pop(&free_blocks, &block, K_FOREVER);  // 从空闲栈取出一个内存块，若无则阻塞
    k_free(block);                    // 释放该内存块
}
```

---

## 8. FIFO (先进先出) k_fifo

### 8.1 原理

FIFO（First In, First Out）是 Zephyr 提供的一种**先进先出**数据传递机制，通过 `k_fifo` API 实现在线程和 ISR 之间传递**数据指针**。

**与 k_queue 的关系**：`k_fifo` 基于 `k_queue` 实现，但提供更简洁的 FIFO-only 语义。Zephyr 官方文档将其列为独立的数据传递对象。

**适用场景**：
- 生产者-消费者（严格按顺序处理）
- 任务间有序数据传输
- ISR 向线程传递数据

### 8.2 API

```c
// 定义 FIFO
K_FIFO_DEFINE(my_fifo);             // 定义 FIFO 对象

// 手动定义链表节点
struct data_item {                  // 自定义数据结构，包含链表节点
    sys_snode_t node;               // 内核链表节点，用于挂入队列
    uint32_t value;                 // 用户数据字段
};

// 发送数据（追加到队尾）
void sender(void)                   // 发送者线程函数
{
    struct data_item *item = k_malloc(sizeof(struct data_item));  // 动态分配数据项
    item->value = 100;              // 设置数据值
    k_fifo_put(&my_fifo, (struct data_item *)item);  // 将数据项追加到 FIFO 队尾
    /* 底层等价于：k_queue_append(&my_fifo._queue, (sys_snode_t *)item); */
}

// 接收数据（从队首取出，可阻塞）
void receiver(void)                 // 接收者线程函数
{
    struct data_item *item;         // 定义接收数据项的指针
    item = k_fifo_get(&my_fifo, K_FOREVER);  // 从 FIFO 队首取出数据项，无数据则永久阻塞
    if (item) {                     // 检查是否成功获取数据项
        printk("Got: %d\n", item->value);  // 打印接收到的数据
        k_free(item);               // 释放数据项内存
    }
}

// 非阻塞轮询
void poll_receiver(void)            // 非阻塞轮询接收函数
{
    struct data_item *item = k_fifo_get(&my_fifo, K_NO_WAIT);  // 非阻塞方式从 FIFO 获取数据
    if (item) {                     // 检查是否有数据
        k_free(item);               // 释放数据项内存
    }
}

// 如需查询操作，建议直接使用 k_queue（k_fifo 内部基于 k_queue 实现）
// 可通过非阻塞方式检查：
struct data_item *peek = k_fifo_get(&my_fifo, K_NO_WAIT);  // 非阻塞窥探
if (peek) {
    /* 有数据，处理完后记得返还或释放 */
    k_free(peek);
}
```

### 8.3 ISR 安全

```c
void my_isr(const void *arg)         // 中断服务函数，接收硬件参数
{
    struct data_item *item = fetch_from_hardware();  // 从硬件获取数据
    k_fifo_put(&my_fifo, item);   // ISR 中可发送数据到 FIFO
    // k_fifo_get 在 ISR 中只能使用 K_NO_WAIT
}
```

### 8.4 FIFO 内部结构

```c
struct k_fifo {                     // FIFO 结构体定义
    struct k_queue _queue;         // 基于 k_queue 实现，_queue 是内部队列对象
};
```

---

## 9. LIFO (后进先出) k_lifo

### 9.1 原理

LIFO（Last In, First Out）提供**后进先出**的数据传递方式，通过 `k_lifo` API 实现在线程和 ISR 之间传递**数据指针**。最后放入的数据最先被取出。

**与 k_stack / k_queue 的关系**：
- `k_lifo` 同样基于 `k_queue` 实现（尾部插入、头部取出 → 等效 LIFO 通过 prepend）
- `k_stack` 存储的是字大小的值（不是指针），而 `k_lifo` 存储任意指针
- `k_lifo` 是 Zephyr 官方独立的数据传递对象

**适用场景**：
- 最近最紧急的数据优先处理
- 中断嵌套场景
- 内存块回收的 LIFO 策略（缓存热度）

### 9.2 API

```c
// 定义 LIFO
K_LIFO_DEFINE(my_lifo);             // 定义 LIFO 对象

struct data_item {                  // 自定义数据结构，包含链表节点
    sys_snode_t node;               // 内核链表节点
    uint32_t value;                 // 用户数据值
};

// 发送数据（插入到队首）
void sender(void)                   // 发送者线程函数
{
    struct data_item *item = k_malloc(sizeof(struct data_item));  // 动态分配数据项
    item->value = 200;              // 设置数据值
    k_lifo_put(&my_lifo, item);     // 将数据项插入 LIFO 队首
    /* 底层等价于：k_queue_prepend(&my_lifo._queue, (sys_snode_t *)item); */
}

// 接收数据（从队首取出，后进先出）
void receiver(void)                 // 接收者线程函数
{
    struct data_item *item = k_lifo_get(&my_lifo, K_FOREVER);  // 从 LIFO 队首取出数据，后进先出
    if (item) {                     // 检查是否成功获取
        printk("Got: %d\n", item->value);  // 打印接收到的数据值
        k_free(item);               // 释放数据项内存
    }
}
```

### 9.3 LIFO VS Stack

| 特性 | LIFO (k_lifo) | Stack (k_stack) |
|------|---------------|-----------------|
| 数据类型 | 任意数据指针 | 仅 `uintptr_t` 值 |
| 底层实现 | 基于 `k_queue` | 基于数组 + 索引 |
| 容量 | 不限（内存管理） | 固定（编译时确定） |
| 后进先出 | ✅ | ✅ |
| 阻塞等待 | ✅ | ✅ |
| ISR 发送 | ✅ | ✅ |

---

## 10. 队列 (Queue)

### 10.1 原理

Zephyr 的队列 (`k_queue`) 是一个通用的**双向链表队列**，支持在**队首或队尾**插入数据节点，也支持在任意位置插入。队列中的数据节点需要包含一个 `sys_snode_t` 头。

**与 k_fifo / k_lifo 的关系**：
- `k_fifo` 和 `k_lifo` 都基于 `k_queue` 实现
- `k_queue` 本身提供更多操作：`k_queue_prepend`、`k_queue_append`、`k_queue_insert`
- 当需要更灵活的插入策略时使用 `k_queue`，否则优先用 `k_fifo` / `k_lifo`

### 10.2 API

```c
// 节点数据结构
struct data_item {                  // 自定义数据结构，包含链表节点
    sys_snode_t node;               // 内核链表节点，用于挂入队列
    uint32_t value;                 // 用户数据值
};

K_QUEUE_DEFINE(my_queue);           // 定义队列对象

// 发送节点（队尾追加）
void sender(void)                   // 发送者线程函数
{
    struct data_item *item = k_malloc(sizeof(struct data_item));  // 动态分配数据项
    item->value = 42;               // 设置数据值
    k_queue_append(&my_queue, &item->node);     // 将节点追加到队尾（利用内部嵌入的 sys_snode_t）
    // 或 k_queue_prepend(&my_queue, &item->node); // 插入队首
    // 或 k_queue_insert(&my_queue, NULL, &item->node); // 在指定位置插入（NULL=队首）
}

// 接收节点
void receiver(void)                 // 接收者线程函数
{
    void *data;                     // 定义通用指针接收数据
    data = k_queue_get(&my_queue, K_FOREVER);  // 阻塞接收（从队首取），无数据则永久等待
    struct data_item *item = (struct data_item *)data;  // 将通用指针转换为具体类型
    printk("Received: %d\n", item->value);  // 打印接收到的数据值
    k_free(item);                   // 释放数据项内存
}

// 非阻塞轮询
void polling_receiver(void)         // 非阻塞轮询接收函数
{
    void *data = k_queue_get(&my_queue, K_NO_WAIT);  // 非阻塞方式从队列获取数据
    if (data) {                     // 检查是否取到数据
        k_free(data);               // 释放数据项内存
    }
}
```

### 10.3 队列 VS 消息队列

| 特性 | 队列 (Queue) | 消息队列 (MsgQ) |
|------|-------------|-----------------|
| 数据存储方式 | 外部（传递指针） | 内部（数据复制） |
| 内存管理 | 由用户管理（可动态） | 内核管理（固定池） |
| 数据大小 | 不限 | 固定 |
| 插入位置 | 队首/队尾/任意 | 仅队尾 |
| 分配/释放开销 | 需手动 `k_free` | 自动 |

### 10.4 FIFO / LIFO / Queue 三者的关系

| 特性 | k_fifo | k_lifo | k_queue |
|------|--------|--------|---------|
| 顺序 | 先进先出 | 后进先出 | 可自定义 |
| 底层 | 基于 k_queue | 基于 k_queue | 原生链表 |
| 复杂度 | 简单 | 简单 | 灵活 |
| 推荐场景 | 严格顺序 | 栈式顺序 | 需要任意插入 |

```c
// k_fifo 本质上是 k_queue 的 FIFO 封装
// k_fifo_put -> k_queue_append
// k_fifo_get -> k_queue_get (from head)

// k_lifo 本质上是 k_queue 的 LIFO 封装
// k_lifo_put -> k_queue_prepend
// k_lifo_get -> k_queue_get (from head)
```

---

## 11. 管道 (Pipe) k_pipe

### 11.1 原理

管道 (`k_pipe`) 提供**面向字节流**的数据传输机制，类似于 POSIX 管道。数据以字节流形式写入管道，以字节流形式读出，**不保留消息边界**。

**关键特性**：
- **字节流语义**：数据无边界，可部分读/写
- **可变大小**：可写入任意大小的数据块
- **同步/异步**：支持阻塞和非阻塞模式
- **ISR 安全**：ISR 中可使用 `K_NO_WAIT`

### 11.2 API

```c
// 定义管道：缓冲区 256 字节，对齐 4
K_PIPE_DEFINE(my_pipe, 256, 4);     // 定义管道，缓冲区 256 字节，4 字节对齐

// 定义管道定义宏参数：
// K_PIPE_DEFINE(name, buffer_size, align)

// 发送数据到管道
void sender(void)                   // 发送者线程函数
{
    const char *data = "Hello Zephyr Pipe!";  // 准备待发送的字符串数据
    size_t written;                 // 记录实际写入的字节数

    /* 阻塞发送 */
    k_pipe_put(&my_pipe, data, strlen(data) + 1, &written,
               strlen(data) + 1, K_FOREVER);  // 阻塞写入管道，直到全部数据写入或超时

    /* 非阻塞发送 */
    k_pipe_put(&my_pipe, data, strlen(data) + 1, &written,
               strlen(data) + 1, K_NO_WAIT);  // 非阻塞写入，管道满则立即返回
}

// 从管道接收数据
void receiver(void)                 // 接收者线程函数
{
    char buf[64];                   // 定义接收缓冲区
    size_t read;                    // 记录实际读取的字节数

    /* 阻塞接收 */
    k_pipe_get(&my_pipe, buf, sizeof(buf), &read, sizeof(buf), K_FOREVER);  // 阻塞读取管道数据

    /* 非阻塞接收 */
    k_pipe_get(&my_pipe, buf, sizeof(buf), &read, sizeof(buf), K_NO_WAIT);  // 非阻塞读取，无数据则立即返回

    printk("Received: %s (bytes: %zu)\n", buf, read);  // 打印接收到的数据及字节数
}

// 查询管道状态
size_t used  = k_pipe_read_avail(&my_pipe);   // 可读字节数
size_t space = k_pipe_write_avail(&my_pipe);  // 剩余空间

// 刷新/清空管道
k_pipe_flush(&my_pipe);             // 清空管道中的所有数据
```

### 11.3 管道 VS 消息队列

| 特性 | 管道 (Pipe) | 消息队列 (MsgQ) |
|------|-------------|-----------------|
| 数据语义 | 字节流 | 消息（有边界） |
| 数据大小 | 任意 | 固定 |
| 部分读/写 | ✅ 支持 | ❌ 必须整条消息 |
| ISR 安全 | ✅ (`K_NO_WAIT`) | ✅ (`K_NO_WAIT`) |
| 内部存储 | 环形缓冲区（可选） | 固定环形缓冲区 |

### 11.4 典型场景：流式数据传递

```c
K_PIPE_DEFINE(data_pipe, 512, 4);     // 定义数据管道，缓冲区 512 字节，4 字节对齐

/* 传感器线程持续产生数据 */
void sensor_thread(void *a, void *b, void *c)  // 传感器数据采集线程
{
    while (1) {                       // 无限循环持续采集
        uint8_t sample[32];           // 定义采样数据缓冲区
        read_sensor(sample);         // 从传感器读取采样数据
        size_t written;               // 记录实际写入字节数
        k_pipe_put(&data_pipe, sample, sizeof(sample),
                   &written, sizeof(sample), K_FOREVER);  // 将采样数据写入管道，阻塞直至写完
        k_sleep(K_MSEC(10));          // 等待 10 毫秒再采集下一次
    }
}

/* 处理线程读取并处理 */
void processor_thread(void *a, void *b, void *c)  // 数据处理线程
{
    while (1) {                       // 无限循环持续处理
        uint8_t buf[128];             // 定义接收缓冲区
        size_t read;                  // 记录实际读取字节数
        k_pipe_get(&data_pipe, buf, sizeof(buf), &read,
                   sizeof(buf), K_FOREVER);  // 从管道读取数据，阻塞直至有数据到达
        process_samples(buf, read);   // 处理读取到的采样数据
    }
}
```

---

## 12. 条件变量 (Condition Variable)

### 12.1 原理

条件变量允许一组线程**等待某个条件成立**，并与互斥量配合使用。线程持有 mutex，检查条件，若不满足则阻塞在条件变量上（自动释放 mutex）。

### 12.2 API

```c
K_MUTEX_DEFINE(lock);               // 定义互斥锁对象
K_CONDVAR_DEFINE(cond);             // 定义条件变量对象

int data_ready = 0;                 // 共享状态标志，指示数据是否就绪

void waiting_thread(void *a, void *b, void *c)  // 等待线程函数
{
    k_mutex_lock(&lock, K_FOREVER);  // 获取互斥锁，保护共享数据
    while (data_ready == 0) {        // 轮询检查条件是否满足
        /* 等待条件成立，同时自动释放 lock */
        k_condvar_wait(&cond, &lock, K_FOREVER);  // 等待条件变量，自动释放锁，唤醒后重新获取
        /* 被唤醒后重新获得 lock */
    }
    /* 条件成立，处理数据 */
    data_ready = 0;                  // 重置数据就绪标志
    k_mutex_unlock(&lock);           // 释放互斥锁
}

void signal_thread(void *a, void *b, void *c)  // 信号发送线程函数
{
    k_mutex_lock(&lock, K_FOREVER);  // 获取互斥锁
    data_ready = 1;                  // 设置数据就绪标志
    /* 唤醒一个等待者 */
    k_condvar_signal(&cond);         // 唤醒在条件变量上等待的一个线程
    /* 或唤醒所有等待者：k_condvar_broadcast(&cond); */
    k_mutex_unlock(&lock);           // 释放互斥锁
}
```

### 12.3 条件变量 VS 信号量

| 场景 | 条件变量 | 信号量 |
|------|---------|--------|
| 与互斥量配合 | ✅ 天然 | ❌ 额外同步 |
| 等待复杂条件 | ✅ 支持 | ❌ 仅计数 |
| 广播唤醒 | ✅ 支持 | ❌ 不支持 |
| 多资源计数 | ❌ 不适用 | ✅ 适用 |

---

## 13. 事件对象 (Event)

### 13.1 原理

事件对象是基于**位掩码（bitmask）** 的信号机制。每个事件对象包含一个 32 位标志位集合，线程可以等待一个或多个特定位被置位。与信号量不同，事件不计数——位要么置位要么清零。

**核心特性**：
- 32 位独立标志位，支持 AND/OR 两种等待模式
- 支持一次性等待（自动清零）和持久等待
- 可在 ISR 中调用 `k_event_post/set/clear`
- 可同时唤醒多个等待线程

### 13.2 API

```c
// 定义事件对象
K_EVENT_DEFINE(my_event);                 // 静态定义事件对象

// 或动态定义
struct k_event my_event;                  // 声明事件对象
k_event_init(&my_event);                  // 动态初始化事件对象

// 置位事件标志
k_event_post(&my_event, 0x05);            // 置位 bit0 和 bit2（或操作，保留原有标志）
k_event_set(&my_event, 0x03);             // 将事件标志设置为 0x03（覆盖原有值，非或操作）

// 等待事件标志（OR 模式：任一指定位置位即触发）
uint32_t result = k_event_wait(&my_event, 0x05, false, K_FOREVER);  // 等待 bit0 或 bit2，不自动清零
result = k_event_wait(&my_event, 0x05, true, K_FOREVER);            // 等待后自动清零触发位

// 等待事件标志（AND 模式：所有指定位均置位才触发）
result = k_event_wait_all(&my_event, 0x07, false, K_FOREVER);       // 等待 bit0+bit1+bit2 全部置位

// 清零事件标志
k_event_clear(&my_event, 0x03);           // 清零 bit0 和 bit1

// 非阻塞查询
result = k_event_wait(&my_event, 0x05, false, K_NO_WAIT);           // 立即返回，不阻塞
```

### 13.3 多条件等待

事件对象支持灵活的位掩码组合等待：

```c
#define EVENT_DATA_READY  BIT(0)          // 数据就绪标志
#define EVENT_ERROR       BIT(1)          // 错误标志
#define EVENT_TIMEOUT     BIT(2)          // 超时标志

K_EVENT_DEFINE(event);                    // 定义事件对象

void waiter_thread(void *a, void *b, void *c)  // 等待线程函数
{
    uint32_t triggered;

    // OR 模式：等待数据就绪或错误发生（任一条件满足即唤醒）
    triggered = k_event_wait(&event,
                             EVENT_DATA_READY | EVENT_ERROR,
                             true,               // 自动清零触发位
                             K_FOREVER);

    if (triggered & EVENT_DATA_READY) {
        process_data();
    }
    if (triggered & EVENT_ERROR) {
        handle_error();
    }
}

void isr_notifier(const void *arg)               // ISR 中触发事件
{
    k_event_post(&event, EVENT_DATA_READY);       // ISR 安全：置位数据就绪标志
}

// AND 模式：等待多个条件同时满足
void sync_thread(void *a, void *b, void *c)      // 同步等待线程
{
    k_event_wait_all(&event,
                     EVENT_DATA_READY | EVENT_ERROR | EVENT_TIMEOUT,
                     true, K_SECONDS(5));         // 等待全部条件置位，5秒超时
}
```

### 13.4 事件 VS 信号量

| 场景 | 事件 | 信号量 |
|------|------|--------|
| 多条件触发 | ✅ 位掩码组合 | ❌ 需多个信号量 |
| 一次性同步 | ✅ 可自动清零 | ✅ 计数递减 |
| 广播能力 | ✅ 可唤醒所有等待者 | ✅ 每个 give 唤醒一个 |
| ISR 安全 | ✅ post/set/clear 均可 | ✅ give 可 |

---

## 14. 定时器 (Timer)

### 14.1 原理

Zephyr 内核定时器允许在指定延迟后或周期性执行回调函数。定时器基于系统 Tick 计数，支持一次性（单次触发）和周期性两种模式。

**核心特性**：
- 到期时执行用户回调函数（在中断上下文中）
- 支持到期同步 `k_timer_status_sync()`，避免在回调中处理繁重逻辑
- 支持 Tickless 模式，空闲时关闭硬件定时器以减少功耗

### 14.2 API

```c
// 定义定时器
K_TIMER_DEFINE(my_timer, expiry_fn, stop_fn);     // 静态定义定时器

void expiry_fn(struct k_timer *timer_id)           // 到期回调函数（中断上下文，应尽量短小）
{
    /* 定时器到期时执行 */
}

void stop_fn(struct k_timer *timer_id)             // 停止回调函数（中断上下文，可为 NULL）
{
    /* 定时器被显式停止时执行 */
}

// 或动态定义
struct k_timer my_timer;                           // 声明定时器对象
k_timer_init(&my_timer, expiry_fn, stop_fn);       // 动态初始化定时器

// 启动定时器
k_timer_start(&my_timer, K_SECONDS(1), K_SECONDS(2));   // 1秒后首次到期，之后每2秒周期性触发
k_timer_start(&my_timer, K_MSEC(500), K_NO_WAIT);       // 500毫秒后单次触发，不重复（K_NO_WAIT=不周期）

// 停止定时器
k_timer_stop(&my_timer);                           // 停止定时器

// 查询定时器状态
uint32_t remaining = k_timer_remaining_get(&my_timer);  // 获取距离下次到期的剩余 Tick 数
uint32_t status    = k_timer_status_get(&my_timer);     // 获取到期次数并清零状态计数
```

### 14.3 定时器到期同步（不依赖回调）

对于不想在中断上下文处理到期事件的场景，可以使用 `k_timer_status_sync()` 在线程上下文中等待定时器到期：

```c
K_TIMER_DEFINE(sync_timer, NULL, NULL);            // 定义定时器，无需回调函数

void worker_thread(void *a, void *b, void *c)     // 工作线程函数
{
    while (1) {
        k_timer_start(&sync_timer, K_SECONDS(5), K_NO_WAIT);  // 启动5秒单次定时器

        /* 阻塞等待定时器到期，返回已到期的累积次数 */
        uint32_t expires = k_timer_status_sync(&sync_timer);   // 线程上下文阻塞等待
        /* 定时器已到期，继续执行后续工作 */
        do_periodic_work();
    }
}
```

> `k_timer_status_sync` 必须在线程上下文中调用（不能在 ISR 中使用），调用后自动清零到期状态计数。

### 14.4 定时器精度与 Tickless

Zephyr 支持 **Tickless 空闲** (`CONFIG_TICKLESS_KERNEL=y`)：

| 模式 | 描述 | 功耗 |
|------|------|------|
| Tickless | 空闲时关闭硬件定时器中断 | 低 |
| Tickful | 固定周期 Tick 中断 | 高 |

在 Tickless 模式下，定时器**只会在需要时产生中断**，减少系统唤醒次数，适合电池供电设备。

---

## 15. 中断管理 (Interrupts)

### 15.1 Zephyr 中断模型

Zephyr 中断架构设计注重**低延迟和可预测性**。中断处理由硬件直接分发到 ISR，不经过内核调度点（除非需要线程切换）。

- **静态中断**：编译时注册，开销最小，通过 `IRQ_CONNECT` 宏定义
- **动态中断**：运行时注册，更灵活，通过 `irq_connect_dynamic` 注册
- **零延迟中断 (ZLI)**：完全绕过内核中断处理逻辑，适用于极端时间敏感场景

### 15.2 注册中断

```c
#define MY_IRQ_LINE  42                                    // 中断线号
#define MY_IRQ_PRIO  2                                     // 中断优先级

// 静态注册中断（编译时）
IRQ_CONNECT(MY_IRQ_LINE, MY_IRQ_PRIO, my_isr,
            DEVICE_DT_GET(DT_NODELABEL(mydev)), 0);
irq_enable(MY_IRQ_LINE);                                   // 使能中断线

// 动态注册中断
void my_isr(const void *arg)                               // ISR 回调函数
{
    /* 处理硬件中断 */
    uint32_t irq = (uint32_t)(uintptr_t)arg;
}

irq_connect_dynamic(MY_IRQ_LINE, MY_IRQ_PRIO, my_isr,
                    (const void *)MY_IRQ_LINE, 0);
irq_enable(MY_IRQ_LINE);

// 中断使能/禁用
irq_disable(MY_IRQ_LINE);                                  // 禁用指定中断线
unsigned int key = irq_lock();                             // 全局关中断
/* 临界区（关中断保护） */
irq_unlock(key);                                           // 恢复中断状态
```

### 15.3 零延迟中断 (Zero Latency Interrupts, ZLI)

零延迟中断完全绕过 Zephyr 的内核中断处理框架：
- 不保存/恢复内核上下文
- 不检查是否需要重新调度
- **不能调用任何内核 API**（信号量、消息队列等）
- 适用于时间极端敏感的硬件响应（如高速定时器）

```c
// 在 Kconfig 中启用：
// CONFIG_ZERO_LATENCY_IRQS=y
//
// 在 IRQ_CONNECT 的 flags 参数中传入 IRQ_ZERO_LATENCY

void ultra_fast_isr(const void *arg)                       // ZLI 中断处理函数
{
    /* 不能调用任何内核 API，只能直接操作硬件寄存器 */
    volatile uint32_t *reg = (uint32_t *)(uintptr_t)arg;
    *reg = CLEAR_FLAG;                                     // 直接清除中断标志
}
```

### 15.4 中断嵌套

Zephyr 支持可配置的中断嵌套：
- 高优先级中断可以抢占正在执行的低优先级 ISR
- 中断优先级数值**越低优先级越高**
- 嵌套深度受限于硬件支持

```text
中断嵌套示意：
ISR(p=0) ──────┬──────────────────────▶
                │ 高优中断执行完成
ISR(p=1) ──┬────┴── 被 ISR(p=0) 抢占 ─▶
            │
    线程    │
```

### 15.5 ISR 可调用的内核 API

| API | 是否可在 ISR 调用 |
|-----|------------------|
| `k_sem_give` | ✅ |
| `k_msgq_put` | ✅ |
| `k_queue_append/insert` | ✅ |
| `k_stack_push` | ✅ |
| `k_event_post/set/clear` | ✅ |
| `k_timer_start/stop` | ✅ |
| `k_sem_take` | ❌（可阻塞） |
| `k_msgq_get` | ❌（可阻塞） |
| `k_mutex_lock` | ❌（可阻塞） |
| `k_mutex_unlock` | ❌ |
| `k_queue_get` | ❌（可阻塞） |

---

## 16. 内存管理

### 16.1 堆内存 (Heap)

Zephyr 提供基于 `k_heap` 的堆内存分配器，支持任意大小分配：

```c
// 定义堆
K_HEAP_DEFINE(my_heap, 4096);                              // 静态定义 4KB 堆内存

// 或使用系统堆（大小由 CONFIG_HEAP_MEM_POOL_SIZE 配置）
extern struct k_heap system_heap;                          // 系统预定义堆

// 分配与释放
void *ptr = k_heap_alloc(&my_heap, 64, K_NO_WAIT);        // 非阻塞分配 64 字节
void *ptr2 = k_heap_alloc(&my_heap, 128, K_MSEC(100));    // 超时等待分配（若堆暂无可释放则阻塞）

k_heap_free(&my_heap, ptr);                                // 释放回原堆

// 系统堆的便捷 API（实质是对 system_heap 的封装）
void *buf = k_malloc(256);                                 // 从系统堆分配（失败返回 NULL）
k_free(buf);                                               // 释放回系统堆

// 查询堆状态
size_t free_bytes = k_heap_free_size(&my_heap);            // 获取空闲字节数
```

> `k_malloc`/`k_free` 使用系统堆，**不可在 ISR 中调用**。`k_heap_alloc` 使用 `K_NO_WAIT` 时可在 ISR 中使用。

### 16.2 内存池 (Memory Pool / Slab)

内存 Slab 分配器提供**固定大小块**的分配，无碎片且分配/释放速度极快（O(1)）：

```c
// 定义内存 Slab：每个块 32 字节，共 10 块，4 字节对齐
K_MEM_SLAB_DEFINE(my_slab, 32, 10, 4);                    // 静态定义 Slab

// 或动态定义
struct k_mem_slab my_slab;                                 // 声明 Slab 对象
k_mem_slab_init(&my_slab, slab_buf, 32, 10);              // 动态初始化

// 分配块
void *block;                                               // 定义接收块指针的变量
k_mem_slab_alloc(&my_slab, &block, K_FOREVER);            // 阻塞等待，直到有可用块
k_mem_slab_alloc(&my_slab, &block, K_NO_WAIT);            // 非阻塞分配

// 释放块
k_mem_slab_free(&my_slab, &block);                         // 将块释放回 Slab

// 查询
unsigned int num_used = k_mem_slab_num_used_get(&my_slab); // 已用块数
unsigned int num_free = k_mem_slab_num_free_get(&my_slab); // 空闲块数

// 典型用法：固定大小的数据包池
K_MEM_SLAB_DEFINE(packet_pool, sizeof(struct net_packet), 32, 4);  // 32个网络包

struct net_packet *pkt;
k_mem_slab_alloc(&packet_pool, (void **)&pkt, K_FOREVER); // 分配网络包
fill_packet(pkt, data, len);
send_packet(pkt);
k_mem_slab_free(&packet_pool, (void **)&pkt);              // 释放包回池中
```

> **Slab VS Heap**：Slab 分配器无碎片、速度快，适合固定大小频繁分配/释放的场景；Heap 适合任意大小的偶发分配。

### 16.3 内存域 (Memory Domain) — 仅 MPU/MMU 设备

Zephyr 支持内存保护（通过 MPU 或 MMU），将内存划分为不同域以实现**线程间隔离**：

```c
K_APPMEM_PARTITION_DEFINE(part_data);             // 定义应用内存分区
K_APP_DMEM(part_data) int my_data;                // 在数据分区中分配变量

K_MEM_PARTITION_DEFINE(part_code);                // 定义代码内存分区
K_APP_BMEM(part_code) void my_code(void) { }      // 在代码分区中放置函数体

struct k_mem_domain domain;                        // 声明内存域对象
/* 将内存区添加到域中 */
k_mem_domain_add_partition(&domain, &part_data);  // 将数据分区添加到内存域
/* 将线程添加到域 */
k_mem_domain_add_thread(&domain, my_thread_id);   // 将指定线程关联到内存域
```

---

## 17. 原子操作与自旋锁

### 17.1 原子操作

原子操作用于**无锁的轻量级同步**，保证多核/中断环境下的读写不被打断：

```c
#include <zephyr/sys/atomic.h>                             // 原子操作头文件

atomic_t val = ATOMIC_INIT(0);                              // 定义原子变量并初始化为0

// 基本操作
atomic_set(&val, 10);                                       // 设置原子值为10
atomic_t ret = atomic_get(&val);                            // 读取原子值（返回10）

// 算术操作
atomic_add(&val, 5);                                        // val += 5（原子加）
atomic_sub(&val, 3);                                        // val -= 3（原子减）
atomic_inc(&val);                                           // val++（原子自增）
atomic_dec(&val);                                           // val--（原子自减）

// 位操作（返回值均为操作前的旧值）
ret = atomic_and(&val, 0x0F);                               // val &= 0x0F
ret = atomic_or(&val, 0xF0);                                // val |= 0xF0
ret = atomic_xor(&val, 0xFF);                               // val ^= 0xFF

// 比较并交换（CAS）
bool success = atomic_cas(&val, 10, 20);                     // 若 val==10 则设为20

// 原子位操作
int was_set = atomic_test_bit(&val, 3);                      // 测试位3是否置位
atomic_set_bit(&val, 3);                                     // 置位位3
atomic_clear_bit(&val, 3);                                   // 清零位3
```

### 17.2 自旋锁 (Spinlock)

自旋锁用于**多核或中断上下文**中的互斥。在单核系统上退化为仅关中断（无忙等待）：

```c
struct k_spinlock lock;                                      // 声明自旋锁

void thread_safe_func(void)                                 // 线程安全函数
{
    k_spinlock_key_t key = k_spin_lock(&lock);               // 获取自旋锁（SMP:忙等，单核:关中断）
    /* 临界区 */
    k_spin_unlock(&lock, key);                               // 释放自旋锁（恢复中断状态）
}

// 与 ISR 同步的典型模式
struct k_spinlock lock;
K_SEM_DEFINE(sem, 0, 1);
int shared_data;

void producer_isr(const void *arg)                           // ISR 中产生数据
{
    k_spinlock_key_t key = k_spin_lock(&lock);
    shared_data = read_sensor();
    k_spin_unlock(&lock, key);
    k_sem_give(&sem);                                        // 唤醒消费者线程
}

void consumer_thread(void *a, void *b, void *c)             // 线程上下文消费
{
    k_sem_take(&sem, K_FOREVER);
    k_spinlock_key_t key = k_spin_lock(&lock);
    int data = shared_data;
    k_spin_unlock(&lock, key);
    process_data(data);
}
```

> **注意**: 持有自旋锁期间**不能调用任何可能阻塞的 API**（如 `k_sem_take(K_FOREVER)`、`k_mutex_lock`），否则在 SMP 上会导致死锁。

### 17.3 SMP 专用的多核同步

```c
// CPU 间中断
void arch_ipi_send(unsigned int cpu_id);  // 向指定CPU发送核间中断(IPI)
// 内存屏障（编译器全屏障，架构无关）
__sync_synchronize();                     // 执行内存屏障，保证多核数据一致性
```

---

## 18. 轮询 API (Polling)

### 18.1 原理

轮询 API 允许线程**同时等待多个内核对象的就绪事件**，类似 POSIX `poll()`/`select()`。与分别为每个对象创建独立线程相比，轮询可显著减少线程数量和内存占用。

**适用场景**：
- 单线程管理多个 I/O 或 IPC 源
- 替代多个等待信号量/队列的独立线程
- 减少 RAM 占用（多个等待任务共享一个栈空间）

### 18.2 API

```c
#include <zephyr/sys/poll.h>                                // 轮询 API 头文件

K_SEM_DEFINE(sem_a, 0, 1);                                  // 定义被轮询的信号量
K_FIFO_DEFINE(fifo_b);                                      // 定义被轮询的 FIFO

void poll_example(void)                                     // 轮询示例函数
{
    struct k_poll_event events[2];                           // 定义事件数组
    int ret;

    // 初始化事件：监控信号量 sem_a
    k_poll_event_init(&events[0],
                      K_POLL_TYPE_SEM_AVAILABLE,             // 事件类型：信号量可用
                      K_POLL_MODE_NOTIFY_ONLY,               // 模式：仅通知
                      &sem_a);

    // 初始化事件：监控 FIFO 是否有数据
    k_poll_event_init(&events[1],
                      K_POLL_TYPE_FIFO_DATA_AVAILABLE,
                      K_POLL_MODE_NOTIFY_ONLY,
                      &fifo_b);

    // 同时等待两个事件中的任意一个
    ret = k_poll(events, 2, K_FOREVER);                     // 永久阻塞等待

    if (ret == 0) {                                          // 轮询成功
        for (int i = 0; i < 2; i++) {
            if (events[i].state == K_POLL_STATE_SEM_AVAILABLE) {
                k_sem_take(&sem_a, K_NO_WAIT);               // 消费信号量
            } else if (events[i].state == K_POLL_STATE_FIFO_DATA_AVAILABLE) {
                void *data = k_fifo_get(&fifo_b, K_NO_WAIT);
                k_free(data);
            }
        }
    }

    /* 重新初始化事件状态，否则下次 k_poll 会使用过期状态 */
    k_poll_event_init(&events[0], K_POLL_TYPE_SEM_AVAILABLE,
                      K_POLL_MODE_NOTIFY_ONLY, &sem_a);
    k_poll_event_init(&events[1], K_POLL_TYPE_FIFO_DATA_AVAILABLE,
                      K_POLL_MODE_NOTIFY_ONLY, &fifo_b);

    // 带超时的轮询
    ret = k_poll(events, 2, K_MSEC(100));                   // 100毫秒超时
}

// 重复使用的轮询循环
void poll_loop(void)                                        // 轮询循环
{
    struct k_poll_event events[1];
    k_poll_event_init(&events[0], K_POLL_TYPE_SEM_AVAILABLE,
                      K_POLL_MODE_NOTIFY_ONLY, &sem_a);

    while (1) {
        k_poll(events, 1, K_FOREVER);

        if (events[0].state == K_POLL_STATE_SEM_AVAILABLE) {
            k_sem_take(&sem_a, K_NO_WAIT);
            /* 处理事件 */
        }

        /* 重置事件状态以供下次轮询 */
        k_poll_event_init(&events[0], K_POLL_TYPE_SEM_AVAILABLE,
                          K_POLL_MODE_NOTIFY_ONLY, &sem_a);
    }
}

### 18.3 支持的事件类型

| 事件类型 | 触发条件 |
|---------|---------|
| `K_POLL_TYPE_SEM_AVAILABLE` | 信号量计数值 > 0 |
| `K_POLL_TYPE_DATA_AVAILABLE` | 队列/栈/消息队列中有数据 |
| `K_POLL_TYPE_SIGNAL` | 信号被触发 |
| `K_POLL_TYPE_FIFO_DATA_AVAILABLE` | FIFO 中有数据 |
| `K_POLL_TYPE_IGNORE` | 忽略此事件 |

---

## 19. 多核处理 (SMP)

### 19.1 Zephyr SMP 架构

Zephyr SMP（对称多处理）允许多个 CPU 核心共享同一个内存空间和内核对象：

- **全局锁 (Global Lock)**：内核持有全局锁，保证一次只有一个核心访问内核关键数据结构
- **细粒度锁**：部分子系统逐步引入细粒度锁以提高多核并行性
- **CPU 掩码 (CPU Mask)**：控制线程可以在哪些核心上运行，优化缓存亲和性

**启用 SMP**：
```kconfig
CONFIG_SMP=y                          # 启用 SMP 支持
CONFIG_SMP_MAX_CPUS=2                 # 最大 CPU 核心数
```

### 19.2 设置 CPU 亲和性

```c
k_tid_t tid = k_thread_create(&my_thread, my_stack,
                               MY_STACK_SIZE,
                               thread_entry, NULL, NULL, NULL,
                               5, 0, K_NO_WAIT);

// 设置 CPU 亲和性掩码
k_thread_cpu_mask_enable(tid, 0);                       // 允许在 CPU0 上运行
k_thread_cpu_mask_enable(tid, 1);                       // 允许在 CPU1 上运行

k_thread_cpu_mask_disable(tid, 1);                      // 禁止在 CPU1 上运行

// 查询当前 CPU ID
unsigned int cpu_id = arch_curr_cpu()->id;              // 获取当前运行核心编号
```

> 正确设置亲和性可提高缓存命中率，减少核心间迁移开销。

### 19.3 SMP 下的同步需求

| 原语 | 单核 | SMP |
|------|------|-----|
| 自旋锁 | 仅关中断 | 忙等待 + 关中断 |
| Mutex | 正常 | 正常（增加所有权跟踪） |
| 信号量 | 正常 | 正常（需确保原子操作） |

> 在 SMP 上，`k_spin_lock` 是真正的自旋（忙等），而单核时退化为只关闭中断。

---

## 20. 时间管理与 Tick

### 20.1 系统 Tick

系统 Tick 是 Zephyr 内核的**基本时间单位**，由硬件定时器中断驱动。每次 Tick 中断触发时，内核更新超时计数并进行调度决策。

- Tick 频率由 `CONFIG_SYS_CLOCK_TICKS_PER_SEC` 决定（默认 100Hz）
- **高 Tick 频率** → 更高时间精度 → 更多功耗（频繁中断唤醒）
- **低 Tick 频率** → 更低功耗 → 计时精度下降
- **Tickless 模式**：空闲时完全关闭 Tick 中断，仅在需要时唤醒

### 20.2 Tick 配置

```kconfig
# ====== Tick 频率 ======
CONFIG_SYS_CLOCK_TICKS_PER_SEC=100       # 系统 Tick 频率（Hz），默认 100Hz
CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC=      # 硬件定时器频率（通常自动检测）

# ====== Tickless 模式 ======
CONFIG_TICKLESS_KERNEL=y                 # 启用 Tickless 空闲模式（推荐，降低功耗）

# ====== 超时设置 ======
# Zephyr 超时子系统使用 delta-list 实现，无需配置位图大小
```

```c
// 获取当前系统运行时间
int64_t ms = k_uptime_get();                             // 系统启动后的毫秒数
uint32_t ms32 = k_uptime_get_32();                       // 32位毫秒数（注意约49.7天回绕）
int64_t ticks = k_uptime_ticks();                        // 系统启动后的 Tick 数

// Tick 与毫秒转换
uint32_t t_up = k_ms_to_ticks_ceil32(100);               // 100ms 对应的 Tick 数（向上取整）
uint32_t t_down = k_ms_to_ticks_floor32(100);            // 100ms 对应的 Tick 数（向下取整）

// 时间测量
int64_t start = k_uptime_get();
do_something();
int64_t elapsed = k_uptime_get() - start;                // 耗时（毫秒）
```

Zephyr 使用统一的超时表达方式：

| 宏 | 含义 |
|----|------|
| `K_NO_WAIT` | 不等待，立即返回 |
| `K_FOREVER` | 永久等待 |
| `K_MSEC(n)` | n 毫秒 |
| `K_USEC(n)` | n 微秒 |
| `K_NSEC(n)` | n 纳秒 |
| `K_SECONDS(n)` | n 秒 |
| `K_MINUTES(n)` | n 分钟 |
| `K_HOURS(n)` | n 小时 |
| `K_TICKS(n)` | n 个系统 Tick |

---

## 21. 工作队列 (Workqueue) k_work

### 21.1 原理

工作队列允许将**非紧急工作从 ISR 或高优先级线程延迟到线程上下文执行**。工作项（work item）被提交到工作队列，由工作队列线程按 FIFO 顺序依次执行。

**为什么需要工作队列**：
- ISR 中不能调用阻塞 API（如 `k_sem_take`、`k_mutex_lock`）
- ISR 应尽量短小，繁重处理应推迟
- 工作队列运行在**线程上下文**，可调用任何内核 API

**系统工作队列**：Zephyr 默认提供一个系统工作队列 `k_sys_work_q`，由 `CONFIG_SYSTEM_WORKQUEUE_STACK_SIZE` 配置栈大小。用户也可创建自定义工作队列。

### 21.2 API

```c
#include <zephyr/kernel.h>                              // 包含Zephyr内核API头文件

// ====== 系统工作队列 ======
// 定义工作项
K_WORK_DEFINE(my_work, work_handler);                    // 定义普通工作项，关联处理函数

void work_handler(struct k_work *work)                   // 工作项处理函数
{
    printk("Work executed in thread context\n");        // 打印提示：工作项在线程上下文中执行
    /* 这里可以调用任何内核 API，包括阻塞 API */
}

void trigger_from_isr(const void *arg)                   // 在ISR中触发工作提交
{
    /* 在 ISR 中提交工作项 */
    k_work_submit(&my_work);                             // 将工作项提交到系统工作队列
}

// ====== 延时工作项 ======
K_WORK_DELAYABLE_DEFINE(delayed_work, delayed_handler);  // 定义延时工作项，支持定时调度

void delayed_handler(struct k_work *work)                // 延时工作项处理函数
{
    printk("Delayed work executed\n");                   // 打印提示：延时工作已执行
}

void schedule_delayed(void)                              // 调度延时工作项
{
    /* 1 秒后执行 */
    k_work_schedule(&delayed_work, K_SECONDS(1));        // 设置1秒后执行延时工作项
}

// ====== 可取消的工作项 ======
void cancel_work(void)                                   // 取消已调度的延时工作
{
    k_work_cancel_delayable(&delayed_work);              // 取消之前调度的延时工作项
}

// ====== 自定义工作队列 ======
K_THREAD_STACK_DEFINE(my_wq_stack, 2048);                // 定义自定义工作队列的线程栈空间
struct k_work_q my_work_q;                                // 声明工作队列对象

struct k_work_q_config my_wq_cfg = {                     // 配置工作队列参数
    .name = "my_wq",                                     // 工作队列名称
    .no_yield = false,                                   // 允许在多个工作项间让出CPU
};

void init_my_workqueue(void)                             // 初始化自定义工作队列
{
    k_work_queue_init(&my_work_q);                       // 初始化工作队列结构体
    k_work_queue_start(&my_work_q, my_wq_stack,          // 启动工作队列线程
                       K_THREAD_STACK_SIZEOF(my_wq_stack), // 传入线程栈大小
                       CONFIG_SYSTEM_WORKQUEUE_PRIORITY,   // 设置工作队列线程优先级
                       &my_wq_cfg);                        // 传入工作队列配置
}

K_WORK_DEFINE(custom_work, custom_handler);              // 定义提交到自定义队列的工作项

void submit_to_custom_wq(void)                           // 提交工作项到自定义队列
{
    k_work_submit_to_queue(&my_work_q, &custom_work);    // 将工作项提交到指定工作队列
}
```

### 21.3 工作项类型

| 类型 | 宏 | 说明 |
|------|-----|------|
| 普通工作项 | `K_WORK_DEFINE` | 立即提交，FIFO 执行 |
| 延时工作项 | `K_WORK_DELAYABLE_DEFINE` | 支持定时调度 |

### 21.4 典型场景：ISR 繁重处理下放

```c
K_SEM_DEFINE(data_ready, 0, 1);                          // 定义二值信号量，用于数据就绪通知
K_FIFO_DEFINE(data_fifo);                                // 定义FIFO队列，用于传递数据指针

/* 工作项：在 ISR 之外处理繁重工作 */
K_WORK_DEFINE(process_work, process_handler);            // 定义处理繁重工作的工作项

void process_handler(struct k_work *work)                // 工作项处理函数（运行在线程上下文）
{
    /* 运行在线程上下文 */
    while (1) {                                          // 循环取出所有待处理数据
        void *data = k_fifo_get(&data_fifo, K_NO_WAIT); // 从FIFO非阻塞取数据
        if (!data) break;                                // 无数据则退出循环
        process_complex_data(data);                      // 在线程上下文中处理复杂数据
        k_free(data);                                    // 释放已处理的数据缓冲区
    }
}

/* ISR —— 只做最少工作 */
void uart_isr(const void *arg)                           // UART中断服务函数
{
    void *buf = k_heap_alloc(&system_heap, 64, K_NO_WAIT); // 从系统堆非阻塞分配缓冲区
    if (buf == NULL) return;
    fill_buffer_from_uart(buf);                          // 从UART硬件读取数据填充缓冲区
    k_fifo_put(&data_fifo, buf);                         // 将缓冲区指针放入FIFO
    k_work_submit(&process_work);                        // 提交工作项，触发线程上下文处理
}
```

### 21.5 工作队列 VS 直接创建线程

| 特性 | 工作队列 | 独立线程 |
|------|---------|---------|
| 资源占用 | 共享线程池 | 每个线程独立栈 |
| 并发 | 串行（单线程） | 可并行 |
| 适用场景 | 短促、零散任务 | 长期运行任务 |
| 提交方式 | `k_work_submit` | `k_thread_start` |
| ISR 安全 | ✅ 可提交 | ❌ 不可创建 |

---

## 22. 线程安全与协作示例

### 22.1 经典生产者-消费者

使用信号量 + 互斥量保护环形缓冲区的完整实现：

```c
#define BUF_SIZE 5                                          // 环形缓冲区容量

struct item {
    uint32_t id;                                            // 数据项ID
    uint8_t  data[64];                                      // 数据负载
};

K_SEM_DEFINE(sem_empty, BUF_SIZE, BUF_SIZE);               // 空槽位信号量
K_SEM_DEFINE(sem_filled, 0, BUF_SIZE);                     // 已填槽位信号量
K_MUTEX_DEFINE(buf_lock);                                   // 保护缓冲区的互斥量

static struct item buffer[BUF_SIZE];                        // 环形缓冲区
static int head = 0, tail = 0;                              // 读写索引

void producer(void *a, void *b, void *c)                   // 生产者线程
{
    while (1) {
        struct item data = produce_data();                  // 生产一项数据
        k_sem_take(&sem_empty, K_FOREVER);                 // 等待空槽位

        k_mutex_lock(&buf_lock, K_FOREVER);                // 锁定缓冲区
        buffer[tail] = data;                                // 写入
        tail = (tail + 1) % BUF_SIZE;                      // 环形索引更新
        k_mutex_unlock(&buf_lock);                          // 解锁

        k_sem_give(&sem_filled);                            // 通知消费者
    }
}

void consumer(void *a, void *b, void *c)                   // 消费者线程
{
    while (1) {
        k_sem_take(&sem_filled, K_FOREVER);                // 等待有数据可消费

        k_mutex_lock(&buf_lock, K_FOREVER);                // 锁定缓冲区
        struct item data = buffer[head];                    // 读取
        head = (head + 1) % BUF_SIZE;                      // 环形索引更新
        k_mutex_unlock(&buf_lock);                          // 解锁

        k_sem_give(&sem_empty);                             // 通知生产者有空槽位
        consume_data(data);                                 // 消费数据
    }
}
```

### 22.2 中断同步到线程

```c
K_SEM_DEFINE(isr_sem, 0, 1);                               // 二值信号量，ISR→线程同步
K_FIFO_DEFINE(isr_fifo);                                    // FIFO 传递 ISR 数据

/* 线程上下文：批量处理 ISR 累积的数据 */
void handler_thread(void *a, void *b, void *c)             // 数据处理线程
{
    while (1) {
        k_sem_take(&isr_sem, K_FOREVER);                    // 等待 ISR 信号

        void *data;
        while ((data = k_fifo_get(&isr_fifo, K_NO_WAIT)) != NULL) {  // 批量取出所有数据
            process_data(data);                              // 在线程上下文中处理
            k_free(data);                                    // 释放缓冲区
        }
    }
}

/* ISR —— 最小工作量，仅做数据搬移和信号通知 */
void uart_rx_isr(const void *arg)                           // UART 接收中断
{
    void *buf = k_heap_alloc(&system_heap, 64, K_NO_WAIT);   // 从系统堆非阻塞分配缓冲区
    if (buf == NULL) {
        return;                                              // 分配失败，直接返回
    }

    size_t len = uart_fifo_read(DEVICE_DT_GET(DT_NODELABEL(uart1)),
                                buf, 64);                   // 从硬件 FIFO 读取数据
    if (len > 0) {
        k_fifo_put(&isr_fifo, buf);                          // 数据指针放入 FIFO
        k_sem_give(&isr_sem);                                // 唤醒处理线程
    } else {
        k_heap_free(&system_heap, buf);                      // 无数据，释放缓冲区
    }
}
```

### 22.3 优先级继承实战

```c
K_MUTEX_DEFINE(resource_mutex);                          // 定义互斥锁，保护共享资源

void task_low(void *a, void *b, void *c)                 // 低优先级任务
{
    k_mutex_lock(&resource_mutex, K_FOREVER);            // 获取互斥锁（永久等待）
    /* 临界区 —— 持有锁，执行较长时间 */
    do_slow_work();                                      // 执行长时间慢速工作
    k_mutex_unlock(&resource_mutex);                     // 释放互斥锁
}

void task_mid(void *a, void *b, void *c)                 // 中优先级任务
{
    while (1) {                                          // 无限循环
        /* 纯 CPU 密集型任务，无需任何锁 */
        do_cpu_bound_work();                             // 执行CPU密集计算（不访问共享资源）
    }
}

void task_high(void *a, void *b, void *c)                // 高优先级任务
{
    /* 高优先级需要锁定 resource_mutex */
    k_mutex_lock(&resource_mutex, K_FOREVER);            // 高优先级任务等待互斥锁
    /* 如果没有优先级继承，task_mid 会无限阻塞 task_high */
    do_fast_work();                                      // 执行快速临界区操作
    k_mutex_unlock(&resource_mutex);                     // 释放互斥锁
}

/*
 * 优先级继承启用后：
 * 1. task_high 等待 resource_mutex
 * 2. task_low 的优先级被提升到与 task_high 相同
 * 3. task_mid 无法再抢占 task_low
 * 4. task_low 快速完成临界区并释放锁
 * 5. task_high 获得锁并执行
 */
```

---

## 附录：关键 Kconfig 选项

```kconfig
# ========== 调度 ==========
CONFIG_PREEMPT_ENABLED=y          # 启用抢占式调度
CONFIG_COOP_ENABLED=y             # 启用协作式（非抢占）线程
CONFIG_TIMESLICING=y              # 启用时间片轮转调度
CONFIG_SCHED_DEADLINE=n           # EDF 截止时间调度（默认关闭）
CONFIG_PRIORITY_CEILING=y         # 启用优先级天花板协议（优先级继承始终内置）

# ========== Tick ==========
CONFIG_SYS_CLOCK_TICKS_PER_SEC=1000  # 系统Tick频率，单位Hz
CONFIG_TICKLESS_KERNEL=y             # 启用Tickless空闲模式，降低功耗

# ========== 中断 ==========
CONFIG_ZERO_LATENCY_IRQS=n        # 零延迟中断支持（默认关闭）
CONFIG_MULTITHREADING=y           # 启用多线程支持

# ========== 多核 SMP ==========
CONFIG_SMP=n                      # 对称多处理支持（默认关闭）
CONFIG_SMP_MAX_CPUS=4             # SMP最多支持的CPU核心数

# ========== 内存 ==========
CONFIG_HEAP_MEM_POOL_SIZE=4096    # 系统堆内存池大小（字节）
CONFIG_MEM_SLAB_TRACE_MAX_USAGE=n # 内存块追踪最大使用量（默认关闭）
CONFIG_MMU=n                      # 内存管理单元支持（默认关闭）
CONFIG_MPU=n                      # 内存保护单元支持（默认关闭）

# ========== IPC ==========
# 消息队列容量在 K_MSGQ_DEFINE 中按实例定义，无全局 Kconfig 选项

# ========== 调试 ==========
CONFIG_THREAD_NAME=y              # 启用线程名称支持，便于调试
CONFIG_DEBUG=y                    # 启用内核调试输出
CONFIG_ASSERT=y                   # 启用断言检查，辅助调试
```

---

> 本文档覆盖了 Zephyr RTOS（3.x）内核中与实时性直接相关的所有核心机制。每个原语都提供了定义、API、典型用法的详细说明，并结合了嵌入式实时系统的经典问题（优先级反转、生产者-消费者、ISR 同步等）进行示例分析。学习时建议结合实际硬件平台（如 STM32N6）进行验证。
