---
feature: bk7258-wifi-native
status: phase-one-accepted
updated: 2026-10-06
branch: feat/bk7258-wifi-native
commits:
---

# BK7258 Vela 原生 Wi-Fi 适配

## Report

## [S1] Problem

BK7258 CP 上的 Wi-Fi 要在 NuttX 下工作，目标有两条：

1. 上层完全走 NuttX：netdev、NuttX 网络栈、wapi/ioctl。
2. 闭源库（`libwifi.a`、闭源 `libbk_phy.a`）依赖的硬件服务，最终全部由 NuttX 驱动提供。

闭源库原样复用。

以往做法的问题：先挑一部分 Armino 源码，再手写 shim 模拟其余部分，然后上板试错。每次语义对不上，都要上板一轮才能发现。本地 `dev`（`9550111`）比 `origin/dev-ai-contest-2026`（`a7de5d2`）多 81 个提交，用户确认 `ac1_tx_dma_dead` 就出现在这 81 个提交之后。

起点 `a7de5d2` 的现状：2026-10-01 在热点 `<SSID>` 上试了 3 次，只成功 1 次。

- 第 1 次失败：扫描发出后一直没有 `scanu_confirm`。
- 第 2 次失败：没有 `auth_send`，随后 `Authentication timed out`、`rw_msg_send timeout for 7172`，此后扫描结果一直为空。
- 第 3 次成功，见 `evidence-20261001/t1-a7de5d2-connect-ping.log`：
  - 第 554 行 `auth_send`，第 2161 行 `COMPLETED`；
  - 第 2838 行拿到地址 `<LAN-IP>`；
  - 第 2926 行 ping 公网 10/10；
  - 全程 0 次 `tx_dma_dead`；
  - 第 2347、2663 行出现 `COMPLETED -> 4WAY_HANDSHAKE`，即连上之后又重新握手了 2 次。Armino iperf 参照日志（0904 镜像，10-01 板测）里这种重握手是 0 次。

## [S2] Design

### [S2.1] 参照基线

- 参照源码：Armino `~/armino/.worktrees/baseline-gcc13`，分支 `baseline/d2ded03-gcc13`，提交 `d5b0278`。它是 `d2ded03` 加 3 处 GCC 13.4 编译修复：cp/ap 的 `compile-options.cmake` 加 `-Wno-error` 和 `-includestdint.h`，`wpa_supplicant-2.10/src/utils/os_none.c` 加 `#include <time.h>`。
- 参照工程：`make bk7258 PROJECT=wifi/iperf`。
- 参照证据：2026-10-01 板测，PM 和 STA_PS 开启，连接、DHCP 正常，`ping 223.5.5.5` 4/4。
- 闭源库身份：`libwifi.a` sha256 前缀 `6e7e018c`，闭源 `libbk_phy.a` 前缀 `fb3e8f6a`，与本仓 prebuilt 一致。
- 规格输入：该构建产出的 `build/bk7258/iperf/bk7258/{compile_commands.json,app.map,config/sdkconfig.h}`。CP 共 539 个编译单元。

### [S2.2] 两阶段

阶段一：先在 NuttX 上原样跑通。

- 编译单元集合 = T4 清单的 538 个单元，减去 S2.7 排除记录里的单元。默认全部保留。
- 每个保留单元都用基线原件，以及原来的 `-D`、`-I`、`-include` 和编译选项。CMake 源码列表和逐文件编译参数由脚本从 `compile_commands.json` 生成，不手写。
- 配置使用基线生成的 `sdkconfig.h`。
- 手写代码只允许出现在三处：FreeRTOS API 到 NuttX 的接缝（T9）；netdev 胶水（T10）；启动入口 `chips/bk7258/wifi_armino/bk7258_armino_start.c`，它在 NuttX 内核线程里代替基线的 `Reset_Handler → entry_main → 调度器 → main()` 这条链（这条链和 NuttX 启动冲突），调用基线 `projects/wifi/iperf/cp/cp_main.c` 里的入口。
- 每个组件建成一个静态库（`nuttx_add_kernel_library`），进入 NuttX 链接时的 `--start-group`，和基线一样只拉入被引用的成员。不直接编进 `arch`，否则所有成员都会被强制链入。
- libc 头用 NuttX 的：NuttX 给每个库加的 `-isystem nuttx/include` 会让 Armino 单元按 NuttX 的结构布局编译，最终链接的也是 NuttX libc。这是对"原参数"的第一处有意偏离。
- 第二处偏离：保留组件参数里的 lwip 裸头目录 `lwip-2.1.2/src/include/lwip` 被原位替换为 `chips/bk7258/wifi_armino/lwip_compat`。原因：lwip 已排除，而这个目录里的 `errno.h`、`netdb.h`（通用 POSIX 头，与 NuttX 重复；`errno.h` 的 122 个 `E*` 数值与 NuttX 完全一致）以及 `debug.h`、`err.h`（同名不同义）会遮蔽 NuttX 自己的头，连 NuttX 内部的 `#include <errno.h>` 也被劫持。通用头只用 NuttX 的一份；`lwip_compat` 只转发基线依赖记录证明解析到这里的 lwip 独有头（`pbuf.h`、`netif.h`），属于 T10 的范围。
- 第三处偏离（2026-10-03，用户决定"A"）：基线最终链接带 `-Wl,--wrap=malloc,calloc,realloc,free,zalloc` 及其 `_r` 变体（`build.ninja`），所有 Armino 和闭源库的分配都经 `bk_rtos/platform_stub.c` 的 `__wrap_malloc` → `os_malloc` → `pvPortMalloc`，后者在 compat 里返回清零内存，闭源库依赖这一点。我们不加全局 `--wrap`，因为那会把 NuttX 自己的分配也改道到 `os_malloc`。改为只对 Armino 代码做同样的改名：每个 Armino 组件库归档后跑一次 `objcopy --redefine-syms`；6 个闭源 `.a` 复制到构建目录后再改名，基线目录不动（`chips/bk7258/wifi_armino/CMakeLists.txt`）。改名表只含 `platform_stub.c` 包装的 9 个名字，没有任何 Armino 单元定义它们，所以不会递归。依据：2026-10-03 第三次上板在 `sta_mgmt_init()` → `co_list_pop_front` 因链表指针为 `0x55555555` 触发非对齐访问，该表由 libwifi `mm.c.obj` 直接 `malloc` 分配、未清零。改后 `nm`：libwifi、libbk_phy 只引用 `__wrap_malloc`/`__wrap_free`，Armino 组件库不再引用裸分配函数；compat 层（编进 `arch`）仍用 NuttX `zalloc`，两条路径最终落在同一个 NuttX 堆上，交叉释放安全。基线的 `printf`、`snprintf` 等打印类 `--wrap` 未照做，Armino 打印走 NuttX libc。
- 第四处偏离（2026-10-03，用户决定"A"）：基线把 `ram_regions.h`（由 SRAM 分区 CSV 生成）`-include` 进 38 个组件，那里的 `CONFIG_PWR_MNG_ADDR` 是基线自己的 `0x2805db00`，而本板的 PWR_MNG 在 `0x2809f700`。`bk_pm_module_lv_sleep_state_get/clear` 和 `pm_debug_lv_state` 读写那个地址，所以不换会直接写 AP 的内存（`0x2805db00` 落在 AP 窗口里）。改为 `-include` 我们自己的 `chips/bk7258/pm/nuttx_port/include/ram_regions.h`（它把 `CONFIG_PWR_MNG_ADDR` 指向 `BK7258_PWR_MNG_BASE`），同时把基线的 partitions `-I` 换成我们的目录在前、基线目录在后：`pwr_clk.h` 按名字包含 `ram_regions.h`，只换 `-include` 不换 `-I` 的话后一次包含会把基线那份再读回来，把改动抵消掉。PSRAM 的几个宏（`CP_PSRAM_HEAP_*`、`AP_PSRAM_HEAP_*`、`PSRAM_CAPACITY`）来自权威 CSV，在 `bk7258_memorymap.h` 里补上，因为 `mem_arch.c`、`heap_4.c`、`trap_base.c`、`psram_driver.c` 要用它们做指针范围检查。改后 `nm`：那 3 个函数的字面量从 `0x2805db00` 变成 `0x2809f700`，ELF 里 `0x2805db00` 0 处。
- 第五处偏离（2026-10-03，用户决定"A"）：CP/AP 的 SRAM 分界不是固定值，而是 Kconfig 里的 `CONFIG_BK7258_RPMSG_SHM_ADDR`。基线 CP 有 311 KB SRAM，我们的 CP 只有 195 KB，放不下 lwip 的 `ram_heap`（51219 字节）。从 AP 划 64 KB 给 CP（AP 运行时堆峰值只有 40 KB，见 2026-10-03 板上 `free` 输出 `maxused 40496`），新分界为 AP `0x28010000..0x28054000`、RPMsg `0x28054000..0x2805ec00`、CP `0x2805ec00..0x2809f700`（CP 259 KB）。只在 `cp-armino` 和新建的 `ap-armino` 里设置新值；标准 `cp`/`ap` 仍用旧值，构建产物逐字节不变（`app.bin` `1c8011f5…`、`app1.bin` `7f4e6d79…`，2026-10-03 复核）。`BK7258_AP_RAM_SIZE` 改为从 `CONFIG_BK7258_RPMSG_SHM_ADDR` 推算，`flash.ld`/`flash_ap.ld` 的断言和 L1 校验脚本同步改为从 `.config` 读取；打包 profile 增加 `bk7258-devkit-openvela-ap-armino.json`（解码器两种名字都接受），`bk7258-package.sh` 增加 `--profile`。
- 源码位置：阶段一通过一个必填的 CMake 变量 `BK7258_ARMINO_BASELINE_DIR` 指向本机 Armino 基线，变量没设置就报错，不在构建文件里写死本机路径。跑通后按 AGENTS.md 把用到的文件导入 `chips/bk7258/` 并保留来源（T19）。
- 阶段一放在一个新的 Kconfig 开关后面，开关关闭时现有 Wi-Fi 构建不受影响。

阶段二：硬件服务原生化。

- 按 S2.5 的范围，每次只把一个硬件目标文件换成 NuttX 原生驱动。
- 每次替换后，都用 T5 的日志对比和 Armino 参照比较，并上板验收。
- 某一步失败，问题只可能出在这一次替换上。

