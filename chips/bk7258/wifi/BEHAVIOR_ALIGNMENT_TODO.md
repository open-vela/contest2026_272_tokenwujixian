# 行为等价审计 — 待改动清单

判据不是"语法是否一样"，而是**编译后实际执行的行为是否一致**。
基线 Armino `d2ded037`；我方 worktree `bk7258-wifi-rebuild` / `runtime-probe` 配置。

配套文档：`FOURWAY_DIFF_AUDIT.md`（120 个编译文件的源码 diff，72 个逐字节相同）。
本文件只记**要改的东西**，按对 Wi-Fi 运行行为的影响排序。

状态标记：`[待改]` 已确证需改 · `[待查]` 有嫌疑未坐实 · `[已闭]` 查证为无影响 · `[有意]` 故意偏离并已论证

---

## P0 — 可能改变 TX/ACK 行为

### P0-1 `[已实现，待板测]` Beken 逻辑优先级映射

权威配置 API 值为 core=2、kmsg=3、wpas=5，但这不是 FreeRTOS native
值。`cp/components/bk_rtos/freertos/v10/rtos_impl.h:17` 定义
`BK_PRIORITY_TO_NATIVE_PRIORITY(p) = RTOS_HIGHEST_PRIORITY - p`，而
`rtos_create_sram_thread()` 和 `rtos_thread_set_priority()` 都使用它。因此在
`configMAX_PRIORITIES=10` 下，权威 native 次序为 core=7 > kmsg=6 > wpas=4。

NuttX native 数值越大优先级越高，所以 OSAL 将 Beken API 0..9 先作 `9-p`，再
线性投影至 `SCHED_PRIORITY_MIN..SCHED_PRIORITY_MAX`；创建和动态调优使用同一函数。
这保留了权威的相对次序，而不是按 FreeRTOS 通用方向把 Beken API 值直传。

本项已有构建/链接证据；仍需板上在低串口日志量下复测 TX confirmation 与 M2，
不得将其单独视为握手失败的已证实根因。

### P0-2 `[降级→P2]` `CONFIG_GENERAL_DMA` 未定义 — 活代码不在 EAPOL 路径

```
权威  CONFIG_GENERAL_DMA 1        我方  未定义（求值为 0）
```

逐个核对 `#if` 上下文后的活/死判定：

| 位置 | 状态 |
|---|---|
| `rw_task.c:316` 调用 `rwm_tx_mpdu_renew` | **活**（`#else CONFIG_RWNX_SW_TXQ` 臂） |
| `rw_task.c:397` 调用 `rwm_tx_mpdu_renew` | **活**（同上） |
| `rw_task.c:293` / `:374` | 死（`#if !CONFIG_RWNX_SW_TXQ`，我方 =1） |
| `rw_msdu.c:38,122,131` | 死（整个文件不在编译集） |

关键：两个活调用点都在 **raw / mgmt TX** 函数内（`:316` 所在函数尾部调用
`rwnx_start_xmit_raw_ex()`，见 `rw_task.c:320-338`），**EAPOL 数据帧走
`rwnx_start_xmit()`，不经过 `rwm_tx_mpdu_renew()`**。

且我方 `dma_memcpy()` 本身就是 `memcpy()` 实现（`glue/misc_shim.c:99`），
定义该宏只会把 `os_memmove` 换成 `memcpy`，不会真启用 DMA 引擎。

**结论：对当前 `acked=0` 无解释力。** 保留为 P2 记录：若将来启用 raw TX，
需评估 CPU 拷贝对 MAC 可见缓冲区的 cache 维护（权威用真 DMA 引擎）。

### P0-3 `[降级→P2]` `CONFIG_WIFI_TX_RAW_ENABLE` 未定义 — 仅一个消费点，在 raw TX 内

```
权威  CONFIG_WIFI_TX_RAW_ENABLE 1     我方  未定义
```

全树扫描：**整个编译集只有一个消费点** `rw_task.c:326`，内容是

```c
#if CONFIG_WIFI_TX_RAW_ENABLE
    if (raw_tx->type == WIFI_TX_RAW_DATA)
        skb->args = skb->msdu_ptr;
    else
#endif
    { skb->args = msg->param; }
```

位于 raw TX 处理函数内（尾部 `rwnx_start_xmit_raw_ex()`）。未定义时恒走
`skb->args = msg->param`。EAPOL 不经此路径。

**结论：对当前故障无影响。** 若启用 raw data TX 功能则需补上。

---

## P1 — 真实差异，影响待定

### P1-1 `[待查]` `driver_beken.c` EAPOL RX 走的是**不同函数**

这是 EAPOL 路径上唯一的功能性源码差异（`FOURWAY_DIFF_AUDIT.md` §6.1）。
补上 `build_config.h` 的比对后，结论比先前更明确：

