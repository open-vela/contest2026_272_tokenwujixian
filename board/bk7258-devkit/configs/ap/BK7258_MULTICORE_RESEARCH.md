# BK7258 Armino 与 NuttX/OpenVela 多核调研

## 1. 结论

本调研基于以下源码快照：

- Armino SDK：`/home/czp/armino/bk_avdk_smp`，revision
  `d2ded037798530175e5dc5cde6fa1878f5d5ef35`（`release/v3.1.1`）；
- NuttX/OpenVela 内核：`/home/czp/openvela_contest/nuttx`，revision
  `dd92bcf425738734d1b8aed09c2bd4dbe3f2e438`；
- OpenVela workspace 文档与应用：`/home/czp/openvela_contest`；
- 团队 BK7258 BSP：当前工作树。

源码结论很明确：**Armino 在 BK7258 上使用的是“域间 AMP + AP 域内
SMP”的混合架构，不是三个物理核共同运行一个 FreeRTOS 内核。**

```text
物理 CPU0                         物理 CPU1              物理 CPU2
┌──────────────────────┐         ┌─────────────────────────────────┐
│ CP app.bin           │ mailbox │ AP app1.bin                     │
│ 单核 FreeRTOS        │◀───────▶│ 一个双核 FreeRTOS SMP 实例      │
│ 独立 Flash/RAM/驱动域│  + SHM  │ AP local core 0 + local core 1 │
└──────────────────────┘         └─────────────────────────────────┘
             AMP                              SMP
```

NuttX/OpenVela 已具备两组可用的通用机制：

1. NuttX `CONFIG_SMP` 提供单内核、多核调度、per-CPU 状态、spinlock、任务
   迁移和跨核函数调用框架；
2. NuttX `RPTUN` + RPMsg/OpenAMP 提供独立 OS 镜像之间的 remoteproc 生命周期、
   共享内存 VirtIO/RPMsg 和通知抽象。

但是，NuttX 当前**没有可直接启用的通用 ARMv8-M SMP 启核端口**。BK7258
必须自行实现芯片级核号、CPU2 启动、IPI、每核 IRQ/idle stack、cache/内存屏障和
TrustZone/外设归属。OpenVela 没有另一套替代这些工作的多核调度器；它直接使用
NuttX SMP 和 RPTUN/RPMsg/OpenAMP。

因此，BK7258 的推荐目标是忠实复现硬件已经验证过的结构：

```text
OpenVela CP（物理 CPU0，独立镜像）
       │
       ├── RPTUN/RPMsg over BK7258 mailbox + shared SRAM（AMP）
       │
OpenVela AP（物理 CPU1 + CPU2，一个 app1.bin，NuttX 双核 SMP）
```

实现时应先完成 AP 单核 bring-up 和 CP↔AP AMP，再启用 AP 内双核 SMP；不应直接
将 `CONFIG_SMP_NCPUS=3` 加到现有 CP 配置。

## 2. 当前 AP defconfig 的准确状态

`configs/ap/defconfig` 已存在，当前内容声明：

```text
CONFIG_ARCH_BOARD_BK7258_DEVKIT=y
CONFIG_BK7258_COMPONENT_AP=y
```

这已经建立了 AP component 的配置身份，不需要重新创建 defconfig；但它尚未声明
架构、内存、console、SMP、RPTUN 等完整配置，也没有对应 AP linker/startup 和
`app1.bin` 导出。因此当前状态是“已有 AP defconfig，占位但不可构建/启动”，而不是
“没有 AP defconfig”。

## 3. Armino 如何运行 BK7258 多核

### 3.1 两个镜像、三个物理核、两个调度域

Armino 构建器同时构建 `bk7258` 和 `bk7258_ap`。普通应用被标记为 CP 并输出
`app.bin`；`*_ap` 被标记为 AP 并在打包时命名为 `app1.bin`。Flash 分区也显式区分
`primary_cp_app` 与 `primary_ap_app`。[A01][A02]

CP 默认配置声明 `CONFIG_CPU_CNT=3`，表示它知道系统中的三个 mailbox/电源管理
端点，但它运行普通单核 FreeRTOS，并未启用 `CONFIG_FREERTOS_SMP`。AP 配置则明确
为 `CONFIG_CPU_CNT=2`、`CONFIG_SOC_SMP=y`、`CONFIG_FREERTOS_SMP=y`。[A03][A04]

这里有两个不能混用的 CPU 编号空间：

| 语境 | CP | AP 主核 | AP 从核 |
| --- | --- | --- | --- |
| BK7258 全局物理/IPC 编号 | CPU0 | CPU1 | CPU2 |
| AP NuttX/FreeRTOS 逻辑编号 | 不属于 AP 调度域 | local CPU0 | local CPU1 |

Armino AP 的 `rtos_get_core_id()` 将本地 FreeRTOS core ID 加 `CPU_ID_OFFSET=1`，
因此 AP local 0/1 映射成全局 CPU1/2；CP 的 offset 为 0。[A05]

这条映射对未来 NuttX `up_cpu_index()` 至关重要：NuttX AP 实例必须返回逻辑
0/1，不能直接用硬件全局值 1/2 索引 `g_assignedtasks[]`。

### 3.2 CP 启动 AP 主核

CP 的 `start_cpu1_core()` 执行以下流程：[A06][A07]

1. 查询 `BK_PARTITION_APPLICATION1`（即 AP `app1.bin`）；
2. 将带线性 CRC 的物理 Flash 地址按 `34 -> 32` 换算成 CPU XIP 地址；
3. 解除 CPU1 power-down；
4. 选择 CPU1 RX event；
5. 将 boot address 右移 8 位写入 CPU1 boot-offset 字段；
6. 释放 CPU1 reset；
7. 通过 mailbox IPC 通知 CPU1 上电状态。

对应源码顺序为：

```c
sys_drv_set_cpu1_pwr_dw(0);
sys_drv_set_cpu1_rxevt_sel(1);
sys_drv_set_cpu1_boot_address_offset(offset >> 8);
sys_drv_set_cpu1_reset(1);
```

当前团队 BSP 使用的 AP XIP vector base `0x02160000` 与 `app1` 分区合同一致，但
仍需在写入 NuttX AP linker 前，结合芯片寄存器和实际产物再次验证 boot-offset 的
地址语义、安全属性与 alias。

### 3.3 AP 主核启动 AP 从核

CPU1 进入 AP `app1.bin` 后成为 AP 调度域的 local CPU0。AP linker 在同一个 ELF/
binary 内放置多个 512-byte 对齐的向量表，并为正在使用的两个 AP 核保留独立 MSP
stack。[A08]

FreeRTOS SMP 的 `xPortStartScheduler()` 在 local CPU0 上运行，随后调用
`multicore_launch_core1()`。该函数把 AP linker 内的 `__vector_core1_table` 作为
物理 CPU2 的 boot address，解除 CPU2 reset；物理 CPU2 进入自己的 reset handler，
最后执行第二个 scheduler core 的入口。[A09][A10]

因此正常路径不是 CP 分别加载两个 AP 镜像，而是：

```text
CP/CPU0 release CPU1 at app1 partition base
    └─ AP local CPU0 initializes one shared AP image/kernel
         └─ AP local CPU0 releases physical CPU2 at the image's core1 vector
```

AP linker 仍包含 generic `core2` vector 代码，但当前 AP 配置只有两个逻辑核，且
第三份 AP stack 被注释。不能据此宣称 AP 镜像运行三个核。[A08]

### 3.4 CP/AP 通信与共享资源

CP 镜像将自己的 mailbox 身份设为全局 CPU0，AP 镜像设为全局 CPU1；Armino
提供逻辑 channel、`mb_ipc` socket 风格接口以及 `.swap_data` 共享缓冲。[A11][A12]

Armino 的驱动代码同时出现两类同步，证明它明确区分 AMP 与 SMP：

- `CONFIG_CPU_CNT > 1` + mailbox：CP/AP 运行域之间的命令、Flash/SARADC 操作协调；
- `CONFIG_FREERTOS_SMP` + spinlock：同一个 AP FreeRTOS 内核的两个核之间同步。

AP/CP 共享 SRAM 存在多个地址 alias；共享数据路径显式使用 cache
clean/invalidate 和 `DMB`。在没有 BK7258 cache-coherence 明确硬件证据前，NuttX
端口必须按**非一致性共享内存**设计，而不能只使用 `volatile`。[A13]

Armino 没有使用 RPMsg/remoteproc；其 mailbox 协议不能直接当作 OpenAMP resource
table 或 VirtIO vring。可复用的是底层 boot/reset、doorbell、共享 SRAM 和 cache
规则，而不是上层 Armino IPC wire protocol。

### 3.4.1 硬件 Mailbox 与共享内存的职责边界

BK7258 的 Mailbox 是真实 MMIO 外设，但**共享内存不由 Mailbox 硬件管理**。当前
CP/AP 均选择 `CONFIG_MAILBOX_V2_0=y`，其硬件消息只有“源/目标 CPU + 两个 32 位
数据字”，一次不搬运 128 字节缓冲，也不解析任何上层协议。[A16]