### [S2.3] 源码一致性规则

阶段一里每个保留的 Armino 源文件，都必须和基线 `d5b0278` 的同路径文件逐字节一致，由比对脚本做门禁，例外清单为空。

`a7de5d2` 的 `chips/bk7258/wifi/armino/` 共 750 个源文件和头文件（T3 统计）：704 个一致，43 个改过（`.c` 32、`.h` 11），3 个在基线里没有对应文件（`.c` 1、`.h` 2）。阶段一不沿用这些改过的文件，直接取基线原文件。

### [S2.4] 加密

阶段一和基线保持一致：wpa 使用 `crypto_mbedtls.c`，配合 Armino `psa_mbedtls` 3.5.2，二者都在基线编译单元里。

这条会替换 2026-10-01 早先定下的"先用 wpa 内部加密"：那是"挑一部分源码"做法下的选择，和 S2.2 的原样原则冲突。

迁移到 NuttX mbedtls 3.4.0 见 S3。

### [S2.5] 硬件服务契约（阶段二）

范围的确定方法：从基线 `app.map` 的 Cross Reference 表出发，由闭源库开始，经过开源逻辑层往下追。

结果是 79 个硬件目标文件，其中 27 个被 Wi-Fi 侧直接引用：

- `pm`、`sys_pm_hal`
- `adc_driver`、`aon_rtc_driver_64bit`、`ckmn_driver`、`dma_driver`
- `flash_driver`、`flash_driver_ext`、`flash_partition`
- `gpio_driver`、`gpio_driver_base`
- `interrupt_base`、`mailbox_channel`、`mb_ipc`、`mb_ipc_heartbeat`
- `otp_driver_v1_1`、`printf`、`reset_reason`、`sdmadc_driver`
- `sys_int_driver`、`sys_ps_driver`、`sys_wifi_driver`
- `timer_driver`、`trng_driver`、`uart_driver`、`wdt_driver`、`trap_base`

阶段二必须把这 27 个全部原生化。另外 52 个只有在被保留代码实际引用时才纳入。

每个目标文件替换时：

- 要提供的符号集合以 T2 生成的表为准；
- 语义以基线源码为准；
- 验证方法见 S2.6。

**2026-10-07 补记（用户决定，原话："要原生化的不止是pm/电源吧，毕竟编译进来了这么多东西"）**：上面的 27 个是「Wi-Fi 侧直接引用」的优先集，**不是阶段二的全部范围**。对 `cp-armino` 镜像的链接表（`668fb2a` 构建，`nuttx.map`）按外设归类，真正链入的硬件类目标文件是 **117 个**，全表如下。标记：**[必换]** = 上述 27 个；**[可换]** = NuttX 有对应驱动但不在 27 个里；**[必留]** = 闭源库接口面，只能留 Armino 形态；**[待定]** = 工具/杂项，逐个小件定。基线：本表之前 S2.5 只有 27 个的清单。

| 类别 | 模块 | 目标文件 | 标记 |
|---|---|---|---|
| 电源 | PM | `pm` `pm_misc` `low_vol` `pwr_clk` `low_pwr_core` `low_pwr_misc` | [必换]`pm` + [可换] |
| 电源 | AON PMU | `aon_pmu_driver` `aon_pmu_hal` | [可换] |
| 电源 | 电源状态 | `sys_pm_hal` `sys_ps_driver` `sys_psram_driver` `sys_wifi_driver` | [必换]`sys_pm_hal` `sys_ps_driver` `sys_wifi_driver` + [可换] |
| 电源 | 低电 | `low_vol` | [可换] |
| 时钟 | 时钟/CMN | `sys_clock_driver` `ckmn_driver` `ckmn_hal` `rosc_32k` | [必换]`ckmn_driver` + [可换] |
| 时钟 | 系统控制 | `sys_driver` `sys_driver_common` `sys_hal` | [可换] |
| 中断 | 中断控制器 | `sys_int_driver` | [必换] |
| 中断 | 异常/启动 | `trap_base` `arm` `stack_base` | [必换]`trap_base` + [可换] |
| 中断 | AMP 锁 | `amp_res_lock` | [待定] |
| DMA | GDMA | `dma_driver` `dma_hal` | [必换]`dma_driver` + [可换] |
| 定时器 | 定时器 | `timer_driver` `timer_hal` | [必换]`timer_driver` + [可换] |
| 定时器 | AON RTC | `aon_rtc_driver_64bit` `aon_rtc_hal_64bit` | [必换]`aon_rtc_driver_64bit` + [可换] |
| 复位 | 看门狗 | `wdt_driver` `wdt_hal` `aon_wdt_driver` `aon_wdt_hal` | [必换]`wdt_driver` + [可换] |
| 复位 | 复位原因 | `reset_reason` `reboot` | [必换]`reset_reason` + [可换] |
| 存储 | Flash | `flash_driver` `flash_driver_ext` `flash_partition` `flash_hal` `flash_bypass` `flash_notify` `flash_server` | [必换]3 个 + [可换] |
| 存储 | EFUSE | `efuse_driver` `efuse_hal` | [可换] |
| 存储 | OTP | `otp_driver_v1_1` `otp_hal` | [必换]`otp_driver_v1_1` + [可换] |
| 存储 | PSRAM | `psram_driver` `psram_hal` | [可换] |
| 存储 | KV | `easyflash` `ef_env` `ef_port` `ef_utils` `bk_ef` | [待定] |
| 通用 | GPIO | `gpio_driver` `gpio_driver_base` `gpio_hal` | [必换]2 个 + [可换] |
| 通用 | UART | `uart_driver` `uart_hal` | [必换]`uart_driver` + [可换] |
| 通用 | TRNG | `trng_driver` `trng_hal` | [必换]`trng_driver` + [可换] |
| 通用 | UID | `bk_uid` `bk_uid_adaptor` | [待定] |
| 采样 | ADC | `adc_driver` `adc_statis` `adc_hal` | [必换]`adc_driver` + [可换] |
| 采样 | SDMADC | `sdmadc_driver` `sdmadc_hal` | [必换]`sdmadc_driver` + [可换] |
| 采样 | SARADC 服务 | `saradc_notify` `saradc_server` | [可换] |
| 采样 | 触摸 | `touch_driver_v1_1` `sys_touch_driver` | [可换] |
| 采样 | 温度/电压 | `temp_detect` `bk_sensor` `volt_detect` | [可换] |
| 跨核 | Mailbox/IPC | `mailbox_channel` `mb_ipc` `mb_ipc_cmd` `mb_ipc_heartbeat` `mb_chnl_buff` `bk_api_ipc` `bk_api_rpc_server` | [必换]3 个 + [可换] |
| 跨核 | 控制器接口 | `cif_cntrl` `cif_ipc` `cif_main` `cif_mem_mgmt` `cif_ps` `cif_wifi_api` `cif_wifi_dp` | **[必留]**（`libwifi.a` 接口面） |
| 射频 | PHY/RF | `bk_phy_adapter` `bk_phy_notify` `bk_phy_server` `bk_rf_adapter` `cli_phy` | **[必留]**（`libbk_phy.a` 接口面） |
| 打印 | printf | `printf` `printf_base` | [必换]`printf` + [可换] |
| 系统 | 杂项 | `delay` `tick` `mac` `bk_init` `components_init` `rtos_init` `system_main` `driver` `drv_model` `dd` `event` `lib_adapter` `bk_crc` `bk_feature` `bk_netif` `cm_backtrace` `sys_audio_driver` `ate` `bk_cli_ble` `bk_system_at` `vnd_cal` `command_line` | [待定] |

**不可换的边界**：`controller_if` 7 个 + `bk_phy` 5 个是闭源库 `libwifi.a`/`libbk_phy.a` 的调用面，只能以 Armino 拷贝形式存在。`bk_wifi` 26 个（MAC/主机接口）同理——NuttX 没有本芯片的 MAC 驱动。按 2026-09 的架构规则，这三类属于「手写麻烦易错、没必要再一次实现」的合法保留。

**范围结论**：上表「[可换]」的条目都在镜像里，按本节原规则（「另外 52 个只有在被保留代码实际引用时才纳入」）**属于阶段二范围**。原 T12–T17 点名的 27 个仍是最优先（Wi-Fi 侧直接引用），但排期不能以它们做完为准；硬件面要收敛到只剩「[必留]」的 12 个（`controller_if` + `bk_phy`）和 `bk_wifi` 的 26 个。

### [S2.6] 验证模型

- 每个改变运行行为的任务都要上板：连接 `<SSID>`，确认 4 次握手完成、DHCP 拿到地址，再 ping 一个公网 IP 4/4。
- 阶段一完成（T11）之前：每次上板允许复位后最多试 3 次，成功 1 次就算通过，但每次失败的方式都要记录。
- T11 本身，以及阶段二的每一步：连接必须 3/3 成功。
- 每份证据要记录：固件包 sha256、烧录命令、UART 日志，以及源码 mtime 早于打包时间的证明（见 AGENTS.md）。
- 日志里的 PSK 替换为 `<PSK-REDACTED>`。固件自己打印的 `password=...` 也要替换。`WPA: TK` 那一行删除。
- 所有结论都必须指向具体的命令输出或日志行号。没有证据的说法写成任务，不能写成结论。

### [S2.7] 排除规则

默认保留。一个编译单元只有在拿到下面两种证据之一时才排除，证据写进 T7 的排除记录：

1. 链接器报重复定义：贴出链接输出里的那一行，以及 NuttX 侧的定义位置。
2. 同一个资源有两个主人，例如两套调度器、两个复位入口、两张中断向量表：写明资源名称，以及双方占用它的代码位置。

目前已定的排除只有两项：