```
权威 build_config.h:  NEED_AP_MLME / HOSTAPD / CONFIG_NO_CTRL_IFACE 已定义
                      CONFIG_AP 在 :348，但位于 #if CONFIG_P2P 内
权威 sdkconfig.h:     无 CONFIG_P2P  →  #if CONFIG_P2P 为假  →  CONFIG_AP 实际未定义
我方 build_config.h:  显式 #undef CONFIG_AP / CONFIG_P2P / CONFIG_WPS / HOSTAPD / NEED_AP_MLME
```

于是 `driver_beken.c` 的 EAPOL RX 分支：

```
权威（#if CONFIG_P2P 假）    →  drv_event_eapol_rx(drv->hapd, sa, pos, left)
我方（#if defined(CONFIG_AP) 假）→  wpa_supplicant_rx_eapol(drv->wpa_s, sa, pos, left)
```

**调用的是两个不同函数**。我方路径经验证工作（日志 M1 五次全部正确接收解析），
权威路径在 STA 模式下传 `drv->hapd`——若该指针为空，权威要么另有 EAPOL RX 入口，
要么该函数在 STA 下另有语义。

**待查**：权威 STA 模式的 EAPOL RX 实际入口。若权威确实走 `drv->hapd` 且非空，
说明我们对 hostapd 上下文的取舍与权威不同，需重新评估；若权威 STA 也从不走到
这里，则我方改动是**必要的移植修正**，标记为 `[有意]` 并记录论证。

同一文件另有一处是**整个事件被删除**而非换分支，需单独记账：

```c
// 权威 :134
wpa_supplicant_event(drv->hapd, EVENT_RX_FROM_UNKNOWN, &event);
// 我方
if (drv->wpa_s == NULL) return;
```

其余 4 处（TX_STATUS 上报、RX_MGMT 上报、MIC failure 上报）都是删掉
`drv->hapd` 回落臂，STA 模式下 `drv->wpa_s` 非空时行为一致。

> 与 `acked=0` 的关系：ACK 由**接收方 AP 的 MAC 硬件**在 FCS 校验后生成，
> 我方任何 RX 逻辑都影响不到 AP 是否回 ACK。该补要补，但不要指望它是解。

### P1-2 `[待查]` `CONFIG_LWIP=0` vs 权威 1

有意偏离（换用 nuttx/net），但**尚未核对 vendored 代码有多少处据此分支**。
需列出全部 `#if CONFIG_LWIP` 消费点，逐个确认走 0 臂时行为可接受。

### P1-3 `[阻塞：上游运行域]` `CONFIG_PHY_MB=0` / `CPU_CNT=1` vs 权威 1 / 3

**成功原厂日志复核（2026-09-04）**：vendor-pure 实际打印 `cpu0 receive the cpu1
boot success event`，并在校准阶段使用 SARADC/tempd；同一次对照中，原厂 M1 后约
11 ms 完成 4-way，而 Vela M2 三次均 `ack=0`。这使 CPU1/IPC 与温度/ADC/OTP
闭包成为当前最强架构差异。详情见 FOURWAY_DIFF_AUDIT.md §6.3。

权威不是只把一个宏打开：CPU0 的 `bk_phy_server.c`、CPU1 的
`phy_client.c` 与两端 `mailbox_channel.c` / `mb_ipc.c` 共同组成 PHY IPC
闭包。`bk_phy_notify.c` 仅是这一闭包中的通知端，发送后必须取得 CPU1 的 ACK
（最多 5 ms）。本仓库当前 AP 是单核 OpenVela 运行域，且没有 Beken
`mailbox_channel` / `mb_ipc` provider；本地 `CONFIG_PHY_MB=0` 的 BK_OK
fallback 不能作为该协议的替代。

结论：不得单独定义 `CONFIG_PHY_MB=1` 或 `CONFIG_CPU_CNT=3`，也不得在 glue
中伪造 ACK。此项需要上游所有者导入并验证 CP server、CPU1 client、mailbox
driver 与 CPU1 生命周期的完整闭包；完成后须以板上 5 ms ACK 和 TX 复测为证据。

同样，`CONFIG_OTP_V1=1` 必须随权威 `otp_driver_v1_1.c`、`otp_hal.c`、
BK7258 OTP 寄存器层和生成的 `_otp.[ch]` map 一起引入。已保留 item ID 对齐，
但不在 team glue 内重写裁剪版 OTP driver。

> **原降级理由已作废（2026-09-04）。** 原文为「射频已工作（scan 通、RSSI 正常），
> 优先级不高」。scan 与 RSSI 全是**接收**方向证据，而 0904 三份日志确认坏的是
> **发送**方向：tid=0 单播数据帧首次投递成功率 0/12（`/tmp/0904-open-1.log` 4 帧、
> `/tmp/0904-open-2.log` 8 帧），信号为 `level=-28` / `rssi=-40`。接收侧证据不能
> 为发送侧故障免责。
>
> 这不只是本条的问题：**「射频已工作」这个推理模式在本文档中被多处复用**，凡以它
> 为降级依据的条目都需重新定级，逐条复核，不要只改这一条。