Armino 因此形成两级间接：硬件 FIFO 传递软件命令结构的地址与长度，接收端 CPU 再
按该地址直接读共享 SRAM；对 UART/CLI/IPC 等较大负载，软件命令中的一个参数又指向
`SWAP` 中的实际 payload。[A16][A17]

```text
发送核：payload → SWAP；构造 mb_chnl_cmd_t
        Mailbox FIFO: data[0]=命令结构地址, data[1]=长度, tid=目标 CPU
                     ↓ 触发目标核 Mailbox IRQ
接收核：读 FIFO → 按地址读命令结构 → 按其中指针读 SWAP payload
```

`SWAP` 的地址由 `ram_regions.csv` 预留、由双方 linker 的 `.swap_data` 固定；2 KiB
被软件静态切成 `4 channel × 2 remote CPU × 2 方向 × 128 B`，正好占满。buffer 归属、
ACK/busy 状态机、`__DMB()` 与 cache 维护全部是软件协议。[A15][A17]

Mailbox 硬件不知道 SWAP 在哪、多大、哪段属于哪个 channel、payload 是日志还是命令、
buffer 何时可复用、cache 是否已同步。准确表述是：

```text
共享 SRAM = 数据面
Mailbox   = 通知面
软件协议  = 管理面
```

同一个 Mailbox 还兼作 AP 内 SMP 的跨核命令通道：Armino 以 `data[1] == 0` 区分
SMP 命令与普通消息。[A16] 这一点决定了 A2 的 RPTUN doorbell 与 A3 的 SMP IPI 必须
共享同一份 Mailbox 驱动并在 ISR 内分派。

### 3.5 Armino 的 console 与 CLI 归属

Armino 不是“在一个串口上切换两个 FreeRTOS 实例”，而是 **CP UART0 作总控台 + AP
CLI 经 Mailbox 代理**。CP 与 AP 各自编译并启动一份 CLI，但 transport 不同：[A18]

| 项目 | CP | AP |
| --- | --- | --- |
| OS | 单核 FreeRTOS | 双核 FreeRTOS SMP |
| `CONFIG_CLI` | `y` | `y` |
| shell transport | UART | Mailbox |
| 配置 print port | UART0 | `UART_PRINT_PORT=1`，但常规 console 不走它 |
| 命令入口 | UART RX | CP 侧 `ap_cmd ...` 经 Mailbox |
| 日志出口 | UART | Mailbox → CP → UART |
| 每核独立 CLI | 不适用 | 否，两核共享一个 AP CLI |
| CPU 标识 | 通常无前缀 | `ap0` / `ap1` |

`shell_task.c` 依据 `CONFIG_SYS_PRINT_DEV_UART` / `CONFIG_SYS_PRINT_DEV_MAILBOX`
把 `LOG_DEV` 与 `CMD_DEV` 一起绑定到同一 transport。AP 侧当前
`CONFIG_SYS_PRINT_DEV_MAILBOX=y`、`CONFIG_SYS_PRINT_DEV_UART=n`，因此**不能仅凭
`UART_PRINT_PORT=1` 断言 AP 启用了 UART1 console**：`bk_printf_init()` 只在
`CONFIG_SYS_PRINT_DEV_UART` 下才对该端口执行 `bk_uart_init()`。[A18][A19]

命令与日志是两条独立路径：

```text
UART0 RX ──► CP CLI（未加前缀的命令只查 CP 命令表，找不到即 cmd NOT found）
             └─ ap_cmd <cmd> ─► MB_CMD_USER_INPUT ─► AP CLI 执行
AP 日志 ─────► MB_CMD_LOG_OUT ─► CP 按 shell queue tag 分类 ─► UART0
```

即“能在 CP UART 看到 AP 日志”与“UART 输入会交给 AP”不是同一件事：前者是持续汇聚，
后者只由显式 `ap_cmd` 触发。`ap_cmd` 是单条命令转发，不是可驻留的 AP console
mode；源码中没有 `cp_cmd`、console switch 或可持久改变 console 归属的命令。[A20]

其余需要区分的事实：

- AP shell task 的 affinity 参数为 `-1`，是 OS 级 shell task，不是 CPU1/CPU2 各一个
  shell；`ap0`/`ap1` 只是日志前缀，不是命令路由。[A18][A21]
- `CPU_ID_OFFSET=1` 只做 AP local 核到全局物理核的编号换算与日志标识，不参与
  console 切换或命令分发。[A05][A21]
- CP 的 `setprintport 0|1|2` 迁移的是 CP 统一日志/命令所用的物理 UART，不是选择
  CP/AP 哪个 OS 拥有 console；AP 当前未注册该命令。[A20]
- AP 异常/dump 存在直接写 UART0 的旁路（`CONFIG_DUMP_UART_PRINT_PORT`）。crash 日志
  出现在 UART0 不能推断 AP 平时拥有 UART0 CLI。[A19]
- UART0 = GPIO11 TX / GPIO10 RX、UART1 = GPIO0 TX / GPIO1 RX 有源码依据，但 DevKit
  的 USB-UART 是否引出 UART1 尚无板级证据，不能据此规划双物理终端。

## 4. NuttX/OpenVela 的多核机制

### 4.1 NuttX SMP：一个内核管理同构核

`CONFIG_SMP` 依赖 `ARCH_HAVE_MULTICPU` 和独立 interrupt stack，并选择 spinlock；
`CONFIG_SMP_NCPUS` 表示该 NuttX 实例管理的逻辑 CPU 数。[N01]

启动主链为：[N02][N03]

```text
nx_start()
  └─ nx_smp_start()
       └─ for logical cpu = 1..N-1: up_cpu_start(cpu)
            └─ SoC-specific reset/boot/vector/NVIC setup
                 └─ nx_idle_trampoline()
                      └─ unlock scheduler and enter idle
```

NuttX scheduler 已负责 TCB/per-CPU 队列、任务投递、跨核函数调用和 spinlock；SoC
端口仍必须提供至少以下硬件机制：[N03][N04][N05]

- `up_cpu_index()` 或 `up_this_cpu()`：物理 ID 到 NuttX 逻辑 ID 的映射；
- `up_cpu_idlestack()`：次核 idle stack；
- `up_cpu_start()`：次核 reset、boot address、初始上下文和握手；
- `up_send_smp_sched()`：让目标核处理任务投递/切换；
- `up_send_smp_call()`：让目标核执行 SMP-call handler；
- per-core NVIC/IRQ stack、原子操作、memory barrier 和 cache 维护。

NuttX 中的 SMP 是单地址空间、单内核资源视图，不负责 CP/AP 两个独立镜像之间的
生命周期或消息协议。

### 4.2 ARMv8-M 不是开关即用的 SMP 端口

通用 `ARCH_CORTEXM33` / `ARCH_ARMV8M` Kconfig 没有选择
`ARCH_HAVE_MULTICPU`；`arch/arm/src/armv8-m/` 的构建清单也没有 cpustart、cpuindex、
smpcall 或 IPI 实现。[N06][N07]

`ARMV8M_TRUSTZONE_HYBRID` 仅提供一个与 SMP 配合的 secure CPU bitmask 机制，不能
替代 BK7258 的 SAU/PPC、VTOR、boot address、reset 或 mailbox 实现。[N08]

所以在 BK7258 chip Kconfig 中直接加 `CONFIG_SMP=y` 不会形成可运行系统。需要新建
BK7258-specific SMP port，并且这类通用 NuttX 能力应在验证后以独立上游 PR 交付，
不能把上游源码复制进团队仓库。

### 4.3 NuttX AMP：RPTUN + RPMsg/OpenAMP

`CONFIG_RPTUN` 选择 RPMsg VirtIO，RPMsg 再选择 OpenAMP；RPMsg 初始化还使用
libmetal。[N09][N10]

SoC/board 端通过 `struct rptun_ops_s` 提供：[N11]

- master/remote 角色和 CPU 名称；
- resource table 与地址转换；
- remote firmware 的 config/start/stop/reset；
- mailbox doorbell `notify(vqid)`；
- RX interrupt callback 注册。

`rptun_initialize()` 之上的通用层负责 remoteproc、resource table、VirtIO vring 和
RPMsg endpoint；它不会自动生成 AP 镜像、决定分区、配置 TrustZone，或替代
BK7258 reset/boot 驱动。

最接近的参考端口是：

- nRF5340：两个 Cortex-M33 独立镜像、共享 SRAM、硬件 IPC notify；[N12]
- STM32H745：CM7/CM4 独立镜像、共享 SRAM 和 HSEM doorbell。[N13]

OpenVela 文档把 OpenAMP 放在 NuttX kernel 组件树中，现有安全域示例也直接使用
`CONFIG_RPTUN`/RPMsg。因此 OpenVela 层没有需要另行移植的第三套多核内核。[V01]

### 4.4 RPMsg UART：跨域 console 隧道及其边界

`CONFIG_RPMSG_UART` 提供一个建立在 RPMsg endpoint 之上的虚拟 tty，是 OpenVela 侧
与 Armino `ap_cmd` 对应但更完整的机制。它**不是把 NSH 进程迁移到另一个核，也不是
切换物理 CPU**，而是把远端的 console 设备暴露到本地。[N14]

