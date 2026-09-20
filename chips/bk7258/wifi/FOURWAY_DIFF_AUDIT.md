# 4-way handshake 路径 C 文件全覆盖 diff 审计

基线：Armino `d2ded037798530175e5dc5cde6fa1878f5d5ef35`（`/home/czp/armino/bk_avdk_smp`）
我方：worktree `bk7258-wifi-rebuild`，`chips/bk7258/wifi/`
镜像：`runtime-probe` 配置，`app.bin` `6e23a121f97be325`

---

## 1. 结论

**4-way handshake 涉及的全部 vendored C 文件，与权威逐字节相同。**

EAPOL 从 wpa_supplicant 组帧到硬件描述符提交，整条链上的文件无一处功能性差异：

```
wpas_glue.c  →  l2_packet_none.c  →  fake_socket.c  →  hostapd_intf.c
             →  rw_task.c  →  rwnx_tx.c  →  rwnx_txq.c  →  skbuff.c
```

密码学侧（`rsn_supp/wpa.c` 4-way 状态机本体、`wpa_common.c`、`sha1-prf.c`、`aes-omac1.c`、`wpa_ie.c`、`pmksa_cache.c`）同样全部相同。

因此故障成因**不在 vendored 源码**。剩余嫌疑范围收缩为两处：31 个我方自研 shim（第 5 节，无权威对照物，diff 无法覆盖）与闭源 `libwifi.a`。

> **待解释对象已改写（2026-09-04）。** 本节原把 `done=1 acked=0 retry=0` 当作故障
> 签名。该组合**本身是正常形态**，理由见 `BEHAVIOR_ALIGNMENT_AUDIT.md` 6.8 的更正
> 说明（同一路径既产出该组合，也产出 `ack=1`）。
>
> 上面「vendored 源码逐字节相同 → 成因不在 vendored 源码」这个 diff 论证本身仍然
> 成立，嫌疑范围收缩的结论保留；改变的只是**要解释什么**：
>
> 1. tid=0 单播数据帧首次投递成功率 0/12（`level=-28` / `rssi=-40`）；
> 2. M2（tid=7）每帧恰好一次投递，从不进入重传，而 tid=0 可达 8 次。
>
> 注意第 2 条把嫌疑重新指向了**优先级/队列相关的配置**，而这类配置多由我方 shim 与
> CONFIG 决定 —— 与本节结论方向一致。

---

## 2. 方法与覆盖范围

文件清单取自**实际编译产物**（`build/runtime-probe` 下的 `*.c.o`），不是源码目录 —— 后者含大量被 `#if` 排除的死代码，比对它们只会产生噪声。

一个必须注意的陷阱：对象文件有**两套命名并存**。

| 命名 | 形态 | 数量 |
|---|---|---|
| 路径式 | `.../vendor/beken/chips/bk7258/wifi/glue/vendor_sources/rwnx_tx.c.o` | 60 |
| 扁平式 | `third_party_beken_armino_wpa_supplicant_src_rsn_supp_wpa.c.o` | 60 |

只按路径 grep 会漏掉 `l2_packet_none.c`、`rsn_supp/wpa.c` 等 4-way 核心文件；只按扁平名 grep 会漏掉 `rwnx_tx.c`。

另一层间接：`glue/vendor_sources/*.c` 是 wrapper，每个文件仅一行 `#include "../../third_party/..."` 指向真实源码。比对 wrapper 本身会得出「全部相同」的假结论。

两层解开后共 **120 个实际编译的 C 文件**，全部纳入比对。

路径映射规则：

```
third_party/beken_armino/glue/bk_wifi/src/  →  cp/components/bk_wifi/src/
third_party/beken_armino/glue/bk_phy/src/   →  cp/components/bk_phy/src/
third_party/beken_armino/wpa_supplicant/    →  cp/components/wpa_supplicant-2.10/
```