### P1-4 `[待查]` `CONFIG_MCU_PS=1` 权威有我方无

MCU 省电。缺失通常意味着不进低功耗，对连接是保守方向，但需确认它是否同时
gate 了唤醒/时钟路径。

### P1-5 `[待查]` `CONFIG_AP_HT_IE / AP_HE / AP_VHT / AP_VSIE` 权威全 1，我方全无

名字带 AP 但需确认是否只在 AP 模式生效。关联时我方 Assoc Request 已正确携带
HT/VHT/WMM IE（见 `FOURWAY_DIFF_AUDIT.md` 相关记录），故初判无影响，待确认。

### P1-6 `[待查]` `CONFIG_WPA_PSK_CACHE=1` 权威有我方无

`wpa_psk_cache.c` 在编译集内且引用 `CONFIG_TASK_WPAS_PRIO`。缺失意味着每次
重连都重算 PSK（慢，但不错）。需确认不会改变 PMK 来源语义。

### P1-7 `[待查]` 其余 OURS_MISSING

`CONFIG_WPA3`、
`CONFIG_SAE*`、`CONFIG_OWE`、`CONFIG_FAST_CONNECT_INFO_ENC_METHOD`、
`CONFIG_CUS_MAC_MASK=0x0`、`CONFIG_PSRAM_AS_SYS_MEMORY`、`CONFIG_CPU_CNT=3`、
`CONFIG_EASY_FLASH(_V4)`、`CONFIG_USE_MBEDTLS`、`CONFIG_OTP_V1`、
`CONFIG_GENERAL_DMA`（见 P0-2）、`CONFIG_FREERTOS`（我方不适用）。

逐个判定：是否有活代码消费点 → 有则判定行为差异 → 无则标 `[已闭]`。

其中 `WPA3` / `PMF` / `SAE*` / `USE_MBEDTLS` 已完成第一轮闭包判定：权威
WPA CMake 在 `CONFIG_USE_MBEDTLS=1` 时选择 `crypto_mbedtls.c`、`psa_mbedtls`，
并在 `CONFIG_WPA3=1` 时追加 dragonfly/SAE。当前工作树只有 WPA 源文件，
没有权威 PSA mbedTLS 组件和配置闭包；不得单开这些宏或删 internal crypto。
此项需要上游导入完整依赖并以 WPA3/PMF 硬件连接验证。

`CONFIG_SHELL_ASYNCLOG=1` 同样不能单开：权威输出走 shell task 的
`shell_cmd_ind_out()` 异步队列，本地无 provider。当前同步 `bk_printf_ext/raw`
造成的时序风险单列处理，不能用伪实现替代队列。

WPA_PSK_CACHE 的 WPA source、wlan_sta_gen_psk provider 和缓存对象已在 runtime
编译集，其 worker 完成时调用 rtos_delete_thread(NULL)。该 self-delete 已映射到
NuttX pthread_exit(NULL)；仍需单独启用和板测，因为缓存会把 PSK 请求改为异步
状态机并引入 PBKDF2 worker 时序。

CONFIG_FAST_CONNECT_INFO_ENC_METHOD=1 与 CONFIG_EASY_FLASH_V4 是一个持久化
闭包：权威读取/写入 fast_connect_id 使用 bk_get_env_enhance() /
bk_set_env_enhance()。本地未导入 EasyFlash V4，且现有 flash shim 明确拒绝写。
因此不得只定义 plain 编码宏，更不能用 team glue 伪造环境存储；需上游导入组件、
分区契约和可恢复写入验证后再处理。

---

## P2 — 已论证的有意偏离 / 无影响

### P2-1 `[已闭]` `CONFIG_MAC_SFRAME_SOFTWARE_RETRY` 不改变 `sk_buff` 布局

我先前把它当成头号疑点（认为 `rw_msdu.h:109` 的
`uint8_t sframe_sw_retry_cnt` 会改变 `sizeof(struct sk_buff)`，而
`rwnx_start_xmit()` 用 `skb + sizeof(struct sk_buff)` 定位 TX 描述符）。

DWARF 实测 + 手工推演都否定了它：

```
不带该宏  sizeof(struct sk_buff) = 56
带该宏    sizeof(struct sk_buff) = 56      ← uint64_t jiffies 强制 8 对齐，
DWARF 实测（我方）              = 56          多出的 1 字节落进尾部填充
```

其余消费点全部是死代码：`rwnx_tx.c:106`（`#else CONFIG_RWNX_SW_TXQ` 臂）、
`:183`（`#if 0`，权威同样 `#if 0`）、`rwnx_misc.c:254`（整个文件不在编译集）。
`rwnx_config.h:1185` 仍需扫一眼，但不涉及跨边界布局。