分层关系与 Armino 明确不同：

```text
Armino                          OpenVela/NuttX
──────                          ──────────────
CLI/Log 私有协议                NSH / uart_rpmsg
MB_CMD_USER_INPUT/LOG_OUT       RPMsg endpoint (rpmsg-tty<devname>)
Armino mailbox channel          RPMsg VirtIO / OpenAMP
                                vring + descriptor + payload
SWAP 软件 buffer                SWAP / RPMSG_SHM
                                RPTUN notify / callback
BK7258 硬件 Mailbox             BK7258 硬件 Mailbox（仅 doorbell）
```

即 OpenVela 路径仍使用 BK7258 硬件 Mailbox，但只作为“共享内存里有新数据”的
doorbell，不复用 Armino 的私有 Shell wire protocol。

约束条件（均为源码结论，非推测）：[N14][N15][N16]

1. 驱动没有 master/remote 分支，**两侧 NuttX 实例都要** `CONFIG_RPMSG_UART=y`，并
   各自调用 `uart_rpmsg_init()`；只有一侧启用则对端没有同名 endpoint，`ns_bound`
   不会发生，数据无法传递。
2. `uart_rpmsg_init(cpuname, devname, buf_size, isconsole)` 调用后**立即**注册本地
   `/dev/tty<devname>`，不等待 RPMsg 建链；只有当出现 `rpmsg_device` 且其远端 CPU
   名与 `cpuname` 精确相等时，才创建 `rpmsg-tty<devname>` endpoint。角色完全由板级
   参数决定。
3. `CONFIG_RPMSG_UART_CONSOLE` 只声明能力，仍需板级传 `isconsole=true` 才会额外
   注册 `/dev/console`，且该注册同样早于 endpoint 建立。还需 `CONFIG_DEV_CONSOLE`，
   并且全局只能有一个 console 驱动被选中。没有运行时自动升级逻辑。
4. `rpmsg_serialinit()` 在 `drivers_initialize()` 中于 `rpmsg_initialize()` 之后调用；
   console 归属在初始化期固定，之后不再重绑定。
5. 没有“切换核心 / 切换 console”命令。典型用法是 remote 侧把它设为 `/dev/console`，
   master 侧从 `/dev/tty<devname>` 收日志并回送输入。OpenVela 的现成工具是
   `apps/system/cu`：`cu -l /dev/ttyAP` 进入，转义符默认 `~`，`~.` 挂断返回；
   参考配置以 `CONFIG_SYSTEM_CUTERM_DEFAULT_DEVICE` 指定默认设备。[N18]
   RPTUN 的 start/stop/reset 属于核生命周期控制，不改变 console 绑定。

失效行为是本阶段最需要设计规避的部分：[N14][N15]

- endpoint 建立前：`open()` 会成功；空 `read()` 阻塞；`write()` 先进入本地环形缓冲，
  缓冲写满后阻塞，且**成功返回不证明远端已收到**。
- `O_NONBLOCK`：空读/满写返回 `-EAGAIN`。
- 读超时只有在启用 `CONFIG_SERIAL_TERMIOS` 且处于非 canonical 模式时才通过
  `VMIN/VTIME` 生效；canonical read 可无限等待。
- endpoint destroy 只销毁 endpoint 并把当前 TX DMA 记为完成，**未调用**
  `uart_connected(dev, false)`因此不会产生 `ENOTCONN`/`POLLHUP`，也不会唤醒已阻塞
  的 read。干净断链后仍可 open，随后读可能永久阻塞、写最终堵满。
- 远端纯 hang（未触发 RPTUN teardown）时没有 UART 层 heartbeat 或请求超时；驱动也
  没有 RPMsg-UART 专属的 timeout/recovery ioctl。因此 CP 侧访问 AP tty 必须自带
  超时或非阻塞策略，不能把它放在关键启动路径上。
- 若 AP 侧选择 `CONFIG_RPMSG_UART_CONSOLE`，参考端口会把 `up_putc()` 实现为空函数。
  这意味着 AP 失去不依赖 RPMsg 的早期/致命输出通道，必须保留 SWAP 记录作为兜底。

因此 RPMsg UART 适合作为 **A2 后半段“AP NuttX 完整可交互”的强证据**，但它依赖
CP/AP 两侧 NuttX 均运行、RPTUN、name service、共享内存布局、cache/barrier 与
Mailbox doorbell 全部就绪；它不能替代 A0/A1 更底层的证据，也不能单独证明 crash
recovery。

## 5. BK7258 与 NuttX 的映射

| Armino/BK7258 机制 | NuttX/OpenVela 对应机制 | BK7258 当前状态 |
| --- | --- | --- |
| CP `app.bin`，物理 CPU0 | 独立 NuttX CP image | 已 bring-up |
| AP `app1.bin`，独立 Flash/RAM | 独立 NuttX AP image | linker/startup/scheduler 已实现并真机运行 |
| CP 启动/停止 CPU1 | BK7258 lifecycle driver；可接 `rptun_ops.start/stop/reset` | start 已接入；stop/reset/phase 未实现 |
| CP↔AP mailbox + SHM | RPTUN/RPMsg/OpenAMP 的 notify + vring SHM | 固定共享内存与 Mailbox IRQ79 已真机双向验证 |
| AP local CPU0=物理 CPU1 | AP NuttX logical CPU0 | 必须实现 ID 映射 |
| AP local CPU1=物理 CPU2 | AP NuttX logical CPU1 | 必须实现 `up_cpu_start(1)` |
| FreeRTOS cross-core yield | NuttX `up_send_smp_sched/call` IPI | 未实现 |
| FreeRTOS SMP spinlock | NuttX spinlock/atomic | 框架已有，硬件验证缺失 |
| cache clean/invalidate + DMB | libmetal/RPTUN cache ops + arch cache/barrier | 分区语义已冻结；cache 属性和维护边界待验证 |
| per-core vector/MSP/NVIC | AP linker + secondary reset entry + IRQ stacks | 未实现 |
| Mailbox v2 FIFO + `INT_SRC_MAILBOX` | `rptun_ops.notify(vqid)` + RX callback；A3 复用为 SMP IPI | 芯片级 doorbell 已实现；A3 IPI 尚未实现 |
| Armino mailbox 逻辑 channel/ACK 状态机 | RPMsg vring + descriptor + endpoint | 不移植，由 RPMsg 取代 |
| CP UART0 总控台 + `ap_cmd` 单条转发 | CP `/dev/console` + AP `uart_rpmsg` tty/console | 双侧已编译接入；首次 `cu` 会话待真机验收 |

不推荐 `CONFIG_SMP_NCPUS=3` 的原因：CP 与 AP 已由独立分区、独立 RAM、独立启动和
mailbox 通信构成不同运行域；Armino 的已工作模型也没有让一个 scheduler 跨越全部
三个物理核。将三核强行合入一个 NuttX 实例会同时改变安全域、外设所有权、内存
模型和启动合同，没有现成硬件证据支持。

### 5.1 已冻结的跨域共享内存策略

以下设计决定已经冻结，后续 linker、defconfig、RPTUN 和打包实现均应遵守：

1. `PWR_MNG` 保持为独立的 `0x100` 字节控制区，不与 RPMsg 合并。它是 CP/AP
   共同使用的固定地址 ABI，承载电源/时钟投票、唤醒计数、复位原因、异常仲裁和
   跨域 Flash 锁；不能放入任一镜像会在启动时初始化的普通 `.data`/`.bss`，也不能
   被 RPMsg resource table、vring 或 buffer pool 的重初始化覆盖。[A14]
2. RPMsg 使用 BK7258 既有 `SWAP` 共享内存语义，Mailbox 作为核间通知/doorbell。
   OpenVela 文档确认，同芯片 Rpmsg VirtIO 的物理前提就是共享内存与核间中断，
   BK7258 的 SWAP + Mailbox 与该模型匹配。[V02]
3. 全 OpenVela CP + AP 路径允许重新定义并扩大 SWAP，使其承载 RPTUN resource
   table、两个 vring 和双向 RPMsg buffer pool。`16 KiB` 是根据当前 NuttX nRF53/
   STM32H7 参考配置得到的推荐起点，不是尚未验证的最终常量；最终大小和地址必须
   由 CP/AP ELF map、descriptor/buffer 配置、cache line 对齐和压力测试共同确定。
4. vendor 兼容路径不得移动或重定义旧 `PWR_MNG`/`SWAP` ABI。若需要让 OpenVela CP
   与 vendor AP 同时运行，则另划 `RPMSG_SHM`；vendor recovery image 只用于恢复且
   不与 OpenVela AP 混跑时，不要求新 OpenVela 布局继续兼容其运行时 ABI。
5. BK7258 的 640 KiB SRAM 当前已经全部分配。扩大 SWAP 必须从 AP_RAM 或 CP_RAM
   等量让出空间，并重新生成双方内存头、链接两个镜像、检查 heap/stack 余量和完成
   真机验证；禁止只增大 `CONFIG_SWAP_SIZE` 或伪造更大的 SRAM capacity。[A15]