- `os_source`（FreeRTOS 内核 11 个单元）：调度器只能有一个，用 NuttX 的。`bk_rtos` 原样保留，它调用的 FreeRTOS API 由 T9 的接缝在 NuttX 上实现。
- `lwip_intf_v2_1`（52 个单元）：用户决定第一阶段就使用 NuttX 自己的 TCP/IP 协议栈（NuttX 不带 lwip，`nuttx/net/` 是它自己的实现）。lwip 被用到的符号由 T10 的 netdev 胶水提供。例外：保留 6 个缓冲区和工具单元，见下一条。
- `lwip_intf_v2_1` 保留单元（2026-10-03，用户决定"1A"）：`src/core/pbuf.c`、`mem.c`、`memp.c`、`def.c`、`ipv4/ip4_addr.c`、`stats.c`。协议单元（tcp、udp、ip、etharp、dhcp、netif、tcpip、sockets 等）和 `port/` 下的全部文件仍然排除。
  - 为什么保留：保留下来的 `bk_wifi` 代码以 lwip 的 `struct pbuf` 作为收发缓冲区（`rwnx_rx`、`rwnx_tx`、`skbuff`、`rw_task`、`bk_wifi_adapter` 共调用 `pbuf_*` 6 个），并依赖它的引用计数、`PBUF_POOL` 和 108+600 字节的预留头部（`PBUF_LINK_ENCAPSULATION_HLEN`）。手写替代品要自己保证布局和语义一致；`a7de5d2` 就是那样做的（`hosted pbuf.c` 加手写 `pbuf_shim.c`），这正是 S1 要避免的做法。`bk_wifi/src/pbuf.c` 只在 `CONFIG_FULLY_HOSTED || SEMI_HOSTED` 时编译（第 4 行），基线两者都没开，所以基线实际用的就是这份 lwip `pbuf.c`。
  - 为什么不冲突：这 6 个单元定义的 106 个符号，和 cp-armino 23 个 NuttX 库导出的 4371 个符号比对，交集为 0（`nm --defined-only`，2026-10-03）。它们只在 Wi-Fi 驱动内部使用，NuttX 网络栈看到的边界仍是 `netdev_lowerhalf`，收发时在 pbuf 和 netpkt 之间拷贝。以后要去掉 lwip，只需替换这 6 个文件，netdev 胶水和 NuttX 上层不用改。
  - 带进来的依赖和处理：
    - `pbuf.c` 在 `PBUF_POOL_FREE_OOSEQ=1`（`pbuf.h:283` 默认值）时引用 `tcp_active_pcbs`、`tcp_free_ooseq`、`tcpip_try_callback`，会把 lwip TCP 拉进来。组件参数加 `-DPBUF_POOL_FREE_OOSEQ=0` 切掉；代价是 PBUF_POOL 用完时不再回收 TCP 乱序段，而这里没有 lwip TCP，本来就没有可回收的。
    - `pbuf.c`、`mem.c`、`memp.c` 要 `sys_arch_protect/unprotect`、`sys_mutex_new/lock/unlock`。基线由 `port/sys_arch.c` 提供，但该文件还带着 lwip 的线程、mbox、per-thread 信号量，且按 FreeRTOS 的 `pthread_key_t` 编译，与 NuttX 冲突（第一次保留它时报 `pthread_internal.h:17` 类型冲突）。所以不保留它，改由 `compat/bk7258_lwip_sys.c` 用 NuttX mutex 提供这 5 个函数，语义照基线的 `CFG_ENABLE_LWIP_MUTEX`（`sys_arch.c:55,463-485`）：一把全局锁。和基线的区别：这里用递归锁（lwip 的约定要求可递归，基线的 FreeRTOS 互斥量不是）。
    - Beken 把 `pbuf_debug_flag` 定义在 OOSEQ 分支里（`pbuf.c:116`），而 `pbuf.c:181,186` 无条件使用；关掉 OOSEQ 后由 `bk7258_lwip_sys.c` 定义，声明放在 `compat/bk7258_lwip_compat.h`，对整个组件 `-include`。
    - 同一个头里取消 NuttX `<unistd.h>` 的 `opterr` 宏：`stats.c:76` 读 `proto->opterr` 字段。`lwip_os/lwip/stats.h` 只在头文件周围暂停这个宏，函数体里仍会被展开。
    - `memp.c` 包含的 `lwip/netdb.h` 重复定义 `struct hostent`、`struct addrinfo`、`EAI_*`、`AI_*`、`h_errno`。新增 `lwip_os/lwip/netdb.h`，先包含 NuttX `<netdb.h>`，再用 lwip 自己的 4 个 `LWIP_DNS_API_*` 开关关掉这些定义，然后 `#include_next`。`memp.c` 需要的 `NETDB_ELEM_SIZE` 保留。
    - `memp.c` 经 `sockets_priv.h:164` 用到 `nfds_t`：`lwip_os/lwip/sockets.h` 补 `#include <poll.h>`。
  - 代价：memp 静态池约 31.7 KB（`nm -S` 实测，2026-10-03），其中 PBUF_POOL 23 KB 是 Wi-Fi 收包要用的，约 8 KB（TCP_PCB、TCP_SEG、NETCONN、NETDB 等）在 NuttX 下没有用户。保持和基线 lwipopts 一致，暂不裁剪；裁剪要改 lwipopts，属于对基线配置的偏离，需要另行决定。
  - 被否决的方案"1B"：像 `a7de5d2` 那样手写 pbuf、底层用 kmm。占 RAM 少，但又回到手写仿造。

原则：OS 和 POSIX 接口（libc、signal、pthread、socket、spinlock）一律用 NuttX 的。Armino 自带的同名副本排除或屏蔽，由 `chips/bk7258/wifi_armino/compat` 对接，不改 Armino 源码。lwip 协议栈内部接口（pbuf、netif、memp 等）不是 OS 接口，归 T10。

按这条原则已处理（证据来自 T8 第二次构建和基线依赖记录）：

- `bk_rtos/.../pthread_local_storage.c` 排除：它定义自己的 `pthread_key_t`，和 NuttX 冲突；导出符号只被已排除的 lwip 和 os_source 调用。
- `wpa_supplicant-2.10/bk_patch/signal.h` 屏蔽：它是无 OS 环境下的 `<signal.h>` 替身，遮蔽了 NuttX 的版本（103 个单元因此报 `sigset_t` 未定义）。保留代码用到的 `SIGALRM`、`SIGHUP`、`SIGINT`、`SIGTERM` 两边数值一致；唯一不同的 `SIGUSR1` 只在未编译的 `hostapd/main.c` 里。`bk_patch/signal.c` 只有空函数，排除后由 compat 头里的 static inline 提供。
- `bk_cli/shell_uart.c`、`bk_cli/cli_rpc.c` 排除，由 `compat/bk7258_armino_shell.c` 接到 NuttX：Armino 的 CLI 和日志（`cli_main.c`、`shell_task.c`）作为应用层原样保留。`shell_uart.c` 自己初始化 UART0 并注册中断，而 UART0 是 NuttX 控制台，两个主人。新设备保持同样的 8 格队列和完成回调顺序，输出走 `up_putc`，输入归 NSH。`cli_rpc.c` 对外只导出 `cli_debug_cmd_table`（`cli_main.c:961` 调用），由空表替代。
- lwip 的 `sockets.h`、`inet.h` 用 `wifi_armino/lwip_os` 中的替换头：socket 是 OS 接口，lwip 已排除。原头文件和 NuttX 重复定义了 `socklen_t`、`sockaddr`、`iovec`、`in_addr` 等，还用宏把 `socket()` 等改名成 `lwip_*()`。这份 lwip 没有 `LWIP_SOCKET_EXTERNAL_HEADERS` 开关，所以用替换头，并沿用 lwip 原来的头文件保护宏。保留代码直接调用 BSD 名字，现在走到 NuttX；`closesocket`、`ioctlsocket` 映射到 `close`、`ioctl`。`lwip/stats.h` 原样使用，只在它周围暂停 NuttX 的 `opterr` 宏（字段名冲突；保留代码不读这个字段）。
- 中断：NVIC 归 NuttX。排除 `icu/interrupt_base.c`、`arch/cm33/arch_interrupt.c`、`driver/bk7258/interrupt.c`，由 `compat/bk7258_armino_irq.c` 提供 `bk_int_*`：源 n 接到 NuttX IRQ `NVIC_IRQ_FIRST + n`（`icu_map.h` 64 行全部是同号映射，用基线调试信息逐行核对），优先级照基线（`INT_SRC_LCD` 为 0，其余为 6，3 位优先级）。注册只开 NVIC，SYS 中断使能仍由各 Armino 驱动自己开。`interrupt_init` 为空：原版会关掉 PRIMASK 和全部 64 路 NVIC。若某个 IRQ 已被 NuttX 驱动打开，拒绝注册并打日志。为此 `cp-armino` 关掉 NuttX 的 GDMA 和 TIMER0 驱动，这两个源交给 Armino。
- `bk_wifi/include/wifi_spinlock.h` 屏蔽：它用同目录引号包含，包含路径挡不住，所以用 `-include` 预先定义它的头文件保护宏。`spinlock_t` 用 NuttX 的类型；锁操作照基线保持空操作，对应的 NuttX 调用写在注释里。

其余组件，包括 `cm33`、`cmsis`、`bk_startup`、`bk_cli`、`at_server`、`unity`、iperf demo，一律以 T8 链接报出的冲突为准，不预先排除。已有成果（`hal_port/`、`a7de5d2` 的 shim 等）只能作为 T9、T10 实现时的参考，不能直接拿来用。

下表是候选组件对外被用到的情况，用来估计 T9、T10 的工作量。数据来自 `d5b0278` 的 `app.map`。"被用到的导出符号"指组件之外有引用的符号数；调用方后面的数字，是该调用方用到其中多少个，只列前 6 名。

| 组件 | 编译单元 | 被用到的导出符号 | 调用方 |
|---|---|---|---|
| `os_source`（FreeRTOS 内核） | 11 | 81 | bk_rtos 71、bk_system 4、psa_mbedtls 4、cmsis 3、lwip 3、coredump 3 |
| `bk_rtos` | 7 | 102 | bk_wifi 60、wpa 45、driver 42、lwip 39、bk_cli 37、bk_phy 24；另有 22 个被闭源库直接引用（libwifi 20、libbk_phy 5） |
| `lwip_intf_v2_1` | 52 | 46 | bk_wifi 27、iperf 19、controller_if 9、bk_netif 9、wpa 4、bk_cli 3 |
| `cm33` | 10 | 28 | driver 10、bk7258 5、bk_cli 3、bk_rtos 2、cmsis 2、os_source 2 |
| `cmsis` | 2 | 1 | common 1 |
| `bk_startup` | 2 | 8 | bk_cli 4、driver 4、bk_pm 2、cm33 2、bk_init 1、cmsis 1、cp 1 |