**结论：无需定义。** 保留记录以防再次误判。

### P2-2 `[已闭]` `CONFIG_WIFI_VNET_CONTROLLER=0` 对普通 STA TX 无行为差异

权威=1、我方=0，`rwnx_tx.c` 有两处活消费点：

```c
:641  if (vif_idx >= 0xF) { is_ctrl_if_data = true; ... }   // 我方 vif_idx=0，恒假
:844  if (is_ctrl_if_data) { host->flags |= TXU_CTRL_IF_DATA | TXU_CNTRL_PT_RERTY; }
      else                 { host->flags &= ~TXU_CTRL_IF_DATA; }
```

权威在 `is_ctrl_if_data=false` 时只做 `flags &= ~TXU_CTRL_IF_DATA`，而 `flags`
在 `:692` 已被 `memset` 清零，清一个本就是 0 的位是空操作。因此对普通 STA
数据帧（含 EAPOL），编不编这段结果一致。

> `TXU_CNTRL_PT_RERTY`（retry 控制位）只在 `is_ctrl_if_data=true` 时置位，
> 我方 `vif_idx=0` 永不进入，权威同理。不解释 `retry=0`。

### P2-3 `[有意]` `wpa_debug_show_keys` 1→0

用 upstream 自身机制压制 PMK/PTK/TK 明文打印（316 处 `wpa_hexdump_key`）。
文本诊断全保留。 `FOURWAY_DIFF_AUDIT.md` §6.2。

### P2-4 `[有意]` `CONFIG_MSDU_RESV_{HEAD,DESC}_LENGTH` 改为 Kconfig 间接

值一致（108 / 600），只是从字面量改为经 Kconfig 单一来源。

### P2-5 `[有意]` `CONFIG_WPA_AUTH_TIMEOUT` 10 vs 权威 2

我方更宽容，是保守方向。

### P2-6 `[待查]` `CONFIG_SHELL_ASYNCLOG=0` vs 权威 1

日志异步化。本身与 Wi-Fi 无关，但**会改变串口阻塞时序**，而时序已被怀疑与
`SM_CONNECT_CFM` 超时有关（见 P0-1 注）。与日志掩码一并评估。

### P2-7 `[已闭]` `build_config.h` 中 41 个 `AUTH_MISSING` 的假阳性来源

我第一轮只比了权威 `sdkconfig.h`，而 `CONFIG_SAE` / `CONFIG_OWE` / `CONFIG_WEP` /
`CONFIG_AP` / `CONFIG_IEEE80211W` 等是 wpa_supplicant 的 build 选项，两边都在
各自 `src/utils/build_config.h` 里。已定位该文件并完成 diff（见 P1-1），
这批需从 41 个里逐项剔除，剩余才是真差异。

---

## 审计进展

### A. `[已完成]` `build_config.h` 全量逐宏比对

```
我方  define 59 / undef 8        权威  define 63 / undef 1
差异 5 项，全部同源：
  CONFIG_AP             ours=未定义  auth=1   （权威 :348，在 #if CONFIG_P2P 内）
  CONFIG_WPS_AP         ours=未定义  auth=1   （权威 :349，同一守卫内）
  HOSTAPD               ours=未定义  auth=1   （权威 :32，顶层无条件）
  NEED_AP_MLME          ours=未定义  auth=1   （权威 :29，顶层无条件）
  CONFIG_NO_CTRL_IFACE  ours=未定义  auth=1   （权威 :35，顶层无条件）
```

**关键细化**：`CONFIG_AP` / `CONFIG_WPS_AP` 位于 `#if CONFIG_P2P` 内，而权威
`sdkconfig.h` 不定义 `CONFIG_P2P` → 这两个在权威实际构建中**也是未定义的**。
所以真差异只有 3 个顶层宏：`HOSTAPD`、`NEED_AP_MLME`、`CONFIG_NO_CTRL_IFACE`。

前两个开启 hostapd/AP MLME 提供者，我方 STA-only 不需要；`CONFIG_NO_CTRL_IFACE`
是**禁用** hostapd ctrl iface，我方未定义等于启用了一个我们不用的接口 —— 方向
无害但应对齐（少编一段代码）。

这批 5 项即 P2-7 所说的 41 个假阳性来源，已全部清账。

### B. `[已完成]` DWARF 跨边界布局全量核对 —— 0 冲突

两轮机械扫描，判据是**库自己认定的布局** vs **我方头文件编译出的布局**：

```
结构体 byte_size：libwifi.a 424 个 / nuttx.elf 1306 个，同名 379 个 → 不一致 0 项
字段 offset：     libwifi.a 431 个 / nuttx.elf 1286 个，同名 386 个 → 冲突   0 项
```

