---
feature: bk7258-wifi-porting
status: in-progress
updated: 2026-08-26
branch: feature/bk7258-wifi
commits: # filled at delivery
---

# BK7258 Wi-Fi OpenVela/NuttX 适配 — STA-only runtime bring-up

## Report

## [S1] Problem

BK7258 DevKit 需要在 OpenVela/NuttX 上提供标准 `wlan0`，让上层通过 WAPI、DHCP、DNS 和 POSIX socket 使用网络。当前只有 Armino/FreeRTOS 版的 Beken 二进制核心（`libwifi.a`、`libbk_phy.a`），没有 NuttX 版库，vendor ABI（Cortex-M33 hard-float、CMSE、SPE/NSPE）未验证，CP SRAM 窗口约 195 KiB。

适配主路径（详见 `my_docs/bk7258-wifi/plan/OPENVELA_NUTTX_WIFI_PORTING_PLAN.md` §11.4，该文档已迁出本仓库，绝对路径 `/home/czp/openvela_contest/my_docs/bk7258-wifi/plan/`）是：保留 Beken MAC/PHY/RF 与 WPA 核心，用 NuttX OSAL 替换 FreeRTOS，用 `netdev_lowerhalf` + NuttX 网络栈替换 Armino lwIP/DHCP/socket。

当前工作块已经超出纯骨架阶段：团队已完成 capability owner、NuttX OSAL、
timer/workqueue、vendor packet/pbuf copy bridge、netdev lower-half、IRQ、
BK7258 SARADC 生命周期和 vendor source/build 接入。当前目标严格是 STA
功能链：扫描、建立 WPA/EAPOL 状态机、连接、断开和后续 DHCP；SoftAP、P2P、
WPS、monitor 与 STA+AP 并发不属于本阶段。

CP 与 `runtime-probe` 两个配置均已启用
`CONFIG_BK7258_WIFI_VENDOR_RUNTIME=y` 与 `CONFIG_BK7258_WIFI_RUNTIME_APP=y`，
两者最终链接均已收敛（未解析符号 0）。Wi-Fi 仍不会开机自启：
`bk7258_wifi_initialize()` 的唯一调用点在 `bk7258_wifi_runtime` 应用内，
board bring-up 不含任何调用点，必须由 NSH 手动调用。

`runtime-probe` 镜像已烧录并取得实板日志（2026-09-03）：扫描、ASSOCIATED、
4WAY_HANDSHAKE、收到 M1 并导出 PTK、构建并发出 M2。连接尚未建成——M2 始终
未被确认（`l2tx cfm status=00000001 acked=0 done=1`，重试 3 次后本地放弃，
`locally_generated=1`），且被 `wpas_notify_psk_mismatch` 误报为 WRONG_KEY，
实际并非密码错误。CONNECTED 状态与 DHCP 仍无证据，未解问题是 TX 确认路径。
CP 配置本身尚未打包烧录。

## [S2] Design

### 架构边界

```text
NuttX netdev_lowerhalf + wireless_ops_s   （团队实现）
        │ NetPKT
vendor packet shim（pbuf ABI + private reserve）  （团队实现）
        │
Beken libwifi.a / libbk_phy.a             （外部输入，runtime-on probe 条件链接）
        │
BK7258 MAC / PHY / RF
```

### 团队代码目录

```text
chips/bk7258/wifi/
├── bk7258_wifi_internal.h   # 私有结构 + 接口契约 + vendor ABI 常量
├── bk7258_wifi_lower.c      # netdev_ops_s + wireless_ops_s + register
├── bk7258_wifi_osal.c       # NuttX OSAL（thread/queue/timer/lock/irq/memory）
├── bk7258_wifi_packet.c     # vendor packet shim（708-byte private reserve）
├── bk7258_wifi_hw.c         # MAC/PHY clock/power/IRQ glue（占位）
├── CMakeLists.txt           # 条件编译
├── Make.defs                # 条件编译
├── Kconfig                  # CONFIG_BK7258_WIFI 及子选项
├── ABI_AUDIT.md             # P0 产物：二进制未解析符号分类 + 库 hash
└── beken_patch_manifest.md  # P0 产物：Beken 集成 patch/库/许可证清单模板

board/bk7258-devkit/src/
└── bk7258_wifi_board.c      # 板级 MAC/校准/国家码/PA-LNA 配置（占位）
```

### 关键合同（冻结自 plan §4、§11.4）

