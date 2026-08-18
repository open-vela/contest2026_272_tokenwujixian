# BK7258 DevKit CPU0 bring-up

此目录承载 BK7258 DevKit 的开发板差异代码，并由仓库 manifest 映射到 `vendor/beken/boards/bk7258/bk7258-devkit`。

芯片通用的启动、时钟、中断、定时器和控制器实现属于 `chips/bk7258/`，不应放入此目录。这里未来放置该板的 defconfig、链接脚本、pinmux、调试 UART、LED、按键以及实际使用的 I2C/SPI 实例。

BK7258 使用两个独立的组件配置，而不是一个合并的多核 NuttX image：

```text
configs/cp/  → CPU0/CP 的 raw app.bin，拥有 UART0 并释放 CPU1
configs/ap/  → CPU1/AP 的 single-core OpenVela raw app1.bin
```

两者拥有独立 linker、startup、RAM、L1 validator 和构建目录。AP 不启用物理
UART、CPU2 或 SMP；它通过 RPTUN/RPMsg VirtIO 与 Mailbox IRQ79 提供标准
RPMsg UART `/dev/console` 和 NSH，CP 侧以 `cu -l /dev/ttyAP` 进入。AP 独立健康
线程仍每秒向 SWAP 更新 heartbeat；CP 静默监测该记录，仅在 stage 变化、fault、
记录失效或 heartbeat 停滞时输出。raw
`app.bin`/`app1.bin` 仅是 L1 组件输入，不能连续写到 CP physical Flash offset
`0x11000`；可用于 Flash cold boot 的是 L2-dev `all-app.bin`。

当前目录已具备 BK7258 DevKit 的 CP/AP OpenVela AMP bring-up：双侧组件构建、CPU1
启动、共享 SRAM、RPMsg VirtIO、Mailbox doorbell 和双向 ACK ping 均有真机证据。
RPMsg UART + AP NSH 已通过双侧 L1 和 L2 decode，仍需以 `/dev/ttyAP` 的首次交互
会话补齐真机验收。Wi-Fi/BLE、USB、LCD/按键、AP restart/reconnect、Flash 运行时
访问和长期稳定性仍不在当前已验证范围。

完整镜像始终从物理 Flash `0x0` 写入。无参数打包仍生成安全的 CP + 66 字节
placeholder 恢复包；OpenVela AP 必须显式使用 `--openvela-ap`，并携带能由打包器
现场重放的 L1 validation report。CP 启核前还会验证 AP 向量和固定 `OVAP` 契约，
placeholder/vendor AP 不会被误释放。`0x11000` 是 CP 分区起点；不得将 raw
`app.bin` 连续写入该位置。

最近的 UART TX FIFO-drain 修复已通过 L1/L2 离线验证，并于 2026-08-12 由操作者在真机确认：单次 `?`/`help` 长输出无需额外按键即可完成。测试时将 minicom 配置为 115200 8N1，并关闭硬件和软件流控；本次未保存 UART 原始日志。

## 已知 bring-up 故障与恢复

- 若 `bk_loader` 在 `LinkCheck Timeout` / `GetBus fail` 停止，失败发生在镜像解析、擦写之前。重新进入下载模式；需要单独诊断 CH340/BootROM 通道时，再以只读 `Read Flash OK` 握手确认。不要把它归因于 `all-app.bin` 或 CP raw 内容。
- 已修复首个 `svc 0` 上下文切换的 HardFault：SVC 必须使用 NuttX 的 `0x60` 优先级，而 SysTick 使用普通 `0x80` 优先级。保留详细 fault 日志以供后续异常定位。
- 已修复 console RX setup 被 NuttX console 特例跳过的问题：在注册 `/dev/console` 前配置 GPIO10 与 `RX_EN`。已在真机用 `?` 验证输入。
- 已修复 TX FIFO-ready 中断没有 drain 软件缓冲区的问题。2026-08-12 已由操作者在真机确认单次 `?`/`help` 长输出无需额外按键即可完成；本次未保存 UART 原始日志。

故障的完整现象、证据、映像边界和后续验收见
`chips/bk7258/BK7258_CPU0_BRINGUP_CONTRACT.md`。工作区外的详细调查笔记不
属于同步后的团队交付物。

## 同步后的复现入口

同步仓库后，使用 `packaging/README.md` 中的端到端 Runbook：它列出 CP 构建、
profile gate、L2 打包、独立 decode、只读下载握手、完整镜像烧录和 minicom 验收。
版本固定的 Linux `bk_loader` 位于 `tools/bk_loader`，其 SHA-256 必须为
`55221d83d5582c362aab27d8883d799acf5eaa7b735d51135e01b0a24156b9ab`。

厂商输入路径不由环境变量选择。未来包装器固定从
`<openvela-workspace>/local/bk7258-vendor-inputs.cmake` 读取本机路径（即本仓库
上一级、含 `build.sh` 与 `nuttx/` 的工作区目录）；可从
`packaging/bk7258-vendor-inputs.cmake.example` 创建该 local-only 文件。它只定位
外部 SDK/build 资产，实际 profile 仍由受控 SHA-256、分区 JSON 和 manifest 决定。

已从 `AIDK_AI玩具开发板_原理图.pdf` 确认调试 UART0：P11/DL_0TX 连接板级 `TX0`，P10/DL_0RX 连接板级 `RX0`，并通过板载 USB-to-UART 电路引出。当前 26 MHz / 115200、GPIO10/11 mux、UART RX/TX 及 CPU0 中断路径均已有真机 CPU0 控制台证据；完整命令、镜像哈希、故障分析和仍待验证的边界记录在 `chips/bk7258/BK7258_CPU0_BRINGUP_CONTRACT.md`。