**这机械地关闭了整个「跨边界结构体布局」嫌疑类别**，包括我此前反复怀疑的
`struct pbuf`、`struct sk_buff`、`struct hostdesc`、`struct fhost_tx_desc_tag`、
`sta_info` 等。不需要再逐个假设。

复现：
```bash
readelf --debug-dump=info prebuilts/libwifi.a          # 库侧
readelf --debug-dump=info <build>/bk7258/nuttx.elf     # 我方侧
# 提取 DW_TAG_structure/union_type 的 DW_AT_name + byte_size + data_member_location 比对
```

### B2. `[已完成]` 跨边界符号契约面 —— 从 31 个文件收缩到 21 个符号

`nm -u libwifi.a` 得 899 个未定义符号，与**真正自研**目标文件（排除
`glue/vendor_sources/` wrapper）导出的 633 个符号求交：

```
=== 闭源库直接依赖我方自研实现的符号 = 21 个 ===
bk7258_wifi_adapter.c   g_wifi_funcs, g_wifi_vars
rtos_compat_shim.c      rtos_create_thread, rtos_delete_thread,
                        rtos_init_queue, rtos_deinit_queue,
                        rtos_push_to_queue, rtos_pop_from_queue,
                        rtos_disable_int, rtos_enable_int,
                        rtos_delay_milliseconds
platform_shim.c         coex_ictw_report_wifi_{connected,connecting,scan,sleep,traffic}_status
os_shim.c               os_malloc_debug, os_free_debug, os_memmove
system_shim.c           bk_printf_ext, bk_printf_raw
```

**这 21 个符号就是完整的自研嫌疑面。** 其余 shim 虽然存在，但闭源库不直接调用
（经 `g_wifi_funcs` 函数表间接调用的另算，见 B3）。

已核验语义正确的：

| 符号 | 实现 | 判定 |
|---|---|---|
| `rtos_disable_int` / `rtos_enable_int` | `up_irq_save()` / `up_irq_restore()`（`bk7258_wifi_osal.c:486,491`） | 语义正确 —— 返回旧状态 / 恢复旧状态，与 FreeRTOS 保存-恢复模型一致 |
| `rtos_push_to_queue` / `rtos_pop_from_queue` | `bk7258_wifi_osal_queue_send/recv`，`<0 → BK_FAIL` | 返回码映射正确 |
| `coex_ictw_report_wifi_*` | 蓝牙共存上报 | 无 BT，空实现合理 |
| `bk_printf_ext` / `bk_printf_raw` | 裸 `printf`（`system_shim.c:114,125`） | 见 §日志分级；不受 syslog 掩码影响 |

### B3 `[降级→P2]` `rtos_create_thread` — 库内仅 2 处调用，均不在连接路径

反汇编 `libwifi.a`（32 位 ARM objdump，93369 行）读出实参，**不需要硬件实测**：

```
签名: rtos_create_thread(thread*, uint8_t prio, name*, func*, stack, arg)
      → r0=thread  r1=prio  r2=name  r3=func  sp[0]=stack  sp[4]=arg

调用点 1  phy_cca_busy_test+0x52
    movs r1, #8          → prio = 8
    mov.w r7, #1024      → stack = 1024

调用点 2  bk_rlk_core_thread_init+0x2a
    movs r1, #3          → prio = 3
    mov.w r3, #4096      → stack = 4096
```

**全库只有这 2 处**。`phy_cca_busy_test` 是 CCA 忙测试，`bk_rlk_core_thread_init`
是 Beken 私有 RLK 功能 —— 都不在 STA 连接 / 4-way 路径上。

**同时修正我上一版写错的一点**：我曾断言"FreeRTOS 小数字=高优先级，需要反转映射"。
这是错的。FreeRTOS 与 NuttX **方向相同**，都是数值越大优先级越高
（FreeRTOS 范围 0..`configMAX_PRIORITIES`-1，权威 `FreeRTOSConfig.h:140` 为 10）。
所以不需要反转，只存在**量程压缩**问题：库传的 8/10（FreeRTOS 高优先级）直传成
NuttX 的 8/255（近最低）。

由于这 2 处都不在连接路径，降级为 P2；若将来启用 RLK 或 CCA 测试需补量程换算。

### B4 `[已完成]` 函数表 283 槽行为比对（比函数体，不比函数名）

方法：`third_party/.../bk_wifi_adapter.c` 与权威**逐字节相同**，所以权威的每个
wrapper 实现就在树里。提取双方函数体、剥注释、归一化空白、比**调用集合**，
而不是比赋值的函数名（比名字得出的 176 项"差异"绝大多数是
`bk7258_wifi_os_malloc_cb` vs `os_malloc_wrapper` 这类无信息量的命名差异）。