判定分级：`IDENTICAL`（字节相同）→ `WS_EOL_ONLY`（仅空白/行尾）→ `INCLUDE_COMMENT_ONLY`（仅 include 与注释）→ `SMALL`（≤20 变更行）→ `LARGE`。

---

## 3. 总览

| 判定 | 数量 | 含义 |
|---|---:|---|
| IDENTICAL | 72 | 与权威逐字节相同 |
| OURS_ONLY | 31 | 我方自研，权威无对照物 |
| LARGE | 9 | 实质差异 |
| SMALL | 5 | 小幅差异 |
| INCLUDE_COMMENT_ONLY | 3 | 仅头文件路径与注释 |

vendored 文件 89 个，其中 72 个（81%）逐字节相同。

---

## 4. 4-way 关键路径逐文件判定

### 4.1 EAPOL 发送链（M2 的实际通路）

| 文件 | 判定 |
|---|---|
| `wpa_supplicant/wpas_glue.c` | IDENTICAL |
| `src/l2_packet/l2_packet_none.c` | IDENTICAL |
| `bk_patch/fake_socket.c` | INCLUDE_COMMENT_ONLY |
| `bk_patch/sk_intf.c` | INCLUDE_COMMENT_ONLY |
| `glue/bk_wifi/src/hostapd_intf.c` | IDENTICAL |
| `glue/bk_wifi/src/rw_task.c` | LARGE（见 6.1，与 EAPOL 无关） |
| `glue/bk_wifi/src/rwnx_tx.c` | **剥离我方探针后 IDENTICAL** |
| `glue/bk_wifi/src/rwnx_txq.c` | IDENTICAL |
| `glue/bk_wifi/src/skbuff.c` | IDENTICAL |

`rwnx_tx.c` 原始 diff 为 +143/-1，剥离 138 行我方 bring-up 探针后仅剩 2 处空行：

```
@@ -677,4 +677,5 @@   host->flags |= TXU_CNTRL_IS_SPECIAL_FRAME 之后多一空行
@@ -1103,3 +1104,2 @@  文件末尾少一空行
```

函数体、`rwnx_select_txq()` 的分支条件、`host->packet_addr[0]` 的计算、`host->tid = skb->priority` 全部与权威一致。

### 4.2 密钥协商与密码学

| 文件 | 判定 |
|---|---|
| `src/rsn_supp/wpa.c`（4-way 状态机本体） | IDENTICAL |
| `src/rsn_supp/wpa_ie.c` | IDENTICAL |
| `src/rsn_supp/pmksa_cache.c` | IDENTICAL |
| `src/common/wpa_common.c` | IDENTICAL |
| `src/crypto/sha1-prf.c`（PTK 导出） | IDENTICAL |
| `src/crypto/aes-omac1.c`（MIC） | IDENTICAL |
| `src/crypto/` 其余 30 个 | IDENTICAL |

日志观测到的 PTK 导出成功、`Derived Key MIC` 正确生成，与源码相同这一事实互为印证。

---

## 5. OURS_ONLY：31 个无对照物的文件（剩余嫌疑范围）

这些是替换 Armino 平台层的 NuttX 自研实现，权威树无对应文件，**diff 无法覆盖**。TX 路径相关的按嫌疑度排序：

| 文件 | 在 TX 路径上的作用 |
|---|---|
| `glue/pbuf_shim.c` | pbuf 容器分配，DMA 源地址由其 `payload` 派生 |
| `hal_port/funcs_fill.c` | `g_wifi_funcs`/`g_wifi_vars` 函数表填充，闭源库全部回调经此 |
| `glue/platform_shim.c` | 时钟/电源/PHY 唤醒 |
| `glue/os_shim.c` | 内存与临界区 |
| `glue/netif_shim.c` | netif 层 |
| `glue/event_shim.c` | 事件分发 |
| `glue/wpa_queue_shim.c` | wpa 任务队列 |
| `hal_port/hal_port_mac.c` | MAC 硬件端口 |
| `hal_port/hal_port_sys_int_power.c` | 中断与电源 |