### [S2.8] 日志对比

不抓寄存器、不插桩。第一阶段编进来的是 Armino 原文件，它们自带的日志（`wifid`、`wpa`、`rwnx`、`hitf`、`lwip` 等前缀）在我们的固件里会原样打印，所以直接比较两份 UART 日志里关键事件的顺序和次数：scan 开始与完成、auth、assoc、4 次握手各状态、DHCP、重握手次数、`tx_dma_dead`、assert、重启。Armino 参照固件不需要另开 debug 分支。

### [S2.9] netdev 胶水行为（T10）

代码在 `chips/bk7258/wifi_armino/compat/bk7258_armino_netdev.c`，替代基线 `lwip_intf_v2_1/lwip-2.1.2/port/net.c`、`wlanif.c` 提供的 20 个符号，数据面接 NuttX `netdev_lowerhalf` 的 `wlan0`。每条后面写明依据，出问题时按依据回查。

- 注册：`net_wlan_initial()` 由 `bk_netif_init()` 开机调用（`bk_netif.c:76`）。基线在这里启动 lwip 栈；NuttX 栈已经在运行，所以改为注册 wlan0，让它在连接前就可见。`net_wlan_add_netif()` 只接受 STA vif，记下 vif 索引和 MAC（基线 `low_level_init()`、`low_level_output()` 从 vif 读的就是这两项）。RX quota 取 4，沿用 `a7de5d2` 的板上结论：quota 为 1 时，ARP 应答会被同时到达的广播帧挤掉。
- 收包：照 `ethernetif_input()`（`wlanif.c:331-451`）。EAPOL 先交给 wpa（`ke_l2_packet_tx`），即使 netif 还没注册也交，基线注释写明了原因；自己发出又回环的帧丢弃；其余帧拷进 netpkt，offset 0 让以太头落在 link-layer 预留区，这是 NuttX 栈读取以太头的位置。
- 发包：照 `low_level_output()`（`wlanif.c:230-262`）。netpkt 拷进 `PBUF_RAW_TX` pbuf 交给 `bmsg_tx_sender()`。`bmsg_tx_sender()` 自己另取一份引用，所以 `pbuf_alloc()` 那份在本函数里释放。
- 取址（2026-10-03，用户决定"2A"）：保持基线流程。`bk_netif.c:38-50` 在 `EVENT_WIFI_STA_CONNECTED` 时调 `sta_ip_start()`，在 `DISCONNECTED` 时调 `sta_ip_down()`；胶水在这两处开关 carrier、启动和停止 DHCP。拿到地址后，像 `wm_netif_status_callback()`（`net.c:405-406`）一样调 `wifi_netif_call_status_cb_when_sta_got_ip()` 和 `wifi_netif_notify_sta_got_ip(IP4)`。
  - 为什么：`notify_sta_got_ip` 会把 Armino 链路状态设为 `GOT_IP` 并调 `wlan_dhcp_done_ind()`（`wifi_netif.c:109-131`）。不调的话，Armino 状态机停在 CONNECTED，与原厂行为不一致。
  - 被否决的"2B"：由用户手动 `renew wlan0`，事后补发通知。每次连接都需要人工操作。
- DHCP 续租（2026-10-03）：
  - 为什么要自己调度：NuttX `dhcpc` 每次调用只完成一次申请（`dhcpc_request()`，`apps/netutils/dhcpc/dhcpc.c:695`），没有续租。`dhcpc_request_async()` 只是每过半个租期重新完整申请一次，不区分 T2，也不处理租约到期（`dhcpc.c:468-510`）。NSH 的 `renew` 也只申请一次（`netlib_obtainipv4addr.c` 注释写明没有续租逻辑）。基线 lwip DHCP 是完整的续租状态机，为了对齐，胶水按 lwip 的时间线调度，每一步调用一次 `dhcpc_request()`。
  - 没有地址时：按 `dhcp_discover()` 的退避重试，间隔 2^n 秒，上限 60 秒。
  - T1 = 租期 ÷ 2 续租，T2 = 租期 × 7/8 重新绑定（`dhcp_handle_ack()` 的默认值）。失败后在距下一个节点一半的时刻重试；剩余不足 60 秒就直接等到下一个节点（`dhcp_t1_timeout()`、`dhcp_t2_timeout()`）。
  - T0（租期到）：清掉地址，从头申请（`dhcp_coarse_tmr()`）。租期上限 0xffff × 20 秒、下限 20 秒，照 `dhcp_bind()`；0xffffffff 视为永久租约。
  - 通知 Armino：got_ip 每次连接只发一次，对应 `net.c:400` 的 `ip4_addr_set`。已拿到的地址丢失（收到 NAK 或到 T0）时发 `wifi_netif_call_status_cb_when_sta_dhcp_timeout()`，对应 `net.c:466-471`。首次申请失败只重试、不通知，和 lwip 相同。
  - 断线竞态：`dhcpc_cancel()` 只在两次重试之间检查取消标志，断线后迟到的 ACK 会被 dhcpc 自己写到 wlan0 上（`dhcpc.c:845`）。胶水给每次连接编号，识别出迟到的结果后撤销这个地址。
  - 与基线不一致的两点，都来自 dhcpc：每次申请都从广播 DISCOVER 开始，基线续租是单播 REQUEST；租约到期不发 DHCPRELEASE。改 `apps/netutils/dhcpc` 属于上游代码，要另提 PR。
- ARP 保活（2026-10-03）：闭源 MAC 库在 `CONFIG_WIFI6` 下发 `ME_INDICATE_SEND_ARP_IND`，`rw_msg_rx.c:1736` 收到后调 `net_begin_send_arp_reply()`。基线（`net.c:1382-1401`、`etharp.c:286-320`）给网关发一个主动 ARP reply，网关 MAC 未知时改发 ARP request，帧都走 `bmsg_special_tx_sender()`（由 `special_arp_flag` 选择）。胶水照这个流程做，有两点差异：
  - 网关 MAC 从网关发来的 ARP 帧里记下，不查 NuttX ARP 表。原因：`arp_find()` 是 `nuttx/net/` 的内部函数；公开的 `SIOCGARP` 要拿 `net_lock()`，而这个函数运行在 Wi-Fi 核心线程上，协议栈拿着锁时可能正在等这个线程的 TX 队列，两边会互相等住。
  - `etharp_tmr_flag`（保活期间 ARP 表项保留 3600 个 tick，`etharp.c:243`）在 NuttX 里没有对应机制，表项仍按 `CONFIG_NET_ARP_MAXAGE` 过期。
- 一期不实现，被调用时打日志：SoftAP 地址和 DHCP 服务（`uap_ip_*`；`dhcp_lookup_mac` 返回 NULL，两个调用方都处理了 NULL）；lwip CLI 的 `ping_start/stop`，用 NSH 的 `ping` 代替；`etharp_request`，唯一调用方 `iperf.c:405` 只用它预热 lwip ARP 缓存。
- 连接入口（2026-10-03，用户决定先用 Armino 原生 API，之后改为 NuttX 原生）：临时 NSH 命令 `armino_sta <ssid> [password]`（`chips/bk7258/wifi_armino/bk7258_armino_sta.c`），调用基线自带的 `demo_sta_app_init()`（`wifi_api.c:183`，即 `bk_wifi_sta_set_config()` + `bk_wifi_sta_start()`，与 `a7de5d2` 板上连通时的调用一致）。命令先检查 SSID 不超过 32 字节、密码为空或 8..64 字符，因为该函数用 `os_strcpy` 拷进 33/65 字节的字段；密码不打印。之后改为给 wlan0 接 wireless ops、走 `wapi`。
- MTU（2026-10-03）：`NET_ETH_PKTSIZE` 原来是 Kconfig 默认值 590（IPv4），wlan0 的 MTU 因此是 576；改为 1514，即 MTU 1500，与基线 lwip 和 `a7de5d2` 的 `BK7258_WIFI_MTU` 一致。`IOB_BUFSIZE` 保持 640，一帧 1514 字节会跨 3 个 IOB；NuttX 上游多数 1514 配置用的是默认 196 字节的 IOB，链式 IOB 是常规用法。RX quota 为 4，最多同时占 12 个 IOB，`IOB_NBUFFERS` 为 24。
- 中断日志：`bk7258_armino_irq.c` 只在某个源首次注册或更换处理函数时打日志。`bk_adc_init()`/`bk_adc_deinit()` 每次采样都会注册、注销 `INT_SRC_SARADC`（src 8，`adc_driver.c`），温度和电压检测大约每秒采样一次；基线也是如此，只是它的 `bk_int_isr_register()`（`interrupt_base.c:37`）不打日志。
- 句柄：`net_get_sta_handle()` 返回一个不透明的占位对象。保留下来的读取方只把它传给 `net_get_if_addr()`（`bk_netif.c:126`、`wifi_at.c:1285`、`cif_wifi_api.c:580`、`iperf.c:273`）。把它当 netif 读字段的两处，`bk_netif.c:157`（`CONFIG_IPV6`）和 `rwnx_utils.c:255`（`#if 0`），都不会编进来。
- lwip 模块初始化（2026-10-03 修正）：基线 `net_wlan_initial()` 一路到 `tcpip_init()` → `lwip_init()`，按 `stats_init`、`sys_init`、`mem_init`、`memp_init` 的顺序初始化（`net.c:227-239`、`init.c:346-352`）。我们原来的 `net_wlan_initial()` 只注册 netdev，漏了这几个，结果第一次收帧（扫描结果上传）时 `mem_mutex` 为 NULL，在 `sys_mutex_lock` 里断言。现在补调 `stats_init()`、`mem_init()`、`memp_init()`（`sys_init()` 不需要：它只为基线 sys_arch 建锁，我们的是静态初始化；`pbuf_init()` 是空宏）。`ram_heap` 因此链入 .bss，放在 CP 的 SRAM 里（`0x280815f0`，51219 字节），和基线一样；`memp` 的池也在 SRAM。

### [S2.10] CPU1（AP）归属与跨核

决定（2026-10-03，用户定方向）：CP、AP 各跑一个 NuttX。跨核传输用 NuttX 的 RPTUN/RPMsg，mailbox 硬件只做门铃，归 NuttX。Armino 的跨核协议 mb_ipc 承载在 RPMsg 端点 `bk7258-mb-ipc` 上（`chips/bk7258/mb_ipc/bk_mailbox_rpmsg.c`），不再单独使用 mailbox 硬件。CPU1 的启停只由 NuttX 做。