```
SAME_CALLS      54     调用集合一致
CALLS_DIFFER    86     调用集合不同（逐项判定见下）
EMPTINESS_DIFF   5     一方空实现
BOTH_EMPTY       5     双方都空
NO_BODY        124     函数体不在本文件（extern / 库内提供）
```

86 项 `CALLS_DIFFER` 按行为分类：

**(a) 转发目标改名，行为等价 —— 47 项**
`os_memcpy`/`os_strlen`/`os_strcmp` 等全部 C 库转发：权威 `os_memcpy_wrapper` →
`os_memcpy`，我方 → `memcpy`；`rtos_*` 系列：权威 → `rtos_enter_critical`，
我方 → `bk7258_wifi_osal_enter_critical` → `up_irq_save()`。语义相同。

**(b) 我方是 stub，只打日志 —— 8 项**（需逐个确认库是否依赖其副作用）
```
_get_net_info / _save_net_info / _lookup_ipaddr / _inet_ntoa
_net_wlan_add_netif / _net_wlan_remove_netif / _sta_ip_down / _uap_ip_*
        ours = hp_stub_log(...)      auth = 真实 lwIP 网络操作
```
这批是 `CONFIG_LWIP=0` 的必然结果（网络栈换成 nuttx/net），方向正确。
但 `_net_wlan_add_netif` 在权威是**关联成功后建立 netif**，我方 stub 掉之后
netif 由谁建、何时建，需确认与 4-way 的时序关系。

**(c) `[待改]` 我方 stub 掉了中断使能 —— 4 项**
```
_sys_drv_int_enable / _sys_drv_int_disable
_sys_drv_int_group2_enable / _sys_drv_int_group2_disable
        ours = { return; }           auth = sys_drv_int_*_wrapper → 真实寄存器操作
```
**这四个是中断使能/屏蔽**，而中断 wiring 是历史上两次有效推进之一。库调用它们
屏蔽/恢复 MAC 中断时我方什么都不做，需确认库是否依赖其副作用（例如在临界区内
假定 MAC 中断已被屏蔽）。

**(d) `[待查]` `_bk_wifi_interrupt_init` 实现不同**
```
auth: sys_drv_enable_mac_gen_int() + sys_drv_enable_mac_prot_int() + sys_drv_enable_hsu_int()
ours: hp_bk_wifi_interrupt_init()   ← 需确认它是否等价展开这三个
```

**(e) `[待查]` `_wifi_mac_phy_power_on` 实现不同**
```
auth: bk_pm_clock_ctrl() + bk_pm_module_vote_power_ctrl()
ours: 含 for 循环 + syslog，调用集合完全不同
```

**(f) 已论证的有意偏离 —— 剩余项**
`_send_udp_bc_pkt`（airkiss 不在 profile）、`_shell_assert_out`（改用 bk_printf）、
`_sys_ll_*_pwd_ofdm`（改用 getreg32/putreg32 + spinlock）、
`_sys_drv_modem_clk_ctrl`（改用 modifyreg32）、`_wifi_notify_state_to_bt`（无 BT）。

**5 项 `[待查]` 我方空实现**：`_dbg_enable_debug_gpio`、`_mcu_ps_machw_init`、
`_mcu_ps_machw_reset`、`_power_save_forbid_trace`、`_tx_verify_test_call_back`
—— 需核对权威 wrapper 经配置求值后是否也为空。

**槽位覆盖**：仅权威有 1 个（`_os_vsnprintf`，我方有 `hp_os_vsnprintf` extern，
是正则漏检）；仅我方有 3 个（`_delay`、`_rc_drv_set_rf_en`、`_sta_ip_start`）；
`_tpc_*`×3 / `_wapi_wpi_*`×2 我方填了而权威是 `NULL`，但反汇编显示库从不引用。

数值槽已核对：`_low_power_delay_time_hardware` 我方 500 = 权威宏 `(500)`；
`_sys_sys_debug_config1_addr` 我方 `0x44010000 + (0x39 << 2)` = 权威
`SOC_SYS_REG_BASE + (0x39 << 2)`。

`_rtos_disable_int` 我方填 `bk7258_wifi_enter_critical_cb` 看似张冠李戴，
但权威 `rtos_disable_int_wrapper()` 的函数体就是 `return rtos_enter_critical();`
（`bk_wifi_adapter.c:829`）—— **权威自己也是同一别名**，行为一致。

---

## P0-4 `[待改]` D-cache 运行时状态相反：权威**开**、我方**关**

### 定案证据（源码，非推断）

```
权威   build/bk7258/iperf/bk7258/config/sdkconfig.h:100   #define CONFIG_DCACHE 1
       projects/wifi/bridge/cp/config/bk7258/config:396   CONFIG_DCACHE=y
       → D-cache 运行时开启

我方   chips/bk7258/bk7258_start.c:55-58
         if ((getreg32(NVIC_CFGCON) & NVIC_CFGCON_DC) != 0)
             up_disable_dcache();          ← 主动关掉 Bootloader 继承的 D-cache
       :64  up_enable_icache();            ← 只开 I-cache
       全树无 up_enable_dcache 调用点，ELF 符号表中该函数不存在
       → D-cache 运行时关闭
```