其余：`bk7258_wifi_{adapter,hw,lower,osal,packet}.c`、`glue/{analog,feature,flash,misc,net_param,phy_calibration,rtos_compat,rtos_ext,system,hw_driver}_shim.c`、`glue/{bk7258_saradc,bk_workqueue_nuttx,scan_diagnostics}.c`、`hal_port/{hal_port_dvfs,hal_port_lpdoze,vnd_cal}.c`、`board/bk7258/vnd_cal/vnd_cal.c`。

对这批文件，源码比对方法论上不适用，只能靠运行时探针或 DWARF 布局比对。历史上有效的两次推进（pbuf 容器对齐、中断 wiring）都出自这一层。

---

## 6. 有差异的 17 个文件逐个说明

### 6.1 LARGE（9）

**`glue/bk_wifi/src/rw_task.c`** +113/-3
补入 `BMSG_HARDWARE_IOCTL_TYPE` 与配套 case（从本文件 `#if CONFIG_PM_V2` 姊妹分支逐字移入）。权威默认 `CONFIG_PM_V2=y` 掩盖了非 PM_V2 变体缺少该 case 的缺陷：`ME_GET_BSS_INFO_REQ` 落入 `default` 被静默丢弃 → `wpa_driver_get_bssid()` 失败 → `wpa_supplicant_event_assoc()` 直接 deauth 并 return，状态永停 ASSOCIATING。**这是已修复的历史缺陷**，修好后 4-way 才开始进行。另有 `#ifdef` → `#if` 对齐。

**`glue/bk_wifi/src/rw_msg_tx.c`** +51/-7
两类改动：一是 `rw_msg_send` 超时的 `BK_ASSERT(0)` 降级为 `RWNX_LOGE` + 返回 `RWNX_ERR_TIMEOUT`（bring-up 期硬 panic 会把 ke 任务卡在请求中途；返回值路径本就被上层处理）；二是 `MM_RESET`/`MM_START` 前后加 `bk7258_hwprobe_latch()` 硬件状态锁存。

**`src/drivers/driver_beken.c`** +35/-8
EAPOL **接收**方向的分支重构：无 `CONFIG_AP` 时不再回落到 `drv_event_eapol_rx(drv->hapd, ...)`（我方无 hostapd 上下文），改为 `drv->wpa_s == NULL` 早退 + `WPA_LOGW("Dropping EAPOL without STA context")`。原 `#if CONFIG_P2P` 守卫改为 `#if defined(CONFIG_AP)`。
注：这是 EAPOL 路径上唯一的功能性差异，但方向是 RX。日志中 M1 五次全部正确接收解析，RX 通路已验证工作，不解释 M2 发送失败。

**`glue/bk_phy/src/bk_rf_adapter.c`** +36/-2
`bk_rf_adapter_init()` 返回值 `void` → `int`，并显式声明 `extern const rf_control_funcs_t *g_rf_funcs_t`，逐槽校验闭源库实际写入的表，而不只校验 C 初始化式。

**`wpa_supplicant/ctrl_iface.c`** +26/-6
`site_survey_cc` 相关代码补 `#if CONFIG_WIFI_SCAN_COUNTRY_CODE` 守卫，`#if CONFIG_AP` 守卫 AP 专属分支。我方未开这些选项，权威默认开启。

**`glue/bk_wifi/src/rwnx_td.c`** +13/-15
`GLOBAL_INT_DECLARATION/DISABLE/RESTORE` 三宏替换为 NuttX `enter_critical_section()`/`leave_critical_section()`；`uint16` → `uint16_t`；恢复被误删的 `rwnx_defs.h`（曾静默带走 `RWNX_LOGE`）。