尚未冻结的参数包括：SWAP/RPMSG_SHM 的最终地址与大小、从 AP_RAM 或 CP_RAM
让出的具体容量、RPMsg descriptor 数量与单 buffer 大小，以及共享区的 MPU/cache
属性。这些参数属于 S0 内存合同的测量结果，而不是本设计决策的一部分。

### 5.2 Mailbox 适配范围：需要，但只做 doorbell

实现 BK7258 RPTUN 时**必须**适配硬件 Mailbox，但适配面远小于 Armino 那一整套。

必要性有三条依据：

1. `rptun_ops_s.notify` 是必填项，语义就是踢对端一脚；发送侧走
   `rptun_notify() → RPTUN_NOTIFY(dev, vqid)`，接收侧靠
   `RPTUN_REGISTER_CALLBACK` 注册的回调被触发后才去检查 vring。[N11][N17]
2. 参考端口正是把这一层接到芯片核间中断上：nRF53 用 IPC signal 实现
   `notify()`，并用 IPC callback 唤醒 RX。[N12]
3. BK7258 上担此角色的现成硬件就是 Mailbox：基址 `0x41000000`、
   `INT_SRC_MAILBOX = 63`（GROUP1）、每核使能位位于 `SYS_CPUx_INT_32_63_EN` 的
   bit 31。[A16][A22]

另一条理由是复用：A3 的 AP SMP 也需要 IPI，Armino 就用同一个 Mailbox 兼两职
（以 `data[1] == 0` 区分 SMP 命令与普通消息）。[A16] 因此 Mailbox 驱动写一次，
`up_send_smp_sched()` / `up_send_smp_call()` 可以复用，不是只为 RPTUN 付出的成本。

需要适配的接口很薄，建议只暴露三个芯片级函数：

```c
int bk7258_mbox_init(bool global_owner);
int bk7258_mbox_notify(int dst_cpu, uint32_t token);
int bk7258_mbox_attach(bk7258_mbox_cb_t cb, void *arg);
```

内部内容为：时钟/复位、本核 channel FIFO 配置、`irq_attach(BK7258_IRQ_MAILBOX, ...)`、
写 `tdata0/tdata1/tid` 发送、ISR 排空 FIFO 并按来源分派。RPTUN 侧只需：

```c
static int bk7258_rptun_notify(struct rptun_dev_s *dev, uint32_t vqid)
{
  return bk7258_mbox_notify(peer_cpu, RPTUN_NOTIFY_ALL);
}
```

明确**不移植**的 Armino 层（RPMsg VirtIO 已有等价物，重复实现会引入两套语义）：

| Armino 层 | 是否移植 | 理由 |
| --- | --- | --- |
| Mailbox 寄存器 / FIFO / IRQ | 是，但重写而非复制 | RPTUN notify 与 SMP IPI 的物理基础 |
| `mailbox_channel` 逻辑通道、ACK/busy 状态机 | 否 | vring + descriptor 已负责流控与完成通知 |
| SWAP `4×2×2×128B` 切片 | 否 | 换成 resource table 描述的 vring + carveout |
| `MB_CMD_LOG_OUT` / `MB_CMD_USER_INPUT` | 否 | 由 RPMsg endpoint + `uart_rpmsg` 取代 |
| heartbeat / IPC router | 否 | RPMsg name service 与 RPTUN 状态位覆盖 |

对 OpenVela 而言，Mailbox 中甚至不必传共享 buffer 地址：共享内存与 vring 地址已由
resource table 约定，`notify(vqid)` 只需传 vring 编号或固定 doorbell 值。

必须提前冻结的实现约束：

1. **初始化归属**。Armino 中 mailbox 设备级初始化只由 CPU0 执行，从核仅使能自身
   channel 中断。[A16] 我们的 CP/AP 是两个独立镜像，必须显式约定：CP 负责全局
   init 与 `chn_pro_disable` 策略，AP 只使能本核，且 AP 不得重复软复位 Mailbox，
   否则会打断已建立的通道。
2. **FIFO 满时不得丢 doorbell**。整个 FIFO 仅 8 个 entry，按 `2/3/3` 分给三个
   channel。[A16] 当前每个方向使用一个共享 outstanding latch，所有通知统一为
   `RPTUN_NOTIFY_ALL`；已有通知在途时可以安全合并，不需要定时轮询。该协议下正常
   情况每方向最多一个 FIFO entry，full 应作为所有权/硬件不变量错误返回。
3. **中断不代表数据可见**。Mailbox IRQ 只说明“有人踢了你”，共享内存可见性仍依赖
   barrier 与 cache 维护，必须单独验证。
4. **IRQ 可能早于对端就绪**。CP 在 AP 未运行时 notify 属正常情况，两侧都要能容忍
   无响应而不阻塞启动。
5. **AP 侧 ISR 必须 demux**。同一个 `INT_SRC_MAILBOX` 既承载 CP↔AP doorbell，又
   承载 AP 内 CPU1↔CPU2 的 SMP IPI，ISR 必须按来源与消息类型分派。

建议的解耦顺序（可将 Mailbox 从 RPTUN 首次联通中拆出，避免一次调两个未知项）：

```text
A2-1  RPTUN + RPMsg + uart_rpmsg，notify 先用轮询线程周期性调 callback
      → 验证共享内存布局、vring、cache、endpoint 命名
A2-2  换成 Mailbox doorbell，去掉轮询
      → 单独验证 IRQ 路由、per-CPU 使能、FIFO 满处理
A3    在同一 Mailbox 驱动上叠加 SMP IPI
```

A2-1 若能跑通 AP tty/console，即说明数据面正确；此时 Mailbox 问题只表现为延迟，
不会与 vring 缺陷混淆。该拆分是建议而非强制；若直接上 Mailbox，需接受排查面更大。

### 5.3 CP 侧 RPTUN footprint 实测（内存契约输入）

冻结 CP_RAM / AP_RAM / 共享区切分之前，先在 CP 上实测一次 RPTUN/RPMsg/OpenAMP 的
开销，避免按猜测预留。测量方法为临时在 CP 配置上打开 `CONFIG_RPTUN` +
`CONFIG_RPMSG_UART` 构建一次，与同源基线对比；该临时配置在取得数据后即删除，未
保留为并存的 defconfig。

| 项 | 基线 | 加 RPTUN | 增量 |
| --- | --- | --- | --- |
| flash（`.text`+`.data`） | 178756 B | 197905 B | +18.7 KiB |
| 静态 RAM（`.data`+`.bss`） | 6912 B | 7056 B | +144 B |
| flash 区域占用 | 12.99% | 14.38% | 区域 1344 KiB |

上表的 flash 增量是**偏低值**：`--gc-sections` 只拉入了 `uart_rpmsg` 那条链，
`rptun_initialize`、`rpmsg_virtio_probe`、`metal_init`、`virtqueue_kick` 在镜像中
均为 absent，因为此时无人调用 `rptun_initialize()`。按归档内 `.text` 估算全链上界：

```text
libopen_amp.a  .text   30.4 KiB
libmetal.a     .text    7.1 KiB
rptun/rpmsg/rpmsg_virtio/uart_rpmsg  19.6 KiB
────────────────────────────────────────────
全链上界 ≈ 56 KiB → 约 230 KiB，占 1344 KiB 的 17%
```

运行时 heap 估算约 13 KiB：`CONFIG_RPTUN_STACKSIZE=4096`、`uart_rpmsg` 收发缓冲
2×4096、priv/dev/pool 结构约 1 KiB；对照 CP 当前可用 heap 186 KiB
（`CONFIG_RAM_END 0x2809f700` − `idle_stack_top 0x28070f00`）约占 7%。

**结论：RPTUN 自身不构成 CP_RAM 缩容压力，flash 也不是约束**。真正需要预留的是共享
内存 carveout，因此 §5.1 的切分只需为 RPMSG_SHM 让出空间，不必压缩 CP_RAM。

同时确认两项前提：OpenAMP 与 libmetal 已 vendored 在 `nuttx/openamp/`
（分别 51 与 114 个 `.c`），无需外部下载；`CONFIG_RPMSG_UART` 要求板级提供
`rpmsg_serialinit()`，否则 `drivers_initialize.c:211` 会产生未定义引用——该 hook
连同真实 `cpuname` 与 `rptun_ops_s` 一并在 A2 引入，A0/A1 阶段 CP 不保留任何 RPTUN
面。

## 6. AP bring-up 实施流程与阶段关卡

AP bring-up 必须按下面的依赖顺序推进。每一阶段只增加一个主要未知量，上一阶段的
硬件证据未记录前，不进入下一阶段：

```text
A0  CPU1 AP reset stub
       ↓
A1  CPU1 单核运行 NuttX
       ↓
A2  CP↔AP RPTUN/RPMsg（AMP）
       ↓
A3  AP 内 CPU1+CPU2 NuttX SMP
       ↓
A4  外设和业务迁移
```

### 6.0 证据分层：镜像存在不等于 AP bring-up

