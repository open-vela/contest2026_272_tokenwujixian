---
feature: bk7258-native-wapi
status: delivered
updated: 2026-09-17
branch: feature/bk7258-wifi-native-wext
commits: 075a3703405e3f9206db22be87595ed374ad64d0..dcc3507
---

# BK7258 原生 WAPI 控制路径

## Report

**What was built** — BK7258 Wi-Fi 的生命周期和 STA 控制面从自定义 `bk7258_wifi_runtime` app 迁移到 board bringup + 标准 WAPI/WEXT 路径。board late bringup 创建 8 KiB kthread 自动完成 vendor 初始化并在进入 NSH 前注册 `wlan0`；`wireless_ops_s` 的 mode/auth/passwd/essid/connect/disconnect handler 接收标准 WEXT ioctl，用户态通过 `wapi` CLI 完成扫描、WPA2-PSK 关联、DHCP 和 ping。runtime app 已删除。

**Verification** — CP/AP fresh build + L1 + package/decode 通过（T1-T4 各一次）。真机验收：冷启动后 `wlan0 ready before userspace`（0.85 s），全程未执行任何 runtime 命令，WAPI 完成 scan → WPA2-PSK/CCMP 关联 → 四次握手 → DHCP（192.168.190.248）→ 网关 ping 4/4（34-39 ms）→ DNS 解析 → 外部 ping 10/10（26-67 ms）。T4 删除 runtime 后重新回归通过（三次尝试，符合 ~30% 扫描空结果基线）。

**Journey log** —
1. `wapi` CLI 的 mode/alg/wpa_ver 参数只接受数字索引或完整字符串名（如 `2`、`3`、`WPA_ALG_CCMP`），不接受缩写（`managed`、`ccmp`、`wpa2`）。`wapi_str2ndx` 对未匹配字符串调用 `exit(EXIT_FAILURE)`，进程静默退出。
2. `CONFIG_WIRELESS_WAPI_STACKSIZE` 默认值 ~2 KB，vendor connect 链（`bk_wifi_sta_set_config` → `bk_wifi_sta_start` → `wpa_psk_request` → `wpa_supplicant` 初始化）嵌套极深导致 Usage Fault (Stack Overflow)。修复：提到 8192，与 runtime app 当初用专用 8 KiB kthread 的原因一致。
3. RX quota=1 导致 ARP 回包被静默丢弃：`netpkt_alloc()` 在 quota 耗尽时返回 NULL，帧在 `rx_submit` 里被释放，不经过 netdev 统计层，所以 `ifconfig` 显示 `Dropped=0`。家庭网络的 mDNS/SSDP/邻居 ARP 会挤掉网关的 ARP 回包。修复：quota 提到 4。之前 quota=8 与 scan 挂起的相关性是巧合（探针证明改动代码从未执行）。
4. 外部 ICMP echo 不通是路由器/ISP 策略，不是驱动问题。DNS（UDP）能解析已证明完整协议栈正常。验证 ICMP 应先 ping 网关。
5. 约 30% 的扫描空结果是已知基线，重试即可，不是回归。

## [S1] 问题

BK7258 Wi-Fi 的数据面已经通过 NuttX netdev 接入并在真机上完成扫描、关联、四次握手、DHCP 和 ping 验证，但生命周期和 STA 控制面仍由 `bk7258_wifi_runtime` 应用承担。系统启动后不会自动初始化 Wi-Fi，用户必须先执行应用命令；连接也直接调用团队封装的 vendor API，没有经过 OpenVela/NuttX 的 WEXT、`wireless_ops_s` 和 WAPI 路径。

目标形态与其他 OpenVela board 一致：board bringup 在进入用户态前完成无线设备初始化，用户态使用仓库已有的 `apps/wireless/wapi` 控制 `wlan0`。迁移期间保留 runtime app 作为已验证路径的对照，只有原生路径在真机上完成同等网络验收后才删除它。

## [S2] Board 拥有 Wi-Fi 生命周期

启用 `CONFIG_BK7258_WIFI_VENDOR_RUNTIME` 的 CP 镜像必须从 board late bringup 自动启动 Wi-Fi。board 创建一个 8192 字节栈的一次性初始化 kthread，由该线程完整调用现有 `bk7258_wifi_initialize()`；不得把 TRNG、随机数播种、SARADC、PHY/RF、校准、vendor 线程创建或 netdev 注册拆到其他线程。这样保留当前已验证的初始化顺序，也保留 NuttX 按 task group 保存 PRNG 状态的约束。

board late bringup 使用完成信号等待该线程，最长 30 秒。初始化在期限内成功后才继续进入用户态，因此正常启动时 `wlan0` 已注册且 WAPI 可立即使用。初始化失败或超时不得永久阻塞开机：必须记录明确错误并继续进入 NSH；失败后不自动重跑可能已经部分执行的硬件初始化，尚未注册 `wlan0` 时 WAPI 按网络设备查找语义返回 `ENODEV`。

初始化入口必须是并发安全的一次性状态机：`NOT_STARTED -> INITIALIZING -> READY` 或 `FAILED`。首个调用者执行初始化；初始化中的重复调用返回 `-EINPROGRESS`，READY 后重复调用返回成功，FAILED 后返回首次失败结果。READY 只能在完整初始化函数成功结束后发布。

## [S3] WAPI/WEXT STA 控制契约

启用仓库已有的 `WIRELESS_WAPI` 和 `WIRELESS_WAPI_CMDTOOL`，不修改 NuttX 或 apps 仓库。BK7258 lower-half 通过现有 `wireless_ops_s` 接收标准 WEXT ioctl，并为 STA MVP 实现以下契约：