### 两次误判记录（避免第三次）

1. 第一次我 grep `CONFIG_CACHE_ENABLE`，得出"权威 cache 关、我方开"，并据此写了
   一整套"描述符停在脏 cache 行、MAC 读到旧值"的根因推理。**宏名错了** ——
   `CONFIG_CACHE_ENABLE` 在权威全树无人设置；真正生效的是 `CONFIG_DCACHE`。
2. 第二次我用 `nm nuttx.elf` 没找到 `up_enable_dcache`，判定"我方也是关的、两侧
   一致"。这个结论**方向对了但理由不完整** —— 真正的证据是 `bk7258_start.c:55`
   显式调用 `up_disable_dcache()`，而非"符号不存在"（符号可能被内联或 gc）。

真实差异与我最初写的**正好相反**：权威开、我方关。

### 行为影响

我方关 cache 是**保守方向**：不存在 CPU 写脏行、MAC 读旧值的一致性风险，所以
它**不可能是** `done=1 acked=0` 的成因（这条排除成立）。但存在两点行为不等价：

1. **性能**：全部内存走无缓存访问，CPU 侧吞吐远低于权威。
2. **`_flush_all_dcache` 槽的语义倒置**。权威 wrapper 因
   `#if CONFIG_CACHE_ENABLE`（未定义）求值为**空函数**；我方填了真实的
   `up_clean_dcache_all() + up_invalidate_dcache_all()`（`bk7258_wifi_adapter.c:755`）。
   即：权威开着 cache 却不刷，我方关着 cache 反而刷。两侧都不出错，但都不是
   对方的语义。反汇编确认 `libwifi.a` **从不调用**该槽（0 引用），所以当前无实害。

### 改（对齐权威）

按 `CONFIG_DCACHE=1` 对齐，即让 `bk7258_start.c` 使能 D-cache 而非禁用。

**但这条改动有一个必须先解决的依赖**：`bk7258_start.c:52-54` 的注释写明
「RPMsg transport deliberately uses uncached shared SRAM」—— 现在靠"全局关
cache"来保证 RPMSG_SHM / SWAP 区的无缓存访问。开 cache 后必须改为按区域控制
（MPU region 或显式 clean/invalidate），否则会破坏 CP↔AP mailbox。

而当前 `.config` 里 `CONFIG_ARM_MPU` / `CONFIG_ARMV8M_MPU` **均未设置**，
板级 `bk7258-devkit/src/` 也无任何 MPU/noncache 配置。所以这条改动的前置工作是
先建立非缓存区机制。

**优先级判断**：不是 4-way 的成因，但属于你要求的「config 与行为保持一致」，
且开 cache 后 TX 描述符（嵌在 pbuf headroom 里，`p + sizeof(struct pbuf)`）
将首次进入缓存一致性风险区 —— 必须与 `_flush_all_dcache` 语义一并处理，
不能只翻一个开关。建议在 4-way 打通之后再做，避免同时引入两个变量。

### 存档：基于错误前提的原始推理（不成立）

这是把「比函数名」换成「比函数体求值结果」之后立刻暴露出来的，**不是配置表
比对能发现的**（`CONFIG_CACHE_ENABLE` 根本不在权威 `sdkconfig.h` 里）。

```c
// 权威 wrapper（bk_wifi_adapter.c:748-756）
static void flush_all_dcache_wrapper(void) {
#if CONFIG_CACHE_ENABLE          // 权威 config: "# CONFIG_CACHE_ENABLE is not set"
#ifndef CONFIG_SOC_SMP
    flush_all_dcache();
#endif
#endif
}                                 // ← 求值后函数体为空
```

```
权威 projects/*/cp/config/bk7258/config  →  # CONFIG_CACHE_ENABLE is not set   （8/8 个项目全部）
我方 runtime-probe/.config               →  CONFIG_ARCH_DCACHE=y
                                            CONFIG_ARMV8M_DCACHE=y
```

**权威 CP 整个 D-cache 是关的，我方开着。** 这意味着：

1. `libwifi.a` 是在「无 cache」前提下编译的 → **库内部不做任何 cache 维护**
2. 反汇编确认库**从不调用** `_flush_all_dcache` 槽（`grep flush_all_dcache
   libwifi.dis` = 0）→ 我方那个槽填得再对也不会被调用
3. 我方全树**只有** `bk7258_wifi_adapter.c:757-758` 两行 cache 维护，且在
   TX 路径上从未被执行
4. 未配置任何非缓存区：`CONFIG_ARM_MPU` / `ARMV8M_MPU` 均未设，板级
   `bk7258-devkit/src/` 无 MPU/noncache 设置 → **全部内存可缓存**