“把 `app1.bin` 编出来并打包进 image”只证明镜像存在，既不证明物理 CPU1 执行，也不
证明 AP NuttX 已运行。反之，只用最终的 AP NSH 作为唯一证据同样不可取：一旦看不到
AP shell，无法区分故障发生在哪一层——CPU1 未执行、AP linker/vector 错误、scheduler
未起、共享内存布局错、cache 未同步、Mailbox IRQ 未路由、RPTUN 未启动、vring 错误、
name service 未绑定，或 `uart_rpmsg` 两侧 `cpuname`/`devname` 不一致。

因此每一阶段都必须有一条**不依赖下一阶段组件**的独立证据：

```text
app1.bin 构建与打包成功        ≠ AP bring-up
A0  SWAP boot record（sequence 递增）  = CPU1 reset-vector bring-up
A1  scheduler heartbeat（被调度线程更新）= AP 单核 NuttX kernel bring-up
A2  uart_rpmsg 双向 AP NSH             = AP AMP 交互 bring-up
A3  CPU2 online + per-core 测试        = AP 双核 SMP bring-up
```

各阶段观察到的 shell 归属必须按下表如实记录，禁止把 CP 的 `nsh>` 当作 AP 证据：

| 阶段 | CP / 物理 CPU0 | AP / 物理 CPU1 | 操作者在 UART0 看到 |
| --- | --- | --- | --- |
| 当前 | NuttX + NSH + `cu` | 单核 NuttX + RPMsg NSH | CP `nsh>`；可经 `/dev/ttyAP` 进入 AP NSH（待真机验收） |
| A0 | NuttX + NSH | 裸 stub，无 scheduler | 仅 CP `nsh>` |
| A1 | NuttX + NSH | 单核 NuttX，无 console | 仅 CP `nsh>` + 板级 `bk7258_ap status` |
| A2 | NuttX + NSH | 单核 NuttX + NSH | CP `nsh>`，可进入 AP NSH |
| A3 | NuttX + NSH | 双核 SMP NuttX + 一个 NSH | 同 A2，不会新增 CPU2 shell |

物理 UART 上的 `nsh>` 属于 CP/物理 CPU0：CP defconfig 以 `nsh_main` 为
`CONFIG_INIT_ENTRYPOINT`，并把 UART0 注册为 `/dev/console`。[T01][T02] AP 现在也以
标准 `nsh_main` 为入口，但其 `/dev/console` 是 RPMsg UART；必须通过 CP 的
`/dev/ttyAP` 访问，不能把两个提示符的归属混淆。

A1 之后“AP NSH 的输入输出放在哪里”是独立决策，三种方案的取舍：

1. **AP 走 UART1**：两个物理终端，早期独立调试直观。但 UART1 对应 GPIO0/1，DevKit
   的 USB-UART 是否引出尚无板级证据，不能预设可用。
2. **AP 走 RPMsg console（推荐）**：AP NSH 仍在 AP 执行，字节经 RPMsg 送到 CP，由
   UART0 显示；与 §4.4 一致，也是最终目标形态。
3. **CP/AP 同时直接驱动 UART0**：不采用。两个独立 NuttX 实例会争抢 RX、IRQ、FIFO 与
   输出流，导致字符交错、输入被错误实例读取、寄存器配置互相覆盖。

### 6.1 A0：CPU1 AP reset stub

**目标**：只证明物理 CPU1 能被 CP 按已冻结的启动合同释放，并从 AP `app1.bin`
的 reset vector 执行团队代码。A0 不证明 NuttX、Mailbox IRQ、RPMsg 或 SMP。

实施前必须冻结：

1. AP XIP vector base、Flash payload/physical offset 和 boot-offset 换算；
2. CPU1 reset 后的 Secure/Non-secure 状态、SAU/PPC 和 VTOR 合同；
3. AP RAM、初始 MSP、最小 stack 和 vector table 对齐；
4. CPU1 power-down、RX-event、boot-address、barrier、reset release 的准确顺序和极性；
5. CPU2 在整个 A0/A1/A2 阶段保持 reset 的方法及可观测状态。

构建侧应新增而不是复用 CP 合同：

- AP 专用 linker/startup/vector table；
- `app1.bin`、AP ELF 和 map 导出；
- AP L1 validator，至少检查 vector base、MSP、Thumb reset target、`.data/.bss`、
  AP RAM/Flash 容量，以及不得覆盖 `PWR_MNG`/`SWAP`；
- 独立 OpenVela AP packaging profile，将 bundled Bootloader、CP `app.bin` 和 AP
  `app1.bin` 组合并独立 decode；
- 保留现有 CP + AP placeholder profile，禁止给它增加任意 AP override。

AP reset stub 只执行以下动作：

```text
Reset_Handler
  → 建立最小 MSP/VTOR
  → 向现有 2 KiB SWAP 写版本化 boot record
  → DMB，并按 cache 属性执行必要的 clean
  → WFI
```

boot record 至少包含 `magic`、ABI version、stage、sequence、vector base、reset PC、
fault code 和 checksum。CP 每次启动前递增 sequence 并清理记录，以防旧 SWAP 内容
被误判为本次启动成功。

CP 侧需要一个最小 lifecycle 控制。这里必须区分“启动从核”与“查询/复位/停止从核”：
前者 OpenVela 已有标准接口，后者没有。

**启动从核走标准 hook，不要自造 API。** NuttX 提供
`BOARDIOC_START_CPU` boardctl 命令，语义即“by master core start specified slave
core”，参数为 cpu core id，并要求板级实现 `int board_start_cpu(int cpuid)`；
`apps/system/init` 已有调用该命令的先例。[N22] 因此 A0 应把 CPU1 释放序列实现为
`board_start_cpu(1)`，而不是自定义一个私有启动接口。

`include/nuttx/board.h` 对该 hook 的描述与本项目场景完全吻合：它用于
“start specified slave cpu core under the **pseudo AMP** case which is different with
armv7-a/armv8-a SMP”。BK7258 的 CP 释放 CPU1 正是这种 pseudo-AMP 启核，而非 SMP
内部的 `up_cpu_start()`，两者不可混用。[N22]

三点相关事实：

1. `CONFIG_BOARDCTL_START_CPU` 没有 `depends on !UP`（相邻的
   `BOARDCTL_IRQ_AFFINITY` 才有），因此可在 `CONFIG_SMP=n` 的 CP 上启用，符合 CP
   当前单核现状。
2. 当前 NuttX 树内**没有任何 board/arch 实现过 `board_start_cpu()`**，它是纯 hook；
   寄存器序列、安全域与 boot-offset 处理仍完全由 BK7258 板级代码提供。
3. `CONFIG_BOARDCTL` **已经是 `y`**，但不是 CP defconfig 显式设置的：
   `CONFIG_NSH_ARCHINIT=y` 通过 `select BOARDCTL` 间接拉起，且
   `board_app_initialize()` 已在 `board/bk7258-devkit/src/bk7258_bringup.c` 中存在。
   因此 A0 只需新增 `CONFIG_BOARDCTL_START_CPU=y` 并实现 `board_start_cpu()`，
   不要重复添加 `CONFIG_BOARDCTL=y`。[T03]

**查询/复位/停止与 boot record dump 没有标准接口**，A0/A1 仍需一个 board-local NSH
builtin（下文记作 `bk7258_ap <status|reset|stop>`），并在代码与提交信息中标注它是板级
调试命令。`board_start_cpu()` 的签名只接受 cpuid、只返回 int，不足以承载 stage、
sequence、heartbeat 与 fault 的读取。

A2 起，`rptun_ops.start` 应内部调用同一个 `board_start_cpu()`，由标准
`RPTUNIOC_START/STOP/RESET`（作用于 `/dev/rptun/<cpuname>`）驱动，保证释放路径只有
一条；此时该 builtin 降级为纯诊断工具。[N19]

`board_start_cpu(1)` 的动作序列为：

```text
保持 CPU2 reset
  → 准备本次 sequence
  → CPU1 power-down release
  → 配置 RX event
  → 写 boot offset
  → DMB
  → release CPU1 reset
  → 有限超时轮询本次 boot record
```

第一轮只轮询 SWAP，不依赖 Mailbox。SWAP 记录稳定后，才增加“AP 触发 Mailbox、CP
ISR 校验相同 sequence”作为第二条独立证据。AP 不得初始化 UART、timer、heap、
scheduler 或 CPU2。

**A0 验收门**：

- AP ELF/raw validator 和完整 image independent decode 均为 `pass`；
- 保存 CP/AP ELF/raw 与 `all-app.bin` SHA-256、完整 loader 日志和 UART capture；
- 释放路径经 `BOARDIOC_START_CPU` → `board_start_cpu(1)`，与板级
  `bk7258_ap status/reset/stop` 可重复配合执行，每次得到新的 sequence；
- 能区分启动超时、AP fault、stale record 和 stale Mailbox notification；
- 证据表明 CPU2 始终未释放；
- placeholder recovery image 仍可恢复 CPU0。

### 6.2 A1：CPU1 单核运行 NuttX

**目标**：在物理 CPU1 上运行一个独立的单核 NuttX AP 实例，证明 startup、timer、
scheduler 和异常路径正确；此时仍不是 SMP。

