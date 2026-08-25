---
feature: bk7258-gdma
status: hardware-verified
updated: 2026-08-25
branch: feature/bk7258-gdma
---

# BK7258 GDMA0 适配

## Report

## [S1] Problem

BK7258 DevKit 当前没有可供 NuttX 外设下半部复用的通用 DMA 控制器适配。LCD 的 SPI1 传输仍以 FIFO 轮询实现，无法安全地将 DMA 用于后续 SPI、UART、I2S 或存储驱动，也没有可重复的片上 SRAM DMA 真机证据。

2026-08-25 的首轮板上测试执行 `bk7258_gdma_test`，结果为 `ret=-110`（超时）且 `callbacks=0`；这证明当时没有完成或错误中断回调，不能视为 GDMA 已通过真机验收。随后按 Armino BK7258 CPU0/CP 初始化路径，在 GDMA0 初始化中将 `prio_mode` 清零以关闭遗留 `bps_clk_gate`/优先级状态，再写 soft-reset；同时保留 CPU0 ICU 源 11 gate、IRQ 计数和事件快照诊断。使用 fresh `--placeholder` 构建生成的 `all-app.bin`（SHA-256 `4e55ee91609a8ac5baa43619b85b556688b19857e415367fff361ae9ae8102b7`）已在真机烧录验证：

```text
nsh> bk7258_gdma_test
[bk7258_gdma_test] start handle=17 src=0x28071abc dst=0x280716ac bytes=512
[bk7258_gdma_test] PASS channel=17 bytes=512 callbacks=1
```

这证明 secure-CP GDMA0 单次 SRAM→SRAM 传输与完成中断回调均已通过真机验收。

## [S2] Design

首版只实现 CPU0/CP 域可用的 GDMA0：控制器基址为 `0x45020000`，有 8 个硬件通道，使用 ICU 源 11（NuttX IRQ `16 + 11`）。`CONFIG_ARCH_DMA` 与 `CONFIG_BK7258_GDMA` 共同启用时，`arm_dma_initialize()` 在 NuttX 常规架构初始化阶段先将全局 `prio_mode` 清零以关闭遗留的 `bps_clk_gate`，再写 soft-reset，随后复位通道、清除残留状态、先挂接再使能 GDMA0 IRQ。每次启动也重新打开 CPU0 source 11 与 NVIC，避免其他启动阶段清除该路由。

当前 CP 是 TrustZone secure 镜像，因此初始化遵循 Armino BK7258 `CONFIG_SPE=1` 路径：GDMA 全局 `secure_attr` 和 `privileged_attr` 写为 `0x0fff`，每次传输的 source/destination request attribute 均标记 secure；`REQ_MUX` 的 source/destination secure 位分别为 bit 20/21，bus-error interrupt enable 为 bit 22。该私有 API 当前不支持非安全 DMA；未来引入 non-secure handoff 或 non-secure DMA 用户时，必须重新设计通道归属与 transaction security policy，不能沿用本配置。

芯片私有头文件提供不依赖厂商 SDK 的异步 DMA 接口：分配返回不透明、带代际（generation）的整数 `BK7258_DMA_HANDLE`，配置单次传输的源端、目的端、请求源、8/16/32-bit 数据宽度及地址递增属性，随后以完成回调启动或停止传输。`bk7258_dmafree()` 返回 `int`；无可用通道返回空句柄，失效、过期或重复释放的句柄以及无效配置或状态转换均返回负 errno。首版只承诺请求源 `BK7258_DMA_REQ_MEMORY`（硬件 mux 0）与单次模式；长度必须为 1..65536 字节且为两端数据宽度的整数倍，地址必须按数据宽度对齐。一个已分配通道同一时刻最多运行一次传输。

ISR 只读取并 W1C 清除该通道的完成、半完成和总线错误状态，更新受临界区保护的通道状态，并在完成或错误时调用一次已登记回调。回调在中断上下文执行，因而不得阻塞；驱动在解锁后调用它，故回调可以配置、重启、停止或释放该通道。总线错误以 `-EIO` 报告。ISR 在持锁时保存完成传输的代际句柄，在解锁后才调用回调，避免并发释放/重分配改变回调可见的句柄。停止、完成或释放通道时，驱动清除控制寄存器、请求 mux（包括总线错误中断使能）、残留状态和回调，杜绝旧回调作用到后来重新分配的通道。

新增一个由可见的 `CONFIG_LVX_USE_DEMO_CONTEST2026_272_BK7258_GDMA_TEST` 选择、并进而启用 `CONFIG_BK7258_GDMA_TEST` 的 NSH 测试命令。它以对齐的静态源/目的 SRAM 缓冲进行确定性模式复制，使用单调时钟等待 DMA 完成事件，在超时或错误时停止并报告失败；成功时验证完整载荷、源缓冲未变、目的缓冲两端 guard 字节未变、一次回调以及正常、重复释放和过期句柄等生命周期拒绝路径，并输出所用通道、字节数和通过标记。该命令是 SRAM→SRAM 真机验收的唯一证据入口。

DMA 缓冲必须位于 GDMA0 可访问的片上 SRAM，且调用者负责在未来启用数据缓存时按平台缓存合同完成源 clean 与目的 invalidate；当前 CPU0 启动路径关闭继承的 D-cache。首版不接受 DMA1、安全/非安全属性切换、scatter-gather、链式描述符、循环传输、外设请求、SPI/LCD 改动或自动缓存维护。

## [S3] Out of Scope

- DMA1（包括 IRQ 56/57 和跨安全域归属）
- SPI1 LCD、UART、I2S、SDIO、Wi-Fi 或其他外设的 DMA 接入
- repeat/loop 模式、半完成 API、scatter-gather、2D DMA、DMA2D 与零拷贝
- 缓存可见性策略变更、DMA 专用分配器或用户态 `/dev` DMA 接口
- 未经明确授权的烧录、Flash 擦写或对开发板产生的硬件操作

## Tasks

- [x] T1: 添加 GDMA0 寄存器定义、Kconfig/构建接入和芯片私有 DMA API。— acceptance: 在 `CONFIG_ARCH_DMA=y` 与 `CONFIG_BK7258_GDMA=y` 的 CP 配置中，芯片层编译并导出分配、配置、启动、停止与释放接口。 (covers: S2)
- [x] T2: 实现 GDMA0 通道生命周期、IRQ 分发和错误清理。— acceptance: 静态测试覆盖参数/状态拒绝路径；实现对通道 0..7 的 W1C 清除、一次回调和释放后失效语义。 (covers: S2; depends: T1)
- [x] T3: 添加受 Kconfig 控制的 SRAM→SRAM NSH 验证命令及 manifest/build 接入。— acceptance: 构建产物包含该命令；命令验证载荷、源不变和 guard，不因超时而无限等待。 (covers: S2; depends: T2)
- [x] T4: 在隔离工作树完成构建、静态检查和真机 SRAM→SRAM 记录。— acceptance: fresh `--placeholder` 构建的 L1/L2 均通过，`all-app.bin` SHA-256 为 `4e55ee91609a8ac5baa43619b85b556688b19857e415367fff361ae9ae8102b7`；烧录后执行 `nsh> bk7258_gdma_test`，得到 `PASS channel=17 bytes=512 callbacks=1`。 (covers: S1, S2; depends: T3)