**`glue/bk_wifi/src/rw_ieee80211.c`** +26/-1
`sta_info_tab` 池显式清零。闭源库 `me_strategy_mem_init()` 用 libc `malloc()` 分配该池，而 `sta_mgmt_entry_init()` 首次使用时把 `entry+0x210` 当作空 `co_list`，故必须在 `mm_init()`/`sta_mgmt_init()` 前置零。范围由 ABI 推导：`g_wifi_mac_sta_max_num` 项 × `0x238` 字节 + 2 个 BCMC 项。

**`glue/bk_wifi/src/rw_msg_rx.c`** +16/-5
插入 `bk7258_scan_diag_record()` 埋点（LMAC 结果上报、表满、国家码丢弃、重复、分配失败、插入、完成），另有缩进规整。纯诊断。

**`glue/bk_wifi/src/rwnx_rx.c`** +17/-2（分级为 SMALL，与上条同源）
同类 scan 诊断埋点 + `#ifdef` → `#if`。

### 6.2 SMALL（5）

**`glue/bk_phy/src/bk_phy_adapter.c`** +17/-3
`CONFIG_SOC_BK7258` 加入 BK7236 家族守卫（BK7258 属该家族），`_aon_pmu_hal_get_chipid` 绑定随之移入。

**`wpa_supplicant/main_supplicant.c`** +16/-4
`params.wpa_debug_show_keys` 由 1 改为 **0**。`bk_prelude.h` 开启 `CONFIG_WPA_LOG` 后，316 处 `wpa_hexdump_key()` 会把 PMK/PTK/TK 原始字节打到串口；置 0 走 upstream 自身的 `show==0` 分支输出 `[REMOVED]`（`wpa_debug.c:113-127`）。用 upstream 机制压制凭据，文本诊断全保留。另 `#include "signal.h"` → `<signal.h>`。

**`glue/bk_wifi/src/wifi_v2.c`** +9/-8
include 路径由 `../../wpa_supplicant-2.10/...` 改为 include-path 相对形式；`g_wifi_funcs`/`g_wifi_vars` 的 `__attribute__((section(".dtcm_sec_data ")))` 定义改为 `extern` 声明（定义移至 `hal_port/funcs_fill.c`）；`#ifdef` → `#if`。

**`wpa_supplicant/notify.c`** +1/-1
`#ifdef CONFIG_WIFI_VNET_CONTROLLER` → `#if`。

### 6.3 INCLUDE_COMMENT_ONLY（3）

**`bk_patch/fake_socket.c`**、**`bk_patch/sk_intf.c`**
`#include "fake_socket.h"` / `"sk_intf.h"` → `<wpa_compat/...>`，末尾空行。函数体不变 —— `fsocket_send()` 的拷贝语义与权威一致。

**`src/utils/os_none.c`** +4/-0
补 `#include <time.h>`。裁剪版 `includes.h` 未引入，原先依赖 GCC 10.3 newlib 的传递包含；GCC 13 传递更少，会报 `unknown type name 'time_t'`。

---

## 7. 对当前故障的意义

已排除的方向（本次审计新增）：

| 方向 | 依据 |
|---|---|
| EAPOL 组帧 / 封装 | `l2_packet_none.c`、`wpas_glue.c`、`fake_socket.c` 函数体全同 |
| 4-way 状态机 / MIC / PTK | `rsn_supp/wpa.c` 及全部 crypto 文件字节相同 |
| TX 队列选择 / 描述符填充 | `rwnx_tx.c` 剥离探针后仅空行差异 |
| skb / txq 管理 | `skbuff.c`、`rwnx_txq.c` 字节相同 |

`selq`/`desc` 两组探针此前已证明交给固件的描述符字段全部正确（`arm=qos`、`capa=00000007`、`tid=7`、`staid=0`、`ethertype` 双向镜像、`len=121=135−14`）。本次审计补上了源码侧的机械证明：**host 侧 vendored 代码没有可指摘之处**。

下一步只有两条路：