实施内容：

1. 完善现有 AP defconfig，但保持 `CONFIG_SMP=n`；
2. 在 A0 linker/startup 基础上增加 AP heap、CPU1 NVIC、timer/SysTick 和 NuttX
   `nx_start()` 路径；
3. 建立 AP fault record，使 CP 能读取 HardFault 寄存器和最后 stage；
4. 第一个 AP kernel task 周期更新 SWAP heartbeat，并可在第二步通过 Mailbox 通知
   CP；
5. 板级 `bk7258_ap status` 显示 AP build ID、stage、heartbeat、last fault 和 sequence。

A1 不启用 AP UART console、RPTUN、RPMsg 或 CPU2。CPU0 的 UART0、Flash 控制、
系统 timer 和现有 NSH 必须不受影响；AP 的时钟、SysTick 和 IRQ 路由不能照抄 CPU0
假设，必须逐项验证。

A1 的 stage 记录应能区分 AP 启动链上的各个断点，例如：

```text
RESET_ENTRY → BSS_READY → NX_START_ENTERED → BOARD_INIT_DONE
            → SCHEDULER_RUNNING → AP_HEARTBEAT
```

`AP_HEARTBEAT` 必须由一个真正被 AP scheduler 调度的线程周期更新，不能由 reset
handler 中的死循环写入；否则它退化为 A0 证据，无法证明 scheduler 已运行。

**A1 验收门**：

- AP 进入 first task，heartbeat 持续递增；
- heartbeat 来源经代码与日志确认为被调度线程，而非 reset handler 循环；
- 板级 `bk7258_ap status` 能同时给出 stage、sequence、heartbeat 计数与最近更新间隔；
- scheduler tick 与上下文切换在真机上稳定；
- AP fault 可被 CP 识别，AP reset/stop/start 不重启 CP；
- 多次冷启动和 AP 局部重启后 CPU0 console 均保持可用；
- 保存固件版本、烧录步骤、UART 日志和观察结果。

### 6.3 A2：CP↔AP RPTUN/RPMsg（AMP）

**目标**：将 CPU0 的 CP NuttX 实例与 CPU1 上的 AP NuttX 实例通过标准
RPTUN/RPMsg 连接。CPU2 仍保持 reset，以免通信和 SMP 两个未知量同时引入。

实施顺序：

1. 根据 CP/AP map 和实际 RPMsg descriptor/buffer 配置，冻结 SWAP/RPMSG_SHM
   最终地址、大小、对齐和 MPU/cache 属性；
2. 保持 `PWR_MNG` 独立 `0x100`。全 OpenVela 路径可扩大并重定义 SWAP；若要求与
   vendor AP 运行时兼容，则保留旧 SWAP 并另划 `RPMSG_SHM`；
3. 实现 BK7258 `rptun_ops_s`：CP 为 master，AP 为 remote，提供 resource table、
   address mapping、start/stop/reset 与 RX callback；
4. Mailbox 只做 doorbell，RPMsg payload、vring 和 buffer pool 全部位于共享 SRAM。

按 §5.2 的建议，A2 内部再拆两步以避免同时引入数据面与中断面两个未知量：

- **A2-1 数据面**：`notify()` 先由轮询线程周期性调用 RX callback，先打通
  resource table、vring、carveout、cache/barrier、endpoint 命名与 `uart_rpmsg`。
- **A2-2 通知面**：接入 §5.2 的 Mailbox doorbell 驱动并移除轮询，单独验证 IRQ
  路由、per-CPU 使能位、FIFO 满处理与 stale notification。

`uart_rpmsg` 配置按 §4.4 的约束落地，两侧 `cpuname` 必须与 BK7258 RPTUN 各自
`get_cpuname()` 的返回值精确一致（下列名称为示意，不得直接照抄）：

```text
CP  : CONFIG_RPMSG_UART=y, CONFIG_RPMSG_UART_CONSOLE=n
      uart_rpmsg_init("<ap-cpuname>", "AP", 4096, false) → /dev/ttyAP
AP  : CONFIG_RPMSG_UART=y, CONFIG_RPMSG_UART_CONSOLE=y
      uart_rpmsg_init("<cp-cpuname>", "AP", 4096, true)  → /dev/ttyAP + /dev/console
```

`uart_rpmsg` 本身不会把 AP 输出自动混入 UART0，CP 侧仍需一个在物理终端与
`/dev/ttyAP` 之间转发字节的工具。**不需要自研**：OpenVela 已提供
`apps/system/cu`，启用 `CONFIG_SYSTEM_CUTERM` 后即可

```text
cp-nsh> cu -l /dev/ttyAP     # 进入 AP NSH
~.                            # 转义符默认 '~'，'~.' 挂断并返回 CP NSH
```

可选 `CONFIG_SYSTEM_CUTERM_DEFAULT_DEVICE` 指定默认设备，`-E` 可改转义符。[N18]

需在真机验证两点，不能预设成立：一是 `cu` 在虚拟 tty 上的 termios/波特率 ioctl 是否
被完整接受（`CONFIG_RPMSG_UART` 确实 select 了 `ARCH_HAVE_SERIAL_TERMIOS`，但未逐项
确认 `cu` 的调用序列）；二是结合 §4.4 的断链/hang 缺陷，AP hang 时 `cu` 的设备侧读
可能永久阻塞，需确认 `~.` 仍能返回 CP NSH。若任一不成立，再考虑自研带超时的转发
命令。无论采用哪种，都不得置于启动关键路径。

**A2 验收门**：

- RPMsg name service 和双向 echo 成功；
- 覆盖不同消息长度、ring wrap-around、buffer exhaustion 和并发 endpoint；
- A2-1 轮询版本先记录一次通过结果，再记录 A2-2 Mailbox 版本结果，两份日志分别归档；
- `ls /dev/ttyAP` 存在，且能从 CP NSH 进入 AP NSH 并执行 `uname -a` 等命令；
- CP→AP 输入与 AP→CP 长输出均正确，长度超过单个 RPMsg buffer 时不丢字节；
- 多次进入/退出 AP console；AP reset 后 endpoint/旧阻塞会话的重连行为单列为后续
  lifecycle 验收，不得从首次会话成功推导；
- AP stop/reset/start 尚未实现，不作为本阶段通过条件；
- 记录 AP hang 情况下 CP 侧访问 `/dev/ttyAP` 的实际行为，并证明 CP NSH 不被拖死；
- cache 压力下 payload 无损坏，CP 控制台和 AP heartbeat 不回退；
- SWAP/RPMSG_SHM 布局、双方 map 和真机日志一起归档。

AP console 迁移到 RPMsg 后，AP 侧的 SWAP stage/heartbeat/fault 记录必须继续保留：
它是 RPMsg 尚未建链、AP crash 或 hang 时唯一不依赖 RPMsg 的诊断通道。

### 6.4 A3：AP 内 CPU1+CPU2 NuttX SMP

**目标**：在同一个 AP `app1.bin` 和 NuttX 实例内，使物理 CPU1/CPU2 组成双核
SMP。CPU0/CP 与整个 AP SMP 域仍然是 AMP。

逻辑 CPU 映射固定为：

```text
AP NuttX logical CPU0 → BK7258 physical CPU1
AP NuttX logical CPU1 → BK7258 physical CPU2
```

实施内容：

1. BK7258 AP chip Kconfig 选择 `ARCH_HAVE_MULTICPU`，AP defconfig 启用
   `CONFIG_SMP=y`、`CONFIG_SMP_NCPUS=2` 和独立 interrupt stack；
2. AP linker 增加 local CPU1 的 vector、idle/IRQ/MSP stack，但仍只有一个 AP
   kernel image 和共享 AP_RAM；
3. `up_cpu_index()` 将物理 ID 1/2 映射为 NuttX 逻辑 ID 0/1；
4. `up_cpu_start(1)` 由 AP logical CPU0 使用 image 内次核 vector 启动物理 CPU2；
5. 实现 `up_cpu_idlestack()`、`up_send_smp_sched()`、`up_send_smp_call()`、目标核
   IPI ISR，以及原子操作、spinlock、barrier 和 cache 维护；
6. 为 AP 内 SMP IPI、CP↔AP RPTUN doorbell 和业务 Mailbox 分配互不冲突的 channel，
   并按 §5.2 在 ISR 内完成来源分派；SMP IPI 与 RPTUN doorbell 复用同一份 Mailbox
   驱动，不新写第二套。

物理 CPU2 应由 AP NuttX 的 `up_cpu_start(1)` 启动，不能由 CP 建模成第二个独立
RPMsg remote。

A3 完成后 AP 侧**仍然只有一个 NSH**（§6.0）。因此不得以“出现第二个 shell”作为
CPU2 已启动的证据，而应由 AP 侧提供可查询的 per-core 状态，例如：

```text
ap-nsh> smpinfo
logical CPU0 -> physical CPU1: online
logical CPU1 -> physical CPU2: online
```