- 争用的资源：CPU1 启停寄存器 `SYS_CPU1_INT_HALT_CLK_OP`（SYS+0x14，NuttX 中名为 `BK7258_SYS_CPU1_CTRL`），字段为 sw_rst bit0、pwr_dw bit1、rxevt_sel bit5、offset bit8-31（`soc/bk7258/soc/sys_struct.h:89-96`）。有两个写入方：
  - NuttX：`BOARDIOC_START_CPU`（`bk7258_ap_control.c:379-468`）。
  - Armino：`reset_cpu1_core()`（`system_main.c`），由 `start_cpu1_core()` 调用，入口有两处：启动投票（`pwr_clk.c:347`）和心跳重启（`mb_ipc_heartbeat.c:166`）。
  - 两者的启动地址不同：NuttX 用 `BK7258_AP_XIP_VECTOR_BASE`，Armino 用分区 1（`get_partition_addr(1)`）。
- 为什么阶段一 Armino 那条路径不会触发（依据是代码阅读，未上板）：
  - 启动投票：`user_app_main()`（`cp_main.c:15`）调 `bk_pm_module_vote_boot_cp1_ctrl()`，`pwr_clk.c:331` 只在 `sys_drv_module_power_state_get(CPU1) == PM_POWER_MODULE_STATE_OFF`（值为 1，`pm.h:236-237`）时才启动 CPU1。这个函数在 bk7258 上读的是 `SYS_CPU_CURRENT_RUN_STATUS`（SYS+0x0c）的 bit9 `cpu1_pwr_dw_state`（`soc/bk7258/hal/sys_hal.c` 及其 `sys_ll` 访问函数，`sys_struct.h:64`）。NuttX `START_CPU` 在释放复位前，等待的正是这一位清 0（`bk7258_ap_control.c:419-423`，`BK7258_SYS_CPU1_PWR_DW_STATE` = bit9）。因此 NuttX 启动 AP 之后，Armino 读到的是 ON，会跳过 `start_cpu1_core()`。
  - 先后顺序（2026-10-03 更正）：AP 的启动是异步的。`bk7258_rptun_initialize()` 调 `rptun_boot("ap")`，`rptun_dev_start()` 只创建优先级 90 的 `rptun` 线程就返回（`nuttx/drivers/rptun/rptun.c:898-925`）。`RPTUN_START` → `bk7258_rptun_start()` → `BOARDIOC_START_CPU` 都在那个线程里执行，而我们的 `is_autostart()` 返回 false（`bk7258_rptun.c:245`）。所以 `bringup` 返回时 AP 不一定已经启动，"NuttX 先"原本只是靠 `armino-entry`（优先级 85）低于 `rptun`（90）这个优先级关系得来的。
  - 现在由代码保证：`bk7258_armino_start.c` 的 `bk7258_armino_wait_ap()` 在调用 `entry_main()` 之前，轮询 `BK7258_SYS_CPU_STATUS` 的 bit9，直到它清零，即 Armino 在 `pwr_clk.c:331` 判断用的同一位。轮询间隔 10 ms，最长 5 s。日志 `[ARMINO] CPU1 powered up by NuttX after <n> ms` 或 `[ARMINO] CPU1 still powered down after 5000 ms`。没有用 `rptun_wait()`：它要求 rptun 驱动提供 `set_phase`/`get_phase`，`bk7258_rptun.c` 没有实现，而且它是忙等（`rptun.c:rptun_dev_wait`）。超时后 Armino 照常启动，这样 AP 启动失败不会连带 Wi-Fi 起不来。
- RPMsg 没有就绪时（依据是代码阅读）：
  - 不会卡死。`bk_mailbox_send()`、`bk_mailbox_ready()` 在端点未绑定时立刻返回 `BK_ERR_MAILBOX_TIMEOUT`（`bk_mailbox_rpmsg.c:426-455`）。Armino 的 `mailbox_channel.c` 遇到这个返回值会重试，物理通道忙的超时为 200 ms（`:34,651-653`）。`ipc_send_cmd()` 等应答的上限是 `IPC_RSP_TIMEOUT` 600 ms（`mb_ipc_cmd.c:46`）；它用 `WAIT_FOREVER` 等的只是串行化用的通道锁。这条命令最终失败，消息不会排队等待重发。
  - 阶段一 CP 不会主动发 IPC：CP 编成 AMP 资源锁的服务端（`mb_ipc_cmd.h:63-65`，`CONFIG_CPU_CNT=3`），获取和释放计数都在本地完成（`amp_res_lock.c:34,166,203`）。只有在其他核也申请过同一资源（`others_req_cnt > 0`）时，释放才会发 `ipc_send_available_ind()`（`:298-301`）。AP 不运行 Armino 的 amp_res，所以这个计数一直为 0。`bk_phy_notify.c`、`flash_notify.c`、`saradc_notify.c` 里都没有 `ipc_send_*` 调用。心跳 master 不主动发消息。这个结论不是对所有保留单元的穷举。
  - 以后接入 PHY、flash、SARADC 服务时（尚未排期，见 `bk7258-armino-otp-mb-ipc.md` Report 的未完成项），由这些服务负责等端点绑定后才对外提供；后端仍保持立刻失败。
  - 心跳：master 的 `cpu_x_state` 只在两种情况下改变：`start_cpu1_core()` 经 `mb_ipc_reset_notify()` 置为 STARTING；或收到 AP 发来的 `IPC_CPU1_POWER_UP_INDICATION` 后置为 POWER_ON（`mb_ipc_cmd.c:650`）。两者都不发生，状态就一直是 `CORE_POWER_OFF`，`ipc_heartbeat_timeout()` 直接返回 0，不会调用 `restart_cpu_x()`。
- 遗留风险：
  - 如果 NuttX 启动 AP 失败，或者 5 s 内 bit9 仍未清零，Armino 的启动投票会把 CPU1 复位到分区 1 的地址。要彻底消除这个风险，只能修改 `system_main.c` 或 `pwr_clk.c`，S2.3 不允许，因此保留这项风险，靠日志里的 `still powered down` 发现。
  - 以后 AP 一旦发送 `IPC_CPU1_POWER_UP_INDICATION`（尚未排期，见 `bk7258-armino-otp-mb-ipc.md` Report 的未完成项），Armino 的心跳 master 就开始监控。AP 必须同时每 2 秒发一次心跳，否则 6 秒后 `restart_cpu_x()` 会按分区 1 的地址重启 CPU1。到那时，要么 AP 同时实现心跳 slave，要么屏蔽 master 的重启。
  - AP 异常后的重启由 NuttX 负责（`ap-monitor`、RPTUN）。
- AP 侧：
  - 数据面经 `NET_USRSOCK_RPMSG` 使用 CP 上的 NuttX 协议栈（`configs/ap` 有 `NET_USRSOCK_RPMSG_CPUNAME="cp"`，`cp-armino` 有 `NET_USRSOCK_RPMSG_SERVER`），wlan0 换实现对 AP 透明。
  - AP 已开启 `BK7258_MB_IPC_RPMSG=y`（10-01 构建的 `.config`；Kconfig 默认 y），但只导入了传输状态机（`mb_ipc/armino_ap/` 下的 `mailbox_channel.c`、`mb_ipc.c`、`mb_chnl_buff.c`）。基线 AP 上负责发送 `PM_CPU1_BOOT_READY_CMD`（`ap/.../pwr_clk.c:137`）、POWER_UP 和心跳（`ap/.../mb_ipc_cmd.c:813-821`、`mb_ipc_heartbeat.c`）的单元都没有导入。阶段一用不到它们：CP 跳过启动投票后，就不会等 BOOT_READY。
  - RPMsg 后端目前只转发逻辑通道 `BK_MB_IPC_LOGICAL`=1（`bk_mailbox_rpmsg.c:116-117`），PM 通道 `MB_CHNL_PWC` 不经过它。
- 参照 team 135（`open-vela/contest2026_135_yongwangzhiqian`，`/tmp/t135-full` 浅克隆，`6052156`，2026-10-03）：做法相同，CP 上的 NuttX 独占 CPU1。
  - 启动：`chips/bk7258/cp/bk7258_ap_control.c:619-650` 先调 `bk_pm_module_vote_power_ctrl(CPU1, ON)`，再按 SDK 顺序调 `sys_drv_set_cpu1_pwr_dw(0)`、`rxevt_sel(1)`、`boot_address_offset(AP 向量 >> 8)`、`reset(1)`，然后等共享内存里的 AP 状态变为 `BK7258_AP_STATE_READY`。他们的注释写明：PM 投票不等于 pwr_dw=0，投票还会清 CPU1 的 halt 位、恢复时钟并等待时钟稳定。
  - 停止和重启：只有 `bk7258_cpu1_sdk_stop()`（`:94-104`，等同 `reset_cpu1_core(0, 0)`）和 `bk7258_ap_restart()`（`:891`）。
  - 跨核：CP、AP 都用 `common/bk7258_rptun_mbox.c` 和 `bk7258_rptun.c`；低功耗协调另走 SDK 的 PWC mailbox（`ap/bk7258_pm_coord_ap.c`）。
  - 整个仓库（tests 除外）搜不到 `mb_ipc_heartbeat`、`restart_cpu_x`、`start_cpu1_core`、`user_app_main`、`vote_boot_cp1`、`PM_CPU1_BOOT_READY`：他们不编译 Armino 的 `system_main.c`、`cp_main.c` 和心跳，所以没有我们这里的双写入方问题。我们按 S2.2 原样保留了这些单元，所以要靠上面"不会触发"的论证，并在 T20 上板验证。
  - 与我们的差异：我们的 `START_CPU` 在释放前调用的是 `sys_hal_module_power_ctrl(17 /* CPU1 */, ON)`（`bk7258_ap_control.c:342-347`，理由见 `:331` 的注释），而不是 135 用的 `bk_pm_module_vote_power_ctrl`。也就是说，硬件上电走的是同一个 hal 函数，只是跳过了 PM 层。135 注释里说 PM 投票还会清 halt、恢复时钟并等待稳定，这些动作有多少是 hal 之外额外做的，尚未核实。跳过 PM 层带来的已知后果：Armino `pm.c` 的 `s_pm_on_modules` 里没有记录 CPU1，阶段二接入 PM 时要处理。