5. pbuf 由 `kmm_zalloc()` 分配（`pbuf_shim.c:89`）—— 普通可缓存堆

> **本小节的前提已被推翻（2026-09-04），保留全文仅作排查历史。** 两处失效：
>
> 1. **「零方差」不再成立。** `/tmp/0904-open-2.log` 中同一 TX 路径上，tid=0 单播
>    数据帧 8 帧共 42 次投递，其中 3 帧最终 `ack=1`。既然同一路径能成功，就不存在
>    「确定性缓存破坏描述符」这种全灭现象。
> 2. **D-cache 已被独立排除。** 见 `BEHAVIOR_ALIGNMENT_AUDIT.md` 6.8 第 1 条：目标
>    CP 启动显式调用 `up_disable_dcache`，Wi-Fi SRAM `0x2808xxxx` 实际为
>    non-cacheable，与权威（全局开 cache 但 MPU 配 non-cacheable）等效。
>
> 注意本文档此处与 AUDIT 6.8 互相矛盾且 6.8 更晚、结论更强：**以 6.8 为准**。
> 下面这套「CPU 视角 vs MAC 视角」的推理不要再作为在办嫌疑引用。

**为什么这能解释 `done=1 acked=0 retry=0` 的 15/15 零方差**：

TX 描述符 **不在** 单独的 DMA 缓冲里，它就嵌在 pbuf 的 headroom 中 ——
`rwnx_start_xmit()` 把 `sk_buff` 放在 `p + sizeof(struct pbuf)`，
`fhost_tx_desc_tag`（含 `hostdesc`）紧随其后。所以：

```
CPU 写 host->tid / staid / packet_addr / eth_dest_addr   →  停在 D-cache 脏行
MAC 硬件按物理地址读描述符                                →  读到旧值 / memset 的零
```

我方两个探针打印的是 **CPU 视角**（经过 cache），所以显示
`arm=qos tid=7 staid=0 ethertype=8e88 len=121` 全部正确；而 MAC 读到的可能是
另一份数据。**这正好解释了「host 侧字段全对、硬件行为却不对」这个此前无法
调和的矛盾**，也解释了零方差（确定性的缓存行为，不是空口现象）。

**待验证（不需硬件的部分）**：
- 确认 `CONFIG_ARCH_DCACHE=y` 下 ARMv8-M D-cache 在启动时是否真的 enable
  （NuttX 有 `up_enable_dcache()` 调用点需核实）
- 确认 BK7258 的 MAC 是否与 CPU 共享缓存一致性域（若硬件本身一致性，则本条作废）

**候选改法（互斥，需先定性）**：
- **A** 关掉 D-cache 对齐权威：`CONFIG_ARCH_DCACHE=n`。最贴近权威，代价是整体
  性能。作为**判决实验**最干净 —— 一次构建即可定性。
- **B** 保留 cache，把 pbuf/描述符改到非缓存区（MPU region 或 `kmm_memalign`
  + 手动 clean）。偏离权威更多，但保住性能。
- **C** 在 `fhost_txdesc_init()` 之前对描述符区做 `up_clean_dcache(addr, len)`。
  最小侵入，但需覆盖所有 DMA 交接点，易漏。

建议先做 A 定性：**如果关掉 D-cache 后 M2 被 ACK，本节即为根因**。

### C. `[待做]` 编译期真值验证
本次比对读的是**头文件字面值**。更强的做法是对每个 TU 跑 `gcc -E -dM` 拿预处理器
真值，排除 `#include` 顺序与 `#undef` 造成的偏差（A 项 `CONFIG_AP` 就是这类偏差：
字面上权威有定义，实际因外层 `#if` 为假而未定义）。构建系统未生成
`compile_commands.json`，需先补。

### D. `[待查]` 运行时判决实验（需硬件）
1. **AC_VO → AC_BE**：`rwnx_tx.c:346` 对 `ETH_P_PAE` 返回 7（→AC_VO）。
   `MM_SET_EDCA_REQ` 在**两侧源码中均只出现于 `lmac_msg.h` 枚举、无可见发送点**，
   说明 EDCA 由闭源库内部下发，源码比对无法推进。临时改返回 0（AC_BE）可判定：
   被 ACK 则锁定 EDCA 方向，仍不 ACK 则排除整个队列方向。偏离权威，仅作诊断。
2. **开放 AP 正控**：目前数据通路的全部 15 次使用都是失败的 EAPOL，无成功样本。

---

## 改动纪律

1. 一次只改一个变量，改完记录 `app.bin` sha 与行为变化
2. 偏离权威的改动必须在源码注释里写清**为什么**，并在本文件标 `[有意]`
3. `[待查]` 项在坐实前不要动代码
4. 每条改动完成后在本文件就地更新状态，不另起文档
