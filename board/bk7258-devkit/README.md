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

## JD9853 显示外设驱动

`src/jd9853.c` 实现了 JD9853（Jadard）4-wire SPI TFT LCD 下半区驱动，挂在 DevKit 12-pin 显示 FPC 上。板端信号与 SoC GPIO 的映射见 `include/board.h`（`BOARD_LCD_*_PIN`）：

- SCL/SDA/D/C/CS 走 LCD_QSPI1_ 网。该 "QSPI1" 网在 SoC 侧对应 **SPI1 控制器** 的四个 pad（GPIO2/3/4/5，Armino SDK `gpio_map.h` mux option 0）。默认仍走 `src/bk7258_spi_bitbang.c` 的 GPIO 位带实现（真机已验证可启动、可点亮）；`src/bk7258_spi_hw.c` 提供了硬件 SPI1 主模式移植（SCK=GPIO2、MOSI=GPIO4 走第二功能；CS=GPIO3、D/C=GPIO5 保持普通 GPIO），可在 "LCD1 display SPI transport" Kconfig 选择里启用，但**尚未在真机验证——若 SPI1 控制器在 CPU0 侧不可达，寄存器访问会导致上电卡死**，启用时 `bk7258_spi_initialize()` 里已埋串口探针用于定位；
- RESET 默认 GPIO45、背光 `LCD_BL_PWM` 为 GPIO25（原理图 P25/PWM0_5），均可用 `BOARD_LCD_RST_PIN`/`BOARD_LCD_BL_PIN` 调整。

初始化命令序列来自开源 ESP-IDF JD9853 面板驱动（`mydazy/esp_lcd_jd9853`，Apache-2.0），分辨率为 240x296（T201BM-C12-03），可用 `CONFIG_LCD_JD9853_XRES/YRES/XOFFSET/YOFFSET` 调整。`CONFIG_LCD_JD9853=y` 会拉起 LCD/SPI/SPI1 控制器（`CONFIG_BK7258_SPI=y`，同时选择 `SPI_CMDDATA`）；`board_app_initialize()` 在 CP NSH 下注册 `/dev/lcd0`。该驱动当前只在真机 CP UART 路径上验证过构建与链接，尚未做真机点亮验收。

SPI1 控制器的波特率由 XTAL 26 MHz 分频得到：`baud = 26 MHz / (2 * clk_rate)`，因此只能取约 13/6.5/4.33 MHz 等档位。`CONFIG_LCD_JD9853_FREQUENCY` 默认 6.5 MHz（clk_rate=2）；GC9D01 默认 13 MHz。像素数据在 `jd9853_wrram()/gc9d01_wrram()` 中先按 RGB565 大端序字节交换到 2 KB 暂存区，再以 `SPI_SNDBLOCK` 分块发送，避免逐像素两次 8-bit 单发。

## LVGL 图形栈（lvgldemo 与 LVGL 测试用例）

defconfig 已启用 `CONFIG_EXAMPLES_LVGLDEMO=y` + `CONFIG_GRAPHICS_LVGL=y`，LVGL 通过 NuttX LCD 端口（`LV_USE_NUTTX_LCD=y`）直接使用 `/dev/lcd0`（走 `LCDDEVIO_PUTAREA`，非 framebuffer），无需 `CONFIG_LCD_FRAMEBUFFER`。内存受限，采用 `LV_NUTTX_LCD_CUSTOM_BUFFER=y`（40 行局部绘制缓冲，约 19 KB），并让 LVGL 走 C 库堆（`LV_USE_CLIB_MALLOC/STRING/SPRINTF=y`）。

上电进入 NSH 后运行对应 LVGL 测试用例（对应 openvela XTS 用例 4.1.124 / 4.1.123）：

```text
nsh> lvgldemo widgets      # GUI widgets 基本功能测试
nsh> lvgldemo stress       # GUI 压力测试
```

目前固件仅验证到构建/链接与 L1 校验通过；真机点亮、LVGL 渲染与内存占用（栈 32 KB + 绘制缓冲 19 KB + LVGL 对象）需在真机验收。若运行时堆不足，可下调 `CONFIG_LV_NUTTX_LCD_BUFFER_SIZE` 或 `CONFIG_EXAMPLES_LVGLDEMO_STACKSIZE`。