并配合分别绑定到两个 AP CPU 的测试线程，记录 `up_cpu_index()` 返回值、每核计数器、
IPI/SMP-call 次数、跨核调度与 spinlock 结果，以及 CPU2 的 reset/restart 状态。

**A3 验收门**：

- 两个 idle task 均运行，CPU affinity、任务迁移和负载分配正确；
- per-core 状态查询显示 logical CPU0/CPU1 与物理 CPU1/CPU2 的映射均 online；
- 绑核测试线程在两个 AP CPU 上分别计数递增，`up_cpu_index()` 结果与绑定一致；
- NuttX SMP call、调度 IPI、spinlock、atomic 和 IPI storm 测试通过；
- CPU2 启动失败有超时和诊断，不破坏 AP logical CPU0/CP；
- Mailbox ISR 能正确区分 SMP IPI 与 RPTUN doorbell，互不吞中断；
- AP SMP 压力运行不破坏 CP↔AP RPMsg、Flash 协调和 cache 数据；
- 重复冷启和稳定性验收具备完整硬件证据。

### 6.5 A4：外设和业务迁移

最后再决定 LCD、视频、音频、网络等外设由 CP、AP logical CPU0 或 AP logical
CPU1 拥有。外设 IRQ 必须只路由到一个负责域；共享 Flash、DMA 和 cacheable buffer
必须有明确互斥、所有权转移和故障恢复协议。

### 6.6 推荐 PR 拆分

1. `pr/bk7258-ap-l1-image`：AP linker/startup、`app1.bin`、validator、package/decode；
2. `pr/bk7258-ap-cpu1-bringup`：CP lifecycle、SWAP boot record、CPU1 真机证据；
3. `pr/bk7258-ap-nuttx-up`：CPU1 单核 NuttX、timer、heartbeat 和 fault record；
4. `pr/bk7258-cp-ap-rptun`：SWAP/RPMSG_SHM、Mailbox notify 和 RPTUN/RPMsg；
5. `pr/bk7258-ap-smp`：CPU2 启动和 NuttX SMP。若需要修改 `nuttx/` 通用层，按
   仓库边界另行准备上游 PR。

当前第一项工程任务应严格限定为 A0：构建经过 ELF/raw 校验的 AP reset stub，将其
作为 `app1.bin` 打入独立 OpenVela AP profile；CP 只释放物理 CPU1，并通过带 sequence
的 SWAP boot record 证明 CPU1 从已验证的 AP vector base 执行。CPU2、NuttX
scheduler、Mailbox IRQ、RPMsg 和 SMP 均暂不启用。

## 7. 主要风险与尚未证明项

- Armino 源码证明了软件写法，但不能替代 BK7258 TRM 对寄存器位、安全域和 cache
  一致性的定义；ROM/Bootloader 内部如何交接 AP 仍不可见。
- AP 配置同时出现 `CONFIG_SPE=1` 与 `ARM_CM33_NTZ/non_secure` FreeRTOS port 路径，
  不能仅凭名称判断 AP 最终安全态；必须结合编译宏、SAU/PPC 和真机状态验证。
- Armino AP linker 含 generic 第三向量入口，但当前配置只运行两个 AP 逻辑核；移植
  时不能照抄所有 vector/stack 段。
- Armino SDK 工作树在调研时有与本问题无关的本地修改；本报告记录了 revision，并
  以多处未修改配置、linker、startup 和构建脚本交叉验证主结论。
- RPTUN 与 NuttX SMP 可能同时使用 mailbox/doorbell。若不先冻结 channel 与 IRQ
  路由，两种机制会互相吞中断或产生不可诊断的唤醒。
- BK7258 Mailbox v2 的整个 FIFO 仅 8 个 entry（当前按 `2/3/3` 分配）。doorbell 语义
  可合并但不可静默丢弃；FIFO 满时若不置 pending 标志并保留轮询兜底，对端将不再检查
  vring 而直接卡死。[A16]
- `uart_rpmsg` 的 endpoint destroy 未调用 `uart_connected(dev, false)`，不会产生
  `ENOTCONN`/`POLLHUP`，也不会唤醒已阻塞的 read；远端纯 hang 时更没有 UART 层超时。
  CP 侧访问 AP tty 必须自带非阻塞或超时策略，否则可能拖死 CP 的操作路径。[N14][N15]
- 若 AP 选择 `CONFIG_RPMSG_UART_CONSOLE`，参考端口会把 `up_putc()` 实现为空函数，
  AP 将失去不依赖 RPMsg 的早期与致命输出通道。必须保留 SWAP stage/fault 记录作为
  兜底诊断不能在 A2 之后删除。[N16]
- AP 侧 `CONFIG_UART_PRINT_PORT=1` 与 `CONFIG_SYS_PRINT_DEV_MAILBOX=y` 同时存在，
  容易被误读为“AP 已有 UART1 console”。判断 console 归属必须以 transport 配置和
  `bk_printf_init()` 的实际条件为准。[A18][A19]
- UART1 对应 GPIO0/1 有源码依据，但 DevKit 的 USB-UART 是否引出该组管脚尚无板级
  证据。在取得原理图或实测确认前，不得把“AP 独占第二个物理终端”写入实施计划。
- `uart_rpmsg` 两侧 `cpuname` 必须与各自 RPTUN `get_cpuname()` 精确一致，`devname`
  也必须相同。名称不匹配时设备节点仍会注册成功，但 endpoint 永不绑定，表现为“可以
  打开却收不到任何数据”，这类故障没有直接报错。[N14]
- `16 KiB` 只是当前 RPTUN/RPMsg 共享区的推荐起点；在 map、对齐和压力测试完成前，
  不得将其作为已验证的 BK7258 固定布局写入 linker 或打包 profile。
- 当前 bundled AP placeholder 必须保留，新的 OpenVela AP profile 不能覆盖已验证的
  CPU0-only 恢复路径。

## 8. 证据索引与方法

调研先以 AP placeholder、`CONFIG_CPU_CNT`、`CONFIG_SMP`、`app1.bin`、mailbox、
`up_cpu_start` 和 `RPTUN` 做基线搜索，再沿构建、启核、调度和 IPC 调用链定向读取。
共检查超过 20 处独立源码位置；最后五组定向读取没有产生会改变架构结论的新模型，
达到本次源码调研的收敛条件。

逐条来源、revision、可信度和 claim 映射见同目录
[`sources.tsv`](sources.tsv)。主要 claim 为：

- **C1**：Armino BK7258 是 CP/AP 域间 AMP；
- **C2**：AP `app1.bin` 内部是 CPU1+CPU2 双核 SMP；
- **C3**：CP 负责释放 CPU1，AP local CPU0 负责释放物理 CPU2；
- **C4**：NuttX 有正式 SMP 框架，但 BK7258/ARMv8-M 芯片端口缺失；
- **C5**：NuttX/OpenVela 可用 RPTUN/RPMsg/OpenAMP 实现 CP↔AP AMP；
- **C6**：推荐目标是 OpenVela CP↔AP AMP + AP 内 NuttX 双核 SMP；
- **C7**：Armino 的 console 模型是 CP UART0 总控台加 `ap_cmd` Mailbox 单条转发；AP
  CLI 的 transport 是 Mailbox 而非 UART1，且 AP 两核共享一个 CLI；
- **C8**：BK7258 Mailbox 是硬件通知外设，共享内存的布局、所有权、同步与 cache 维护
  全部由软件管理；实现 RPTUN 时需要适配 Mailbox，但只作为 doorbell；
- **C9**：`uart_rpmsg` 是跨域 console 隧道而非核心迁移，要求两侧实例都启用并初始化，
  没有运行时切换命令，且存在断链不通知与 hang 无超时的失效模式；
- **C10**：AP bring-up 证据必须分层，镜像构建打包成功不构成任何 bring-up 结论。

本文是设计/证据调研，不构成 AP、SMP、RPTUN 或任何硬件验收完成声明。

## 9. 来源代号

- [A01] Armino `bk_sdk_project.py:162-173`
- [A02] Armino `auto_partitions.csv:5-9`
- [A03] Armino CP `bk7258.defconfig:7-14,50-57`
- [A04] Armino AP `bk7258_ap.defconfig:8-18,160-168`
- [A05] Armino AP/CP `include/os/os.h` 的 `CPU_ID_OFFSET`
- [A06] Armino CP `system_main.c:144-203`
- [A07] Armino CP `sys_ps_driver.c:77-119`、BK7258 `sys_reg.h:117-167`
- [A08] Armino AP `bk7258_ap_bsp.ld:33-122,543-593`
- [A09] Armino FreeRTOS SMP CM33 port `port.c:1194-1222`
- [A10] Armino SMP `startup_cpu1.c:259-318`
- [A11] Armino CP/AP `mailbox_channel.h`
- [A12] Armino `mb_chnl_buff.c:22-47`、`mb_ipc.h`
- [A13] Armino `cache.c:30-41`、`mb_ipc_cmd.c:159-210,274-282`
- [A14] Armino `pwr_clk.h:46-92`、`flash_shared_lock.c:26-42`
- [A15] Armino `ram_regions.csv:5-10`、`bk_ram_region.py:73-167`
- [A16] Armino Mailbox v2 硬件层：`soc/common/hal/include/mbox0_hal.h:58-67`、
  `soc/common/hal/mbox0_hal.c:55-95`、`driver/mailbox/mbox0_drv.c:7-14,54-66,100-140`；
  选择开关 `driver/mailbox/Kconfig:6-8` 与 `projects/test/platform/*/config`