- 注册：`netdev_lower_register(&lower, NET_LL_IEEE80211)`；数据面 `netdev_ops_s`，控制面 `wireless_ops_s`。
- 数据面首版采用边界拷贝：TX `NetPKT → vendor packet → MAC`，RX `MAC → vendor packet → NetPKT`；禁止把 NetPKT 强制转换为 vendor pbuf。
- vendor packet private reserve 常量：`CONFIG_MSDU_RESV_HEAD_LENGTH=108` + `CONFIG_MSDU_RESV_DESC_LENGTH=600` = 708 字节；以 Kconfig 暴露并在运行时/编译期断言。
- OSAL 仅映射到 NuttX 原语，禁止在 NuttX 旁运行第二个调度器；禁止批量无语义 stub。
- EAPOL/WAI 分流给 vendor WPA 路径，不进 NuttX IP 数据路径。
- 关联成功后才 `netdev_lower_carrier_on()`；DHCP lease bound 后调用 `wlan_dhcp_done_ind(vif_idx)`。
- 团队仓不复制 Armino 源码；Beken 集成修改以 patch manifest 交付。

### 当前构建与验证状态

已在独立 worktree/build slot 中执行 CMake configure 和 runtime-on probe
构建。runtime-off 曾有通过记录，但当前改动后仍需重新确认；runtime-on
当前停在链接闭包阶段。不得把 host 编译通过、默认镜像链接通过或
`runtime-probe` 产物当作真机 STA 证据。

## 当前实现状态

| 阶段 | 状态 | 说明 |
| --- | --- | --- |
| Capability/ABI/OSAL | 已完成 | generated ABI 保持原布局，P0/P1 OSAL 已接入 |
| packet/netdev bridge | 已完成（host evidence） | pbuf/vendor packet 使用显式 copy，未宣称 zero-copy |
| hardware/SARADC glue | 部分完成 | SARADC MMIO 生命周期和校准表已接入，reset/RF/板上测量未完成 |
| STA WPA source closure | 进行中 | 按 SDK non-P2P manifest 接入，runtime-on 当前停在最终链接 |
| STA scan/association | 未开始实板验收 | 依赖 runtime link、RF 时序、WPA/EAPOL 和控制面闭合 |
| DHCP/IP/stability | 未开始实板验收 | 依赖 carrier/IP-ready、DMA/cache 和 pbuf reserve 证据 |

## [S3] Out of Scope

- 真机验证（RF 扫描、WPA 关联、DHCP、DNS、MQTT）——当前尚未执行。
- 修改 NuttX 上游或 Armino 集成代码（本块只产出 patch manifest 机制，不实现 patch）。
- SoftAP、P2P、STA+AP 并发、monitor、WPS 和 BT coexistence。
- RPMsg 跨核网络共享（usrsock/L2）。
- flashing；在 runtime-on 链接、RF 时序和 STA 控制面验证完成前禁止烧录。
- 自动启动 vendor runtime；当前只能由 `bk7258_wifi_runtime init` 手动触发。

## Tasks

- [ ] T1: 建立 `chips/bk7258/wifi/` 骨架与 `bk7258_wifi_internal.h` — acceptance: 私有结构、vendor packet/reserve 常量、OSAL/lower-half/hw 接口声明齐备且相互一致 (covers: S2)
- [ ] T2: 新增 `chips/bk7258/wifi/Kconfig` — acceptance: `CONFIG_BK7258_WIFI` 及子选项、vendor packet reserve 常量、vendor 库开关语义明确，可被 menuconfig 解析 (covers: S2)
- [ ] T3: 实现 `bk7258_wifi_osal.c`（NuttX OSAL 映射） — acceptance: thread/queue/timer/lock/irq/memory 每项映射到 NuttX 原语并有调用上下文语义注释，无无语义 stub (covers: S2)
- [ ] T4: 实现 `bk7258_wifi_packet.c`（vendor packet shim） — acceptance: alloc/free/ref/push/pull/coalesce/SG 枚举 + 708-byte private reserve，接口与 T1 契约一致 (covers: S2)
- [ ] T5: 实现 `bk7258_wifi_lower.c`、`bk7258_wifi_hw.c` 与板级 `bk7258_wifi_board.c` 占位 — acceptance: netdev_ops_s/wireless_ops_s/register/carrier 接口齐备，hw 门控与板级配置为明确占位而非空实现 (covers: S2)
- [ ] T6: 新增 `chips/bk7258/wifi/CMakeLists.txt` + `Make.defs` 条件编译 — acceptance: 仅在 `CONFIG_BK7258_WIFI=y` 时编入芯片组件，AP/CP 分支语义正确 (covers: S2)
- [ ] T7: 产出 P0 分析产物 `ABI_AUDIT.md` + `beken_patch_manifest.md` — acceptance: `libwifi.a`/`libbk_phy.a` 未解析符号按 OSAL/packet/协议栈/hw 分类并附库 SHA-256；manifest 模板含 upstream commit/patch hash/库 hash/许可证字段 (covers: S2; depends: T1)