- `mode` 支持设置和读取 `IW_MODE_INFRA`；其他模式返回 `-EOPNOTSUPP`。
- `auth` 保存并读取 WAPI 依次提交的 WPA version 和 pairwise cipher；首个交付范围为开放网络和 WPA2-PSK/CCMP，不支持的组合返回 `-EOPNOTSUPP`。
- `passwd` 从 `SIOCSIWENCODEEXT` 接收并边界检查 PSK。读取时只报告已配置/无密钥状态，不复制或打印 PSK。
- `essid` 保存和读取连接 SSID。`IW_ESSID_DELAY_ON` 只更新扫描选择；`IW_ESSID_ON` 由 NuttX upper-half 在保存成功后调用 `connect`；OFF 由 upper-half 调用 `disconnect`。连接 SSID 与现有 directed-scan SSID 必须分别保存，不能互相污染。
- `connect` 校验初始化状态、mode、auth、SSID 和密钥组合后，复用现有 `bk7258_wifi_sta_connect()` 及其真实 `bk_wifi_sta_set_config()/bk_wifi_sta_start()` 路径。ioctl 在异步关联请求被接受后返回，不轮询握手完成。
- `disconnect` 调用 authority 已提供的 `bk_wifi_sta_disconnect()`，不得用空成功、手写状态清理或关电代替。已有 connected/disconnected event 继续唯一负责 NuttX carrier on/off。

现有扫描实现、数据面和 EAPOL 分流保持不变。未纳入 STA MVP 的 BSSID 锁定、SoftAP、monitor、WPA3、WEP、TKIP、频率、速率、发射功率和省电控制必须返回明确的不支持，不得伪造成功。任何日志都不得包含 PSK、PMK 或派生密钥。

## [S4] 迁移验证和 runtime 退役门槛

迁移分两次验证。首先在 runtime app 仍编译存在时完成 host 构建、L1 校验和 package/decode，证明 board 自动初始化与 WAPI handler 可以集成。随后使用真机冷启动，整个过程中不执行 `bk7258_wifi_runtime init/scan/connect`，仅通过 WAPI 完成：启动后发现 `wlan0`、扫描、配置 WPA2-PSK/CCMP、按 SSID 关联、四次握手、DHCP 获取地址和 ping 网关。

真机证据必须记录测试 commit、构建产物对应关系、烧录步骤和脱敏 UART 日志。约 30% 的既有扫描空结果只允许按现有已知基线重试，不能把一次失败或一次重试写成新路径通过；最终通过记录必须包含连接、DHCP 地址和 ping 回包。

只有上述 WAPI 真机验收通过后，才能删除 `bk7258_wifi_runtime` 应用及其 Kconfig、defconfig、manifest linkfile、runbook 和只为该应用暴露的诊断入口。删除后重新执行 CP/AP 构建与 package/decode，并用纯 WAPI 做一次真机回归。低层 `txtest` 若仍是定位 vendor TX 的唯一入口，应先迁到明确的诊断设施；不得因删除应用而丢失仍有独立诊断价值的能力。

## [S5] 范围外

- 修改 OpenVela/NuttX 的 WEXT upper-half 或 `apps/wireless/wapi`。
- 新增第二套 supplicant、网络栈或 vendor Wi-Fi 生命周期。
- SoftAP、P2P、monitor、WPA3、WEP、TKIP 和持久化保存 Wi-Fi 凭据。
- 在未获得明确授权时烧录设备；host build/package 不能替代真机网络验收。
- 处理既有的偶发空扫描和 serial.c 断言，除非新路径提供可重复的独立回归证据。

## Tasks

- [x] T1: 将 Wi-Fi 生命周期迁入 board late bringup，并实现 30 秒有界等待和一次性初始化状态机。acceptance: 启用 vendor runtime 的 CP 在不运行任何应用命令时于进入 NSH 前注册 `wlan0`；失败/超时有明确日志且 NSH 仍可进入；重复初始化不会执行第二次硬件/vendor 初始化（covers: S2）。commit `0ab3f1a`。
- [x] T2: 启用 WAPI command tool 并完成 BK7258 STA WEXT 控制契约，迁移期间保留 runtime app。acceptance: CP/AP fresh build、L1 校验和 package/decode 通过；WAPI 的 mode/auth/passwd/essid/connect/disconnect/scan 经 `wireless_ops_s` 到达真实 vendor API，PSK 不出现在日志或读取结果中（covers: S3; depends: T1）。commits `76005b5` + `5423cfc`。
- [x] T3: 在真机上验证不依赖 runtime app 的原生路径。acceptance: 一次有完整证据的冷启动中未执行任何 `bk7258_wifi_runtime` 命令，仅使用 WAPI 完成 wlan0 可见、扫描、WPA2-PSK/CCMP 关联、四次握手、DHCP 和网关 ping；日志已脱敏并绑定测试 commit（covers: S4; depends: T2）。commit `5423cfc`。硬件证据: 网关 ping 4/4, DNS 解析成功, 外部 ping 10/10。
- [x] T4: 在 T3 通过后删除 runtime app 及其专属配置和映射，并保留仍有价值的低层诊断能力。acceptance: 仓库不再构建或映射 `bk7258_wifi_runtime`，CP/AP fresh build、L1、package/decode 通过，纯 WAPI 真机回归仍完成关联、DHCP 和 ping（covers: S4; depends: T3）。commit `dcc3507`。真机回归通过（三次尝试，第三次成功，符合 ~30% 扫描空结果基线）。