- [A17] Armino 两级间接与 SWAP 切片：`driver/mailbox/mbox0_adapter.c:113-129`、
  `driver/mailbox/mb_chnl_buff.c:25-48`、`include/driver/mailbox_channel.h:108-126`、
  `include/driver/mb_chnl_buff.h:28-41`
- [A18] Armino CP/AP shell transport 绑定：`cp/components/bk_cli/shell_task.c:26-35,296-307`、
  `ap/components/bk_cli/shell_task.c:25-34,292-304`
- [A19] Armino AP print 端口条件与 dump 旁路：`ap/components/bk_system/printf_base.c:28,56-71`、
  `ap/components/bk_cli/shell_mailbox_cp1.c:269-277`、
  `projects/test/platform/ap/config/bk7258_ap/config:973`
- [A20] Armino `ap_cmd` 转发与 `setprintport`：`cp/components/bk_cli/cli_main.c:1048-1062,1198-1201`、
  `cp/components/bk_cli/shell_task.c:2411-2439`、`cp/components/bk_cli/cli_misc.c:431-454,650-678`
- [A21] Armino AP shell task affinity 与核标识：`ap/components/bk_cli/cli_main.c:1279-1287`、
  `ap/components/bk_cli/shell_task.c:1949-1956`、`ap/components/bk_system/printf.c:138-156`、
  `ap/include/os/os.h:1282-1291`
- [A22] BK7258 Mailbox 中断源与每核使能：`cp/include/soc/bk7258/reg_base.h:72`、
  `cp/middleware/soc/bk7258/soc/icu_map.h:93`、`cp/middleware/soc/bk7258/soc/sys_reg.h:1078-1079`
- [N01] NuttX `sched/Kconfig:405-500`
- [N02] NuttX `nx_start.c:797-813`
- [N03] NuttX `nx_smpstart.c:68-141`、`arch.h:2330-2403`
- [N04] NuttX `percpu.h:40-79`
- [N05] NuttX `sched_smp.c:191-240,437-485`
- [N06] NuttX ARM `Kconfig:964-997`
- [N07] NuttX ARMv8-M `CMakeLists.txt:21-53`
- [N08] NuttX ARMv8-M `Kconfig:138-152`
- [N09] NuttX `drivers/rptun/Kconfig:6-12`
- [N10] NuttX `drivers/rpmsg/Kconfig:6-10`、`rpmsg.c:841-879`
- [N11] NuttX `include/nuttx/rptun/rptun.h:335-397`
- [N12] NuttX `arch/arm/src/nrf53/nrf53_rptun.c`（`notify` 见 `:283-296`，
  IPC callback 见 `:349-366`，shmem/resource table 见 `:80-239`）
- [N13] NuttX `arch/arm/src/stm32h7/stm32_rptun.c`
- [N14] NuttX `drivers/serial/uart_rpmsg.c`：`uart_rpmsg_init` 与设备注册
  `:426-497`、`isconsole` 写入 `:440-443`、endpoint 创建条件 `:331-347`、
  endpoint destroy `:350-364`；配置见 `drivers/serial/Kconfig:160-178`
- [N15] NuttX `drivers/serial/serial.c`：console 跳过 `setup()` `:710`、
  `O_NONBLOCK` 读写 `:1122-1179,1481-1488`、`VMIN/VTIME` 超时 `:1201-1229`、
  正确断连机制 `uart_connected()` `:2231`
- [N16] NuttX console 唯一性检查 `drivers/drivers_initialize.c:85-89`、
  `rpmsg_serialinit()` 调用时序 `:192-212`；RPMsg console 下 `up_putc()` 置空的参考实现
  `arch/risc-v/src/k230/k230_start.c:193-196`
- [N17] NuttX RPTUN notify/callback 通路 `drivers/rptun/rptun.c:254-260,791-797`
- [N18] OpenVela `apps/system/cu`：参数解析 `cu_main.c:361`（`getopt "l:s:ceE:fho?"`）、
  转义符默认 `~` `:343`、`~.` 挂断提示 `:287`；默认设备配置示例
  `nuttx/boards/risc-v/k230/canmv230/configs/master/defconfig:103-104`
- [N19] NuttX RPTUN 标准控制 ioctl `include/nuttx/rptun/rptun.h:45-47`、
  `drivers/rptun/rptun.c:1017-1046`（`RPTUNIOC_START/STOP/RESET` 经
  `/dev/rptun/<cpuname>` 驱动 `rptun_ops.start/stop/reset`）
- [N20] NuttX linker script 经 cpp 预处理、共享区可按 `CONFIG_RPTUN` 条件伸缩：
  `boards/arm/nrf53/nrf5340-dk/scripts/flash_app.ld:26-35`；同板
  `CONFIG_RAM_START` 与 `.ld ORIGIN` 为两处独立硬编码，属手工对齐
  `configs/adc_cpuapp/defconfig:42-43`
- [N21] NuttX heap 上界惯例使用 `CONFIG_RAM_END`
  `arch/arm/src/nrf53/nrf53_allocateheap.c:116-141`
- [N22] NuttX pseudo-AMP 启核标准 hook：`include/sys/boardctl.h:199`
  （`BOARDIOC_START_CPU`）、`boards/boardctl.c:869-882`（ARG 为 cpu core id）、
  `include/nuttx/board.h:861-872`（`board_start_cpu(int cpuid)`，注释明确用于
  pseudo AMP 启动 slave core）、`boards/Kconfig:4838-4845,5006-5013`
  （`CONFIG_BOARDCTL` / `CONFIG_BOARDCTL_START_CPU`，后者无 `depends on !UP`）、
  调用先例 `apps/system/init/builtin.c:157-170`
- [T01] 团队仓库 CP console/NSH 配置 `board/bk7258-devkit/configs/cp/defconfig:35,37,46,53`
- [T02] 团队仓库 UART0 console 注册 `chips/bk7258/bk7258_uart.c:217-228`；AP
  placeholder `board/bk7258-devkit/configs/ap/defconfig:1-9`
- [T03] `CONFIG_BOARDCTL` 已被间接启用：`configs/cp/defconfig:39`
  （`CONFIG_NSH_ARCHINIT=y`）→ `apps/nshlib/Kconfig:1133-1141`（`select BOARDCTL`）；
  `board_app_initialize()` 见 `board/bk7258-devkit/src/bk7258_bringup.c:47`；
  生成配置中 `# CONFIG_BOARDCTL_START_CPU is not set`（实测 `cmake_out/*/.config`）
- [V01] OpenVela `docs/en/device_dev_guide/kernel/KernelDev.md:65-95` 与
  `security/security_configuration.md:31-40`
- [V02] 飞书 Wiki《核间通讯框架》，node token
  `Tq2jwu1U2iYwJlk8UQ3cUkhdnYY`，revision 4，“Rpmsg Physical Layer”与
  “Rpmsg Transport Layer”

## 10. A3 实现状态

§6.4 描述的 A3（AP 内 CPU1+CPU2 NuttX SMP）已在团队仓库落地为可构建实现，
CP↔AP 仍保持 AMP：

- `chips/bk7258/bk7258_smp.c`：`up_cpu_idlestack` / `up_cpu_start` /
  `up_send_smp_sched` / `up_send_smp_call` / 每核 interrupt stack /
  `up_get_intstackbase`，以及物理 CPU2 次核启动入口和 `_vectors_core1`
  次核向量表；
- `chips/bk7258/bk7258_cpuindex.c`：`up_cpu_index()` 用 MSP 与 link-time
  常量（每核 intstack top / CPU2 boot stack top）区分逻辑核，无共享可变全局；
- `chips/bk7258/bk7258_mailbox.c`：Mailbox 本地 channel 改为跟随运行核，
  ISR 按 `BK7258_MBOX_SMP_MAGIC` 在 RPTUN doorbell 与 AP SMP IPI 之间 demux；
- `board/bk7258-devkit/scripts/flash_ap.ld`：新增 `.vectors_core1`（512 对齐）
  与 CPU2 boot stack；`configs/ap/defconfig` 启用 `CONFIG_SMP=y`/
  `CONFIG_SMP_NCPUS=2`；
- `configs/ap/README.md` §A3 记录了移植细节与硬件验证边界。

本仓库的 ELF/raw L1 validator 已验证镜像存在 `_vectors_core1`、`up_cpu_start`
等符号，`app1.bin` 与 ELF PT_LOAD 一致。**硬件寄存器（`SYS_CPU2_CTRL`
0x44010018、`SYS_CPU2_INT_EN_HI` 0x44010094、CPU2 状态位）按 CPU1 既有位域
外推，尚未经 BK7258 TRM 交叉核对；真机运行前必须按 README §A3 的说明记录
寄存器回读与 CPU2 heartbeat，才构成 A3 验收。**