## [S3] Out of Scope

- 把 wpa 加密迁移到 NuttX mbedtls：另起 feature。
- AP 核侧改动、SoftAP、BLE、WPA3/SAE 验证。CPU1 归属见 S2.10：阶段一 AP 不改；AP 侧 PM 握手和心跳 slave 尚未排期（`bk7258-armino-otp-mb-ipc.md` 已 superseded，其 Report 列为未完成项）。
- 整体合入 `dev` 那 81 个提交。
- 推送分支、发 PR：由用户另行决定。

## Tasks

- [x] T1: 复现起点。acceptance：构建通过；连接成功后 ping 公网 4/4；记录 `tx_dma_dead` 的次数和行号。结果：构建、decode、完整性门禁都通过；镜像 sha256 `121d3cc7…1af7`；`<SSID>` 上连接 1/3；ping 10/10；`tx_dma_dead` 0 次。证据见 S1。(covers: S2.6)
- [x] T2: 接口表提取脚本。结果：`chips/bk7258/tools/armino_interface.py` 生成 `chips/bk7258/tools/armino_baseline/{interface.tsv,summary.txt}`，`--check` 返回 0；167 个外部符号中 164 个有提供者；`malloc`/`free` 经 `--wrap` 由 `bk_rtos/platform_stub.c` 提供；`dbg_env` 只被 `dbg_task.c.obj` 引用，该成员没有链入；硬件目标文件 79 个，直接 27 个。读取基线 `app.map`、`compile_commands.json`，对闭源库跑 `nm`，输出"符号 → 提供者目标文件 → 源文件 → 组件"的对照表。脚本和生成结果一起入库。acceptance：重复运行输出一致；闭源库 167 个外部符号中 164 个有提供者，其余 3 个（`malloc`、`free`、`dbg_env`）在表中写明来源；S2.7 表中的数字能从脚本输出复现。(covers: S2.1, S2.5, S2.7)
- [x] T3: 源码一致性门禁脚本。结果：`chips/bk7258/tools/armino_source_gate.py` 按目录前缀把文件映射到基线（`glue/`、`wpa_supplicant/`、`board/`、`include/common/`、`include/`），比对 `.c`/`.h`/`.S`。在 `a7de5d2` 上：`.c` 改动 32 个、无对应 1 个（`glue/bk_wifi/src_rwnx_td.c`，没有任何构建文件引用它），返回 1。另外查出头文件改动 11 个、无对应 2 个（`vnd_cal/include/bk_arm_arch.h`、`bk_misc.h` 是手写的空兼容头，基线对应文件分别在 `cp/middleware/driver/include/bk_private/` 和 `cp/components/bk_common/include/`）。自测：逐字节相同返回 0，改 1 个字节返回 1，多出无对应文件返回 1，全部列入例外后返回 0。拿阶段一的每个保留源文件和基线逐字节比对，出现例外清单之外的差异时返回非零。acceptance：对 `a7de5d2` 现有的 `armino/` 运行，能准确报出 32 个改动文件和 1 个无对应文件，并返回非零。(covers: S2.3)
- [x] T4: 编译清单生成脚本。结果：`chips/bk7258/tools/armino_compile_manifest.py` 生成 `chips/bk7258/tools/armino_baseline/{armino_units.tsv,component_flags.tsv}`。538 个编译单元、38 个组件；同一组件内所有单元的编译参数一致（去掉 `-o`、`-MF`/`-MT`、`-D__FILE__` 和源文件位置这些逐文件差异后），因此按组件存一份参数就够。`-I` 路径把基线树前缀重写成 `@ARMINO@`，消费方再替换成本机路径。参数按 shell 规则（shlex）解析，每个 argv 元素占一列，所以 `"-Wl, --gc-sections"`、`-DAPP_VERSION="unknown"` 这类带空格或引号的参数保持完整。`--check` 返回 0；全部 538 个单元逐个和 `compile_commands.json` 原始 argv 比对，0 处不一致。从基线 `compile_commands.json` 生成 CMake 源码列表和逐文件编译参数。acceptance：重复生成结果一致；每个单元的参数和基线相同，只有登记过理由的工具链等价替换可以不同。(covers: S2.2; depends: T2)
- [x] T5: 日志对比脚本。输入两份 UART 日志（Armino 参照、我们的固件），按 S2.8 抽取关键事件序列和计数，输出两边的差异。acceptance：对 `/home/czp/iperf-ref-1001.log` 和 `evidence-20261001/t1-a7de5d2-connect-ping.log` 运行，能报出 T1 的 2 次重握手，而参照为 0 次。结果（2026-10-03，`board/bk7258-devkit/tools/compare_uart_logs.py`）：对验收那一组运行，输出 `rehandshake 0 vs 2 REGRESSION`，握手序列里 candidate 两次出现 `COMPLETED -> 4WAY_HANDSHAKE` 而 reference 的序列停在一次 `GROUP_HANDSHAKE -> COMPLETED`，同时给出 scan/auth/assoc/握手各状态、DHCP、`tx_dma_dead`、assert、restart 的并排计数。同一文件自比对为 "not worse"、退出 0。计数口径：restart 取 `!RST` 与 `[BK7258] PSRAM id=` 两个启动标记的较大值，避免一次启动被算两遍；DHCP 认 `me dhcp done`、`dhcp done`、`wlan0 ip`、以及 `inet addr:` 非零地址四种写法（两侧固件打印不同）。退出码只跟 T11 的两项（重握手、`tx_dma_dead`），assert 与 restart 只报告不判退——抓取窗口从复位开始时自带一个 `!RST`，是窗口差异不是回归。(covers: S2.8)
- [x] T6: 参照负载基线。~~Armino 参照固件跑 30 分钟 TX 负载：优先用 `iperf -c`；热点有客户端隔离时，改用最大包长的连续 ping 公网。acceptance：统计 `tx_dma_dead`、assert、重启的次数，并记录用了哪种负载方式。~~ **已取消（2026-10-07，用户决定，原话："取消T6吧 后续对于wifi的众多考验会在后面遇到的"）**。理由：对照原厂 Armino 固件混进了 OS、配置、内存布局等其它差异，不是「原生化」这一个变量，尺子是歪的；而真正的 Wi-Fi 压力考验留到后面应用阶段在真实使用中遇到。T18 的判据随之从「不多于 T6 的结果」改为绝对门槛，负载方式移到 T18。基线：本行原写法见 `5ce85c3` 的同名条目。(covers: S2.6)
- [x] T7: 排除记录。根据 T8 的链接输出和资源冲突，为每个被排除的单元下 S2.7 要求的证据。acceptance：每个排除单元都能对应到一行链接输出，或一条写明双方代码位置的资源冲突说明；`os_source`、`lwip_intf_v2_1` 两项的依据已写入。结果（2026-10-03 整理，`chips/bk7258/wifi_armino/CMakeLists.txt` 的 `excluded`/`excluded_units` 是执行清单）：共 538 个单元中排除 68 个，分 6 组，逐组证据如下。

  | 组 | 单元数 | 排除对象 | 证据类型 | 证据 |
  |---|---|---|---|---|
  | 1 | 11 | `os_source`（FreeRTOS 内核，`cp/components/os_source/freertos_v10/`） | 资源冲突 | 调度器只能有一个。NuttX 侧：`nuttx/sched/`（`nx_start`、`nxtask_start`）。Armino 侧：FreeRTOS `tasks.c`/`queue.c`/`list.c` 等。替代：`compat/bk7258_freertos_nuttx.c` 提供 `bk_rtos` 需要的 45 个符号（T9）。 |
  | 2 | 46 | `lwip_intf_v2_1` 的协议单元（`tcp.c`、`udp.c`、`ip.c`、`etharp.c`、`dhcp.c`、`netif.c`、`tcpip.c`、`sockets.c`、`api/*.c`、`port/` 全部等） | 资源冲突 | TCP/IP 栈只能有一个。NuttX 侧：`nuttx/net/`（`netdev_register`、`tcp_input`、`arp_in`）。Armino 侧：lwip-2.1.2 的同名功能。替代：`compat/bk7258_armino_netdev.c` 把 20 个 lwip 符号接到 `netdev_lowerhalf`（T10）。保留 6 个缓冲区/工具单元见 S2.7（`pbuf.c`、`mem.c`、`memp.c`、`def.c`、`ip4_addr.c`、`stats.c`）。 |
  | 3 | 1 | `bk_rtos/freertos/posix/freertos_impl/source/pthread_local_storage.c` | 链接冲突 | 自定义 `pthread_key_t`（`pthread_internal.h:17`，`uint32_t`），与 NuttX `nuttx/include/sys/types.h` 的同名类型冲突；T8 第二次构建报 `conflicting types for 'pthread_key_t'`。它的导出只被已排除的 lwip 和 os_source 调用。 |
  | 4 | 3 | `wpa_supplicant-2.10/bk_patch/signal.c`；`bk_cli/shell_uart.c`、`bk_cli/cli_rpc.c` | 资源冲突 | signal：无 OS 环境的 `<signal.h>` 替身，遮蔽 NuttX 的版本（103 个单元因此报 `sigset_t` 未定义），替代是 `compat/bk_patch/signal.h` 的 static inline。UART0：NuttX 控制台与 Armino `shell_uart.c`（`bk_uart_init` + 自己的收发 ISR）抢同一个 UART0，替代是 `compat/bk7258_armino_shell.c` 走 `up_putc`、输入归 NSH。`cli_rpc.c` 只导出 `cli_debug_cmd_table`，由空表替代。 |
  | 5 | 3 | `cmsis/.../startup_bk7236.c`、`system_bk7236xx.c`；`middleware/driver/mailbox/mbox0_adapter.c`、`mbox0_drv.c` | 资源冲突 | 复位入口：NuttX 的 `__start`/`nx_start` 与 Armino 的 `Reset_Handler` 抢 CPU 复位向量，`system_bk7236xx.c` 的 `SystemInit` 只被该入口调用；`set_reboot_tag()` 移到 `bk7258_armino_start.c`。MBOX0：NuttX 的 RPTUN/RPMsg（`chips/bk7258/bk7258_mailbox.c`）与 Armino 的 `mbox0_adapter.c` 抢 mailbox 0 硬件，替代是 `chips/bk7258/mb_ipc/bk_mailbox_rpmsg.c`（它提供 `mbox0_adapter.c` 的全部全局符号），`mbox0_drv.c` 只被 adapter 调用。 |
  | 6 | 4 | `middleware/driver/icu/interrupt_base.c`、`middleware/arch/cm33/arch_interrupt.c`、`middleware/driver/bk7258/interrupt.c` | 资源冲突 | NVIC 只能有一个主人。NuttX 侧：`nuttx/arch/arm/src/armv8-m/arm_vectorirq.c`、`irq_attach`。Armino 侧：`bk_int_*` 层、自己的 NVIC 驱动和向量处理器。替代：`compat/bk7258_armino_irq.c` 把源 n 映射到 `NVIC_IRQ_FIRST + n`。 |

  补充说明（同样属于排除依据）：`lwip` 的 `sockets.h`、`inet.h`、`netdb.h`、`stats.h` 不是排除单元而是替换头（`lwip_os/`），理由见 S2.7；`bk_wifi/include/wifi_spinlock.h` 同理（`compat/bk7258_wifi_spinlock.h`）；`ram_regions.h` 换成 `pm/nuttx_port/include/ram_regions.h`（S2.2 第四处偏离）。这些是编译期替换，不涉及排除编译单元，故不在上表。(covers: S2.7; depends: T8)