1. **运行时实验**。`classify8021d()` 对 `ETH_P_PAE` 返回 TID7 → AC_VO（`rwnx_tx.c:346`）。AC_VO 的 EDCA 参数经 `MM_SET_EDCA_REQ` 下发，而该消息在两侧源码中**均只出现于 `lmac_msg.h` 枚举、无可见发送点**，说明由闭源库内部下发，源码比对无法推进。把返回值临时改为 0（AC_BE）可判定：被 ACK 则锁定 EDCA 方向，仍不 ACK 则排除整个队列方向。偏离权威，仅作诊断，验证后回滚。
2. **DWARF 布局比对**。对第 5 节的 31 个 shim 做跨边界结构体 `byte_size` 全量核对（`libwifi.a` 带 131802 个 DW_TAG），比继续逐个假设可靠。

---

## 附：复现命令

```bash
# 1. 取实际编译的 C 文件清单（两套命名都要）
find cmake_out/.../build/runtime-probe -name '*.c.o' | sed 's|\.o$||' > /tmp/all-obj.txt

# 2. 解开 glue/vendor_sources/ wrapper 间接层
grep -oE '#include[[:space:]]+"[^"]*\.c"' <wrapper>

# 3. 逐文件比对
git -C /home/czp/armino/bk_avdk_smp show d2ded037:<authority-path>
```

审计脚本产出：`/tmp/audit.tsv`（120 行判定表）、`/tmp/diffs/*.diff`（17 个差异全文）。

## 6.3 原厂 vendor-pure 成功日志对照（2026-09-04）

对照输入已固化为：

- authority：`/home/czp/openvela_contest/contest2026_272_tokenwujixian/0904-armino-verbose.log`，SHA-256 `a8c4d0bf3a6ed3a0c72c2882ffc2769ee6485ea7c2dc4018abca966e1184a5c1`，SDK 分支 `vendor-pure`；
- target：`/tmp/0904-vela-1.log`，SHA-256 `0d3e2260322d454cec4be3c57961675ee360a1367f788711083e6af47506949c`。

两者均连接同一 BSSID `6e:26:dd:7f:cc:cc`，并均有相同的
`load polar tab magic code error 0xffffffff`。原厂仍从 M1 后约 11 ms 内完成
`4WAY_HANDSHAKE -> GROUP_HANDSHAKE -> COMPLETED`，随后取得 DHCP ACK。因此 polar
table 报错不是 Vela M2 未获 ACK 的充分根因，不能继续作为主假设。

Vela 已经完成扫描、认证、关联并收到 M1；M2 三次的 PHY 确认全为
`ack=0 done=1 tid=7 q=3`，之后才是本地发起 deauth。`WRONG_KEY` 因而是 M2
空口未确认后的本地结果，不是 AP 在 MIC/密码校验后的拒绝。

最早且与 TX 模拟/校准直接相关的已观测分叉为：

| 阶段 | vendor-pure（成功） | Vela（失败） | 结论 |
|---|---|---|---|
| 温度输入 | `temp in otp is:563`，`temp_code=29,dmodldosel=4,Dgmdc=7` | `temp_code=-19,dmodldosel=2,Dgmdc=0` | 温度/ADC/OTP 相关校准输入不等价 |
| 温补服务 | SARADC acquire/release，创建 `tempd` | 无 authority tempd provider；本地 `bk_feature_temp_detect_enable=0` | 温补路径未闭合 |
| 多核运行域 | `cpu0 receive the cpu1 boot success event`，有 IPC/CIF/PHY server | `CONFIG_CPU_CNT` 缺省为 1、`CONFIG_PHY_MB=0` | 原厂 CPU1/IPC 运行域缺失 |
| 温度功率修正 | `shift_b=0,shift_g=0` | `shift_b=-4,shift_g=-7` | 同一 54 Mbps 校准率得到不同 TX 功率修正 |

这建立了优先级，不等于证明某一个字段单独导致 M2 失败：真实 OTP、SARADC、tempd、CPU1
mailbox/PHY provider 是同一上游闭包的组成部分，不能通过在 team glue 里填常量或伪造 ACK 来替代。