- [x] T8: 阶段一 CMake 接入和首次链接。结果：开关 `BK7258_WIFI_ARMINO_BASELINE`、`chips/bk7258/wifi_armino/`、`configs/cp-armino`；保留单元全部编译通过（第 6 次 keep-going 构建只剩链接失败）。首次链接清单在 `chips/bk7258/tools/armino_baseline/phase1_link.txt`：93 个 undefined（os_source 48、lwip 31、链接脚本符号 10、其他 4）、6 个重复定义（`bk_mailbox_*`）、flash 超出 111660 字节。开关关闭时 `cp` 的 `app.bin` 仍是 `1c8011f5`；`nuttx.elf` 去掉调试段后与改动前逐字节一致（`425b6535`），差异只在 `bk7258_bringup.c` 的行号表。新的 Kconfig 开关打开时，CP 的 CMake 读取 T4 清单，按组件参数编译保留单元，路径来自 `BK7258_ARMINO_BASELINE_DIR`，配置用基线 `sdkconfig.h`。acceptance：开关关闭时，`a7de5d2` 的构建结果不变；开关打开时，所有保留单元都能编过；链接输出里的 undefined 和重复定义符号整理成清单入库。(covers: S2.2, S2.4; depends: T3, T4)
- [x] T9: OS 接缝。在 NuttX 上实现 T8 清单里 FreeRTOS API 那部分 undefined 符号，供原样保留的 `bk_rtos` 调用。sdk-source 分支修过的 9 个问题（队列 item_size、栈按字计、临界区嵌套、ISR 上下文判断、`nxmq_*` 无取消点等）要带过来。acceptance：这些符号各定义一次；主机测试覆盖队列 item_size、栈按字计、临界区嵌套、ISR 上下文发送四项。结果（2026-10-03，`compat/bk7258_freertos_nuttx.c` + `board/bk7258-devkit/tests/test_bk7258_os_seam.py`）：接缝符号各定义一次（cp-armino 链接无 multiple definition）；`python3 test_bk7258_os_seam.py` 5 项全过，四项分别为 16 字节消息原样往返、32 字栈深变成 `32*4+1024` 字节、两层临界区只在最外层用首次保存的标志恢复中断一次、ISR 里对满队列发送返回 `errQUEUE_FULL` 且不进临界区。测试编译真实接缝源码并链接打桩的 NuttX 原语（`tests/os_seam/`），验的是转换逻辑本身而非副本。板上另有一条独立证据：`rtos_set_semaphore` 在中断上下文走 `xSemaphoreGiveFromISR`，SARADC 中断每秒触发并由信号量同步；队列的 FromISR 路径仅由主机测试覆盖。(covers: S2.2, S2.7; depends: T7, T8)
- [x] T10: netdev 胶水。实现 T8 清单里原本由 lwip 提供的符号，接到 NuttX netdev，行为按 S2.9。acceptance：链接 map 里 lwip 目标文件只有 S2.7 列出的 6 个保留单元，ELF 里没有 `tcp_*`、`tcpip_*`、`udp_*`、`etharp_output`、`dhcp_start`；连接后不需要 `renew`，UART 出现 `[ARMINO] wlan0 ip <地址>`，`bk_wifi_sta_get_linkstate_with_reason` 报 `GOT_IP`；ping 网关 4/4；断开再连接，能再次拿到地址且 wlan0 上没有残留的旧地址。验收标准 2026-10-03 修改：原来写的"链接 map 里没有 lwip 目标文件"与 S2.7 的 1A 决定冲突；原来写的"`renew wlan0` 能拿到地址"与 S2.9 的 2A 决定冲突（地址由连接事件自动获取）。结果（2026-10-03，固件 `b5dcfc40c319f66c8b7e1b6fad5bc7d23885d7370af58cfdf8d98a6753118583`，kit `bk7258-kit-f9abf35-b5dcfc40`，板测日志 `~/102-12.log`，热点 `<SSID>`）：(1) `nm` 显示 lwip 目标只有 `pbuf.c`、`mem.c`、`memp.c`、`def.c`、`ip4_addr.c`、`stats.c`，ELF 无 `tcp_*`/`tcpip_*`/`udp_*`/`etharp_output`/`dhcp_start`；(2) 连接后 UART 有 `[ARMINO] wlan0 ip <LAN-IP>` 和 `BK STA got ip4`（`GOT_IP`），未用 `renew`；(3) `ping <LAN-IP>` 两次均 10/10、0% 丢包；(4) `armino_sta disconnect` 后 `[ARMINO] sta ip down`、`ifconfig` 为 `0.0.0.0`，重连走完整 `SCANNING → … → COMPLETED` 流程并再次拿到地址，ping 10/10。(covers: S2.2, S2.7, S2.9; depends: T7, T8)
- [x] T11: 阶段一链接和上板验收。acceptance：链接没有 undefined 符号；重复定义只出现在 T7 记录过的位置；L1 校验、decode、完整性门禁都通过；`<SSID>` 上连接 3/3；ping 公网 4/4；T5 对比显示重握手次数、`tx_dma_dead` 次数不多于 Armino 参照。结果（2026-10-03，固件 `b5dcfc40c319f66c8b7e1b6fad5bc7d23885d7370af58cfdf8d98a6753118583`，kit `bk7258-kit-f9abf35-b5dcfc40`，热点 `<SSID>`）：(1) cp-armino 链接 0 个 undefined、0 个 multiple definition（`nm` 与链接输出）；(2) L1 校验 pass、打包 `decode pass`、kit 的完整性门禁 pass；(3) 连接 3/3：同一固件上 3 次成功 0 次失败（`~/102-12.log` 的连接与断开重连，`/tmp/102-13.log` 的连接），每次均有 `[ARMINO] wlan0 ip` 与 `BK STA got ip4`；(4) `ping 223.5.5.5` 10/10、0% 丢包（在 AP 的 NSH 上经 RPMsg usrsock 走 CP 的 wlan0，CP 侧另有多次 ping 网关 10/10）；(5) T5 对比 `/home/czp/iperf-ref-1001.log` 与 `/tmp/102-13.log`：重握手 0 对 0、`tx_dma_dead` 0 对 0，握手序列逐条相同，退出 0（candidate not worse）。注意：日志证据里的密码需替换为 `<PSK-REDACTED>`，`WPA: TK` 行需删除，方可入库。(covers: S2.2, S2.6; depends: T5, T9, T10, T20)
- [ ] T12: 阶段二：电源与低功耗原生化。范围（S2.5 全表「电源」+「中断控制器」，16 个目标文件）：PM 组 `pm`、`pm_misc`、`low_vol`、`pwr_clk`、`low_pwr_core`、`low_pwr_misc`；AON PMU 组 `aon_pmu_driver`、`aon_pmu_hal`；电源状态组 `sys_pm_hal`、`sys_ps_driver`、`sys_psram_driver`、`sys_wifi_driver`；SoC 控制组 `sys_driver`、`sys_driver_common`、`sys_hal`；中断控制器 `sys_int_driver`。NuttX 侧落点：PM 框架 / 时钟电源控制。**换完后 `driver_init()` 注册表里这些项消失，初始化改在 `board_late_initialize()`/`up_initialize()` 注册**。acceptance：链接 map 证明这些目标文件不再来自 Armino 源文件；闭源库 `libwifi.a`/`libbk_phy.a` 对这批的调用（`bk_api_*`、`drv_model` 名）仍能解析且语义一致；T5 对比没有新增差异；连接 3/3；ping 4/4。(covers: S2.5; depends: T11)
- [ ] T13: 阶段二：DMA、时钟、定时器原生化。范围（S2.5 全表「DMA」+「时钟」+「定时器」，10 个目标文件）：DMA 组 `dma_driver`、`dma_hal`；时钟组 `sys_clock_driver`、`ckmn_driver`、`ckmn_hal`、`rosc_32k`；定时器组 `timer_driver`、`timer_hal`；AON RTC 组 `aon_rtc_driver_64bit`、`aon_rtc_hal_64bit`。NuttX 侧落点：DMA / clock / timer / RTC 驱动。acceptance 同 T12。(covers: S2.5; depends: T12)
- [x] T14: 阶段二：Flash 与内存原生化。范围（S2.5 全表「存储」的 Flash + PSRAM，9 个目标文件）：`flash_driver`、`flash_driver_ext`、`flash_partition`、`flash_hal`、`flash_bypass`、`flash_notify`、`flash_server`、`psram_driver`、`psram_hal`。改为基于 NuttX MTD（PSRAM 走 NuttX 内存侧）。acceptance 同 T12，另外 easyflash 写入的数据重启后仍在。(covers: S2.5; depends: T11)**进度（2026-10-27）**：9 个单元全部换成我们的源码，map 里 0 个 Armino 成员。PSRAM 两个单元收成 `chips/bk7258/bk7258_psram.c`（29/29 导出，板上两核分片 `bk7258_psram_test` 均 PASS）；flash 族 7 个挂了 `mtd_dev_s`，注册 `/dev/mtd0`（4096/4096/2048），`bk7258_drvtest mtd` 证明 MTD 面与 `bk_flash_read` 同区间读出相同；easyflash 写 `probe=1234` 重启后仍在。CP flash 余量 421.7 KiB。未复测：本镜像连接 3/3。细节见契约文档批次 14、15。**验收（2026-10-27）**：9/9 单元退出 Armino 源；PSRAM 两核分片 `bk7258_psram_test` PASS；`bk7258_drvtest mtd` 显示 MTD 面与 `bk_flash_read` 同区间相同；easyflash 写 `probe=1234` 重启后仍在（镜像 `bf68d649…`）；连接与 ping 由用户确认正常（未逐次计 3/3、未留日志）。
- [ ] T15: 阶段二：校准、采样与身份原生化。范围（S2.5 全表「采样」+ 校准/身份，16 个目标文件）：ADC 组 `adc_driver`、`adc_statis`、`adc_hal`；SDMADC 组 `sdmadc_driver`、`sdmadc_hal`；SARADC 服务 `saradc_notify`、`saradc_server`；OTP/EFUSE 组 `otp_driver_v1_1`、`otp_hal`、`efuse_driver`、`efuse_hal`；随机数 `trng_driver`、`trng_hal`；身份 `bk_uid`、`bk_uid_adaptor`；校准 `vnd_cal`。acceptance 同 T12，另外校准日志里的 `xtal`、`pwr_gain` 与参照一致。**进度（2026-10-07）：TRNG 一组已完成并上板**——`chips/bk7258/bk7258_trng.c`（提交 `835316a`）提供同名 6 个符号（`bk_trng_driver_init/deinit`、`bk_trng_start/stop`、`bk_rand`、`bk_fill_rand`），语义按 `trng_driver.c` 逐条保留（丢弃 16 个读数、使能后等 50 µs、`& RAND_MAX`、小端字填充）。**替换是彻底的**：`bk_trng_driver_init()` 由 `board_late_initialize()` 调用，不再依赖 Armino 的 `driver_init()` 与 `#if CONFIG_TRNG_SUPPORT`（基线 `driver.c:275`）——否则那个门一关，这里所有入口都是空操作，`bk_rand()` 之所以能用只是因为 `bk7258_urandom.c` 顺手开了块；init/deinit/start/stop 的契约本身没动，显式 deinit 之后守卫仍会报 `BK_ERR_TRNG_DRIVER_NOT_INIT`。**源码已按「换一处删一处」清掉 5 个**：`trng_driver.c`、`trng_driver.h`、`trng_hal.c`、`trng_ll.c`、`trng_hal_debug.c`。留下的 11 个 trng 头是被包含链拉着的：`cli_reg.c` 包 `trng_hal.h`，`driver/trng.h`（调用者的契约声明）包 `trng_types.h`，`soc_cap.h` 包 `trng_cap.h`；`cli_trng.c` 是调用者，照旧编译。`armino_units.tsv` 是基线单元表（记录权威源的全集），保留不动；`excluded_units` 里的条目是「谁顶替了它」的记录。闭源库对这 6 个符号引用为 0。板测（2026-10-07，固件 `9dc97334…`，kit `bk7258-kit-505bae4-trng-9dc97334`）：ai_agent LLM 往返正常回复，即 TLS 握手与 X509 通过——psa_mbedtls 的熵走 `bk_rand`，是本项的关键路径。**UART 日志仍需入档**作为 S2.6 的证据（暂缺）。已知保留的既有行为：`bk_trng_stop()` 会关掉 TRNG 块，而 `bk7258_urandom.c` 缓存 `g_trng_enabled` 不重开，故一次 `bk_rand()` 之后 `/dev/urandom` 可能读到静默源；替换前就是这个行为，本次未改。(covers: S2.5; depends: T11)
- [ ] T16: 阶段二：跨核原生化。范围（S2.5 全表「跨核」的 mailbox 侧 + AMP 锁，8 个目标文件）：`mb_ipc_cmd`、`mb_ipc_heartbeat`、`mb_chnl_buff`、`bk_api_ipc`、`bk_api_rpc_server`、`amp_res_lock`，以及基线清单里的 `mailbox_channel`、`mb_ipc`（本镜像里对应我们自己的 `armino_cp_mailbox_channel`、`armino_cp_mb_ipc`，见 S2.10；基线里那两个名字不在我们链接表上）。阶段一它们经 RPMsg 端点 `bk7258-mb-ipc` 运行（S2.10）；`bk7258-armino-otp-mb-ipc.md` 已于 2026-10-06 标为 superseded，不再作为对齐目标。**`controller_if` 7 个是 `libwifi.a` 的接口面，本项不碰。**acceptance 同 T12。(covers: S2.5; depends: T11)
- [ ] T17: 阶段二：通用外设与杂项原生化。范围（S2.5 全表「复位」+「通用」+「打印」+ 系统杂项中属硬件的，25 个目标文件）：中断/异常 `trap_base`、`arm`、`stack_base`（基线清单的 `interrupt_base` 在本镜像不存在）；打印 `printf`、`printf_base`；UART `uart_driver`、`uart_hal`；看门狗 `wdt_driver`、`wdt_hal`、`aon_wdt_driver`、`aon_wdt_hal`；GPIO `gpio_driver`、`gpio_driver_base`、`gpio_hal`；复位 `reset_reason`、`reboot`；触摸 `touch_driver_v1_1`、`sys_touch_driver`；温度/电压 `temp_detect`、`bk_sensor`、`volt_detect`；音频 `sys_audio_driver`；驱动注册表 `driver`、`drv_model`、`dd`（随各模块迁走而缩小）。**不纳入本项**：`controller_if` 7 个 + `bk_phy` 5 个（闭源接口面，[必留]）、`bk_wifi` 26 个（MAC，无 NuttX 等价物）、`bk_cli`/`at_server`/`easy_flash` 等工具层（另行决定）。acceptance 同 T12。(covers: S2.5; depends: T11)
- [ ] T18: 最终验收。跑 30 分钟 TX 负载（优先 `iperf -c`；热点有客户端隔离时，改用最大包长的连续 ping 公网）。acceptance：`tx_dma_dead`、assert、重启均为 0 次；ping 20/20；证据按 S2.6 齐全；**链接 map 上属硬件类的 Armino 目标文件只剩 S2.5 标 [必留] 的 12 个（`controller_if` 7 + `bk_phy` 5）**。判据说明（2026-10-07 随 T6 取消而调整）：原写法是「不多于 T6 中 Armino 的结果」，T6 取消后改为绝对门槛；负载方式从 T6 移到本行。范围判据（2026-10-07 随 S2.5 全表补记而增加）：防止只做完原 27 个就以为齐了。(covers: S2.6; depends: T12, T13, T14, T15, T16, T17)
- [x] T20: CPU1 归属，按 S2.10 由 NuttX 独占。acceptance：上板日志中 `[AP] start: cpu1 powered up` 和 `[AP] release` 各出现一次，且 `[ARMINO] CPU1 powered up by NuttX after <n> ms` 出现在 Armino 启动日志之前，没有 `still powered down`；连接后 10 分钟内 AP 没有重启（不出现第二次 `[AP] release`，也没有新的 AP 启动横幅）；AP 的 NSH 可用；在 AP 上经 usrsock ping 网关 4/4。说明：Armino 的 `reset_cpu1_core at:`、`boot cp1 fail` 是 `BK_LOGD` 级别，可能被过滤，不作为判据。结果（2026-10-03，同上固件 `b5dcfc40…`，板测日志 `~/102-13.log` 与串口观察）：(1) `[AP] start: cpu1 powered up, cpu_status=00007000 after 0` 与 `[AP] release seq=1` 各 1 次；(2) `[ARMINO] CPU1 powered up by NuttX after 0 ms` 在 `[ARMINO] queue 1` 之前，无 `still powered down`；(3) AP 启动日志显示 `ram=[28010000..28054000]`，即 S2.10 的新窗口，`[AP] stage=scheduler-running seq=1 fault=0x00000000`；(4) `cu -l /dev/ttyAP` 进入 `nsh-ap>`，音频/GDMA/PA/按键注册正常；(5) AP 上 `ping <LAN-IP>` 10/10、0% 丢包（经 RPMsg usrsock 走 CP 的 wlan0）；(6) 连接保持 11 分钟以上（日志到 527 s），无第二次 `[AP] release`、无新启动横幅、无 FAULT/assert，其后 ping 仍 10/10。(covers: S2.10; depends: T10)
- [x] T19: 导入仓库。把阶段一仍在使用的 Armino 原文件按 AGENTS.md 导入 `chips/bk7258/`，保留模块结构和来源记录，去掉对 `BK7258_ARMINO_BASELINE_DIR` 的依赖。acceptance：干净检出、不设该变量也能构建；T3 门禁对导入目录返回 0。结果（2026-10-06，提交 `5ce85c3`；2026-10-07 补记 checkbox，此前做完了没勾）：(1) `chips/bk7258/armino_baseline/` 2065 个文件 / 34.5 MB = 470 个保留单元 + 全部 130 个 `-I` 目录下的头文件 + 生成的配置头 + 6 个闭源 `.a`，基线 `d5b0278`，来源记在 `chips/bk7258/armino_baseline/PROVENANCE.md`；(2) `chips/bk7258/wifi_armino/CMakeLists.txt` 的 `BK7258_ARMINO_BASELINE_DIR` 默认改指它并去掉 git-HEAD 校验，不设该变量编 CP 和 AP 均通过；去调试段的 `nuttx.elf` 与导入前逐字节一致（CP `3281ae83…`、AP `ee4fe515…`），`objdump -d` 只差自身的文件路径行；(3) T3 门禁对导入目录 identity map 2058 相同 / 0 修改，返回 0；(4) 2026-10-07 复核：`env -u BK7258_ARMINO_BASELINE_DIR` 从 `5ce85c3` 干净检出重编 CP 2091 个目标通过，镜像 `83d83bba…`，板测 wlan0 拿到 IP、网关 ping 4/4。已知残留：清单里 `cp_main.c` 的 `__FILE__` 是绝对路径（其余 469 个是 `components/…`），只影响 assert 字符串。(covers: S2.2, S2.3; depends: T11)
