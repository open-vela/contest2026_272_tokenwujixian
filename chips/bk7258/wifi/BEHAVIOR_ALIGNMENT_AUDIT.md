# BK7258 Wi-Fi 行为等价全量审核

> 状态：全量静态行为审核已闭合；首批自研 RTOS/tick 契约已完成 host/L1 验证，
> 第二批真实函数指针 ABI 与第三批关联期 DVFS/VDDDIG 已完成 host/L1 验证；
> 其余唯一修复契约待分批实施。第三批硬件行为尚未复测。

## 1. 目标与判据

目标不是比较源码文字，而是验证 OpenVela/NuttX runtime-probe 固件与 Armino
权威基线 d2ded037798530175e5dc5cde6fa1878f5d5ef35 在 Wi-Fi 运行路径上的
实际行为等价性。

判据：实际翻译单元；预处理后的配置真值与活分支；函数表最终实现的副作用、
返回码、参数单位、阻塞、内存与中断语义；闭源库直接与间接契约；跨边界 ABI；
启动时真实硬件状态。只有权威源码不可见时才使用反汇编。

## 2. 审核覆盖矩阵

| 区域 | 总量 | 已审 | 未审 | 状态 |
|---|---:|---:|---:|---|
| 实际编译 C 文件 | 120 | 120 | 0 | 完成 |
| vendored 功能差异 | 17 | 17 | 0 | 完成 |
| 实际使用的 CONFIG 宏 | 223 | 223 | 0 | 完成 |
| wifi_os_funcs_t 槽位 | 223 | 223 | 0 | 完成 |
| Wi-Fi 函数指针签名 | 223 | 223 | 0 | 完成：191 兼容，32 不兼容 |
| wifi_os_variable_t 槽位 | 71 | 71 | 0 | 完成 |
| phy_os_funcs_t 槽位 | 135 | 135 | 0 | 完成 |
| phy_os_variable_t 槽位 | 53 | 53 | 0 | 完成 |
| rf_control_funcs_t 槽位 | 12 | 12 | 0 | 完成 |
| rf_control_variable_t 槽位 | 6 | 6 | 0 | 完成 |
| 闭源库直接依赖的自研符号 | 21 | 21 | 0 | 完成 |
| 同名结构体 byte_size | 379 | 379 | 0 | 完成，0 冲突 |
| 同名结构体字段 offset | 386 | 386 | 0 | 完成，0 冲突 |
| 启动/缓存/中断/时钟关键运行态 | 关键项 | 关键项 | 0 | 完成 |

## 3. 已确认事实

### 3.1 编译源码与 ABI

runtime-probe 构建的路径式与扁平式对象各 60 个；解开 vendor_sources wrapper 后
共 120 个实际编译 C 文件。DWARF 机械核对得同名结构体大小 379 项、字段偏移
386 项，冲突均为 0。详见 FOURWAY_DIFF_AUDIT.md。

### 3.2 D-cache 当前真值

权威 sdkconfig.h 定义 CONFIG_DCACHE=1，项目 config 为 CONFIG_DCACHE=y。
我方 defconfig 虽为 CONFIG_ARMV8M_DCACHE=y，但 bk7258_start.c 启动时显式调用
up_disable_dcache()，只开启 I-cache，且全树无 up_enable_dcache() 调用点。因此
当前最终状态是权威开 D-cache、我方关 D-cache；配置字面值与启动行为不一致。

但权威同时开启 CONFIG_MPU=y。其 bk7258/mpu_cfg.c region 4 在
CONFIG_CACHE_ENABLE 未定义时，把 0x28000000–0x3fffffff 配为 Normal
non-cacheable；EAPOL 探针中的 pbuf/描述符地址为 0x2808xxxx，落在该区域。
我方因为全局关闭 D-cache，同一地址也为 non-cacheable。因此对 Wi-Fi 实际使用的
共享 SRAM，两侧缓存行为等价，不解释 done=1/acked=0；差别只在其他内存性能。

分类：等价移植。若未来对齐权威开启 D-cache，必须先实现同等 MPU non-cache
区域，不能只翻 CONFIG_ARMV8M_DCACHE 或删除 up_disable_dcache()。

### 3.3 MAC/PHY 中断行为

四个动态回调 _sys_drv_int_enable/disable/group2_enable/group2_disable 已逐层追到
最终寄存器实现。两侧都对 SYS 低 bank 0x44010080、高 bank 0x44010084 做
加锁读改写；disable 返回修改前的整个 enable 寄存器，enable 返回 0。该返回值
契约供 WIFI_INT_DISABLE/WIFI_INT_RESTORE 保存并恢复原状态。我方实现一致。

bk_wifi_interrupt_init 按真实配置求值后，两侧都启用同一 7 个源：MAC gen、
MAC prot、MAC TX trigger、MAC RX trigger、MAC TX/RX misc、MAC TX/RX timer、
PHY modem。CONFIG_SOC_BK7236XX=1 且 CFG_HSU 未定义，因此两侧都不启用 HSU。
寄存器位号与权威 bk7258/sys_reg.h 一致。

分类：一致。旧探索文档中“这四个回调是空 stub”的结论是单层函数提取器未跨文件
递归造成的假阳性，不作为修复依据。

### 3.4 已实现、待板测确认：_wifi_mac_phy_power_on 五步 PM/clock 顺序

权威最终实现严格执行五个动作：

1. vote WIFIP_MAC power ON；
2. vote PHY power ON；
3. vote PHY_WIFI submodule power ON；
4. MAC clock ON；
5. PHY clock ON。

此前我方回调只直接执行 PHY power、MAC power、PHY clock、MAC clock 四个裸 helper，
缺少第 3 步。该步骤不是一次可省略的幂等寄存器写：权威 PM 同时维护
s_pm_phy_pm_state / s_pm_phy_calibration_state，并在首次 PHY_WIFI 上电时调用
phy_wakeup_reinit(1)、设置重初始化标志；后续 PHY_WIFI OFF 是否真的关 PHY 取决于
该状态。我方直接调用 bk7258_phy_power(true) 绕过了这套状态机。

虽然 vendored wifi_init.c:71 早期也会单独投票 PHY_WIFI ON，但函数表槽位本身仍应
与权威等价：闭源库可以在重连、恢复或重复上电路径再次调用它。审核分类：
当前 bk7258_wifi_mac_phy_power_on_cb()（bk7258_wifi_adapter.c:652-689）已按权威 bk_wifi_adapter.c:426-436 的五步顺序调用同一 PM/clock API，并检查每一步返回值。
PHY_WIFI vote 进入 platform_shim.c:217-283 的子模块状态机，首次路径可触发 phy_wakeup_reinit(1)。分类更新为：已实现、待板测确认；本次包含该修复的新镜像，需以 UART 看到五步调用及后续连接结果后，才能升级为板上已确认。不得再把它计入未修复项或重复修改。

### 3.5 必须修复候选：Wi-Fi 任务优先级关系未对齐

权威配置 API 值为 wifi core=2、kmsg=3、wpas=5；但 Armino 的
rtos_create_sram_thread() 在 xTaskCreate 前执行 native=9-API_priority。因此最终
FreeRTOS 原生优先级为 core=7、kmsg=6、wpas=4，即 core>kmsg>wpas。

我方真实值为 core 被 os.h 别名成 kmsg=3，kmsg=3，wpas=100，并原值直传
pthread_setschedprio。NuttX 数值越大越高，因此最终变成 wpas(100)>core(3)=
kmsg(3)：不仅量程不一致，核心相对顺序也完全反转。高日志量的 WPAS 可以长期
压过负责处理 LMAC 消息与 confirmation 的 wifi core。

审核分类：必须修复。修复阶段需定义明确的 FreeRTOS→NuttX 量程映射或直接设置
三个 NuttX 语义值，保持 core>kmsg>wpas；不能照抄 2/3/5，也不能把 core 与
kmsg 合并成同级。

### 3.6 必须修复：rtos_delete_thread 未实现删除语义

权威 FreeRTOS 契约：参数为 NULL 时立即 vTaskDelete(NULL) 终止当前线程；非空时
若目标尚未结束则 vTaskDelete(*thread) 终止指定线程。我方 rtos_delete_thread()
只在非空时把句柄置 NULL，NULL 时直接返回；bk7258_wifi_osal_thread_delete() 也
始终返回 0，不取消或退出任何 pthread。

该差异位于实际运行路径：main_supplicant.c、rw_msg_rx.c、wpa_psk_cache.c、
sa_station.c 有 rtos_delete_thread(NULL) 自删；rw_task.c 删除 wifi core/app 线程，
wpa_psk_cache.c 删除指定 worker。当前实现会留下继续执行的线程或重复状态机。

分类：必须修复。修复阶段需分别实现当前线程退出与指定线程终止，并处理 detached
pthread 句柄生命周期；不能只清空句柄。

### 3.7 必须修复：_bk_ms_to_ticks 单位换算错误

权威 FreeRTOS tick rate 为 1000 Hz，所以 bk_get_ms_per_tick()=1，
BK_MS_TO_TICKS(ms) 应等价于 ms/1，即 1000 ms→1000 ticks。
我方 CONFIG_USEC_PER_TICK=1000，同样是 1 ms/tick，但回调实现为
(ms * MSEC_PER_TICK) / 1000；因此 1000 ms→1 tick，1..999 ms→0 tick。

活调用点 rwnx_txq.c:425 用它计算 3 个 beacon interval 的 TXQ timeout。该错误
会把约 300 ms 的队列生命周期压成 0 tick，直接改变 TX 队列过期/调度行为。
分类：必须修复。修复阶段直接使用 NuttX MSEC2TICK(ms)（并确认其舍入语义）。

### 3.8 必须修复：计数信号量忽略 max_count

权威 rtos_init_semaphore(maxCount) 创建有上限的 counting semaphore；本项目大量
调用传 maxCount=1（rw_msg_tx confirmation、EAPOL 同步发送、scan completion、
WPAS 控制消息等），另有 workqueue 传 10。权威在已满时 give 返回失败。

我方 bk7258_wifi_osal_sem_create() 丢弃 max_count，使用普通 NuttX semaphore，
nxsem_post() 只受 SEM_VALUE_MAX 限制。重复 completion 可以累积，之后的新请求
可能立刻消费旧 token，而非等待本次 CFM。分类：必须修复。修复阶段需在自研
semaphore wrapper 中保存 max_count，并使 post 在上限时失败，保持 ISR-safe。

### 3.9 必须修复：32 个函数表槽位签名不兼容

使用 runtime-probe 的精确编译命令，对完整 bk7258_wifi_adapter.c 增加
-Wincompatible-pointer-types 机械审核：编译退出 0，但产生 64 条函数指针告警，
每个槽位重复两次，去重为 32 个唯一槽位。-Wcast-function-type 为 0。

明确 ABI/返回值错误的 10 项：

| 槽位 | 期望 | 我方实际 |
|---|---|---|
| _power_save_if_ps_rf_dtim_enabled | UINT8(void) | bool(void) |
| _power_save_forbid_trace | UINT16(UINT16) | void(void) |
| _ps_need_pre_process | int(UINT32) | bool(void) |
| _power_save_rf_sleep_check | bool(void) | uint32_t(void) |
| _mac_ps_bcn_callback | void(uint8_t*,int) | void(void) |
| _gpio_dev_unprotect_unmap | bk_err_t(uint32_t) | void(uint32_t) |
| _gpio_dev_unprotect_map | bk_err_t(uint32_t,uint32_t) | void(uint32_t,uint32_t) |
| _mcu_ps_machw_reset | UINT32(void) | void(void) |
| _mcu_ps_machw_init | UINT32(void) | void(void) |
| _mcu_ps_bcn_callback | void(uint8_t*,int) | void(void) |

网络/命令/WAPI 等 16 项：_send_udp_bc_pkt、_net_wlan_add_netif、
_net_wlan_remove_netif、_sta_ip_down、_sta_ip_mode_set、_uap_ip_start、
_uap_ip_down、_inet_ntoa、_lookup_ipaddr、_get_net_info、_save_net_info、
_do_evm、_do_rx_sensitivity、_evm_via_mac_evt、_wapi_wpi_encrypt、
_wapi_wpi_decrypt。它们需结合真实配置与库调用分类，但签名本身必须按权威修正，
即使最终实现是架构性 stub，也必须保留参数和确定返回值。

其余 6 项 _tx_evm_* 是 UINT32（unsigned int）与我方 uint32_t（本工具链为
unsigned long）的名义类型差异；ARM32 机器传参与返回形式相同，但 C 函数指针
仍不兼容。修复阶段使用权威声明的精确类型。

分类：32 项全部必须修复签名；函数体行为另在 283 槽行为矩阵中独立分类。

## 4. 完成判据

每项必须分类为：一致、等价移植、有意偏离、必须修复、未被运行路径使用、
或需要硬件唯一变量验证。所有槽位与配置项分类完成前不进入修复阶段。

## 5. 阶段 checkpoint 1（可安全压缩）

### 5.1 精确进度

- 实际编译 C 文件：120/120 已审；
- 跨边界结构体：byte_size 379/379、字段 offset 386/386，冲突均为 0；
- CONFIG 宏：此为 checkpoint 1 的历史快照，当时后台全量预处理审核进行中；最终
  223/223 结论、分类和证据以 checkpoint 3 第 6.4 节为准。
- wifi_os_funcs：行为 16/223 已闭合，207 待审；签名 223/223 已审，32 不兼容；
- wifi_os_variable：0/60；phy_os_funcs：0/137；phy_os_variable：0/58；
  rf_control_funcs：0/12；
- 已确认必须修复候选：_wifi_mac_phy_power_on、任务优先级映射、
  rtos_delete_thread、_bk_ms_to_ticks、计数信号量 max_count、32 个槽位签名；
- 已确认等价：Wi-Fi SRAM cache 属性、四个 SYS 中断 enable/disable 回调、
  bk_wifi_interrupt_init 的 7 个活中断源、critical-section 回调。

### 5.2 机器可读证据

| 文件 | 内容 | SHA-256 / 行数 |
|---|---|---|
| /tmp/bk7258-behavior-audit/wifi_os_funcs.tsv | 223 槽行为 ledger | 82e24222e2d172caed11ea8d1e34f4466174641201e09e0ccde5952fa4e29e8a |
| /tmp/audit.tsv | 120 个实际编译 C 文件 diff 判定 | 120 行 |
| /tmp/cfg-used.txt | vendored 源码引用的 CONFIG 宏 | 223 行 |
| /tmp/runtime-arch-compdb.json | runtime-probe 精确 arch 编译命令 | bc8dad21a04034f35cbde042a46fd1a727a1d5069a364bef72a6ad5e6e9e275b |
| tool_g001a0682d3f20001mn52DivXZ | 严格函数指针诊断原始输出 | 12e4acc536cd3745b9d9b8b1ef6ebdce6b8a25ef06db35e87151783e3dd64340 |

### 5.3 后台审核

- general-1：全部翻译单元预处理 CONFIG 真值；
- general-2：wifi_os_funcs 全槽行为；
- general-3：PHY/RF/变量表与闭源库直接契约。

它们已收到“停止扩展搜索、按当前证据返回覆盖缺口”的指令。压缩后先查询三个
actor 状态并收取报告；不得重新做已完成的源码搜索。

### 5.4 压缩后第一步

1. 读取本文第 2、3、5 节；
2. 检查 general-1/2/3 状态，收取结果；
3. 逐项复核 actor 结论并更新 ledger，不直接采信；
4. 下一批从 wifi_os_funcs ledger 第一个 pending 槽开始，每批 25–40 槽；
5. 每批结束更新计数、ledger SHA 和新的 checkpoint；
6. 所有矩阵闭合前不修改实现。

## 6. 阶段 checkpoint 2（可安全压缩）

### 6.1 三份全量矩阵已校验

| 矩阵 | 数据行 | 分类统计 | SHA-256 |
|---|---:|---|---|
| /tmp/wifi-funcs-behavior-audit.tsv | 223 | 一致51、等价移植114、有意偏离19、必须修复27、未使用12 | 59a2bc9bb7e21ec3ac7ee7850d852665c4011303c34260734adcf5daca69edda |
| /tmp/config-behavior-audit.tsv | 223 | 一致169、等价移植18、有意偏离12、必须修复8、死分支7、未分类9 | 0cd2d79469e6f43a1c85692659eb07badd7dd182478ba13ebf1270f8db1c3ae5 |
| /tmp/other-contract-behavior-audit.tsv | 298 | 一致120、等价移植108、有意偏离15、必须修复32、未使用23 | 3e0156d5fba5533d0c9b4ecdb24346080554c4ec637d32380b0d3c56cc5f35f1 |

三份 TSV 均已验证：行数正确、列数固定、唯一键无重复。上述“必须修复”
27+8+32 **不能直接相加**：任务优先级、线程 API 等在多个矩阵重复出现。待 9 个
CONFIG 闭合后，必须按行为契约键合并去重，生成唯一修复清单。

### 6.2 wifi_os_funcs 223/223 完成

全槽无未分类项。27 项必须修复的完整逐槽说明在
/tmp/wifi-funcs-behavior-audit.tsv。已由主线程独立复核的高风险项包括：

- _pbuf_header 缺少向前扩头时的 headroom 边界检查；
- _pbuf_cat 只更新链首 tot_len，权威更新 head chain 每个节点；
- _wifi_mac_phy_power_on 已补齐 PHY_WIFI 子模块 PM 状态机；当前仅待新镜像板测确认；
- rtos_create_thread 优先级映射及错误处理不等价；
- rtos_delete_thread 不删除线程；
- rtos_init/set_semaphore 不实现 max_count；
- timer init/reload/stop/is_running 的上下文和时序不等价；
- CKMN、GPIO、WAPI、网络/EVM stub 中存在签名或真实副作用差异。

严格编译额外确认 32 个唯一函数指针签名不兼容；这些槽即使被配置遮蔽，签名也
必须在修复阶段按权威声明改正，不能依赖 ARM32 寄存器碰巧兼容。

### 6.3 其他跨边界契约 298/298 完成

精确覆盖：wifi_os_variable 71、phy_os_funcs 135、phy_os_variable 53、
rf_control_funcs 12、rf_control_variable 6、libwifi 直接自研 provider 21。
32 项必须修复详见 /tmp/other-contract-behavior-audit.tsv。重点包括：

- _pm_cpu_frq_high 档位 480M→320M；
- PHY command/OTP 常量错误或为 0；
- _cmd_ble_rf_bit_set 重复 designated initializer，CLR 槽未填；
- ddev_control、SARADC/温补、OTP、bandgap/VDD、GPIO TXEN/RXEN 是空实现；
- RF 模块 power ctrl 与 low-analog 是空实现；
- bk_printf_ext/raw 把权威异步过滤日志变成同步阻塞串口。

### 6.4 CONFIG 223/223 完成

最终分类：一致169、等价移植18、有意偏离12、必须修复16、死分支8。更新后的
/tmp/config-behavior-audit.tsv 为 223 行、9 列、无重复键，SHA-256 为
eeec963dbf8735b67ac6edb8f729e1e90c1e362be497a9e32ed7a8a4fa4fe127。

最后九项按实际 d2 生成 sdkconfig.h（而不是源树中可能过时的默认 sdkconfig）
裁决如下：

| CONFIG | d2 真值 | runtime-probe 真值 | 分类 | 行为结论 |
|---|---:|---:|---|---|
| CPU_CNT | 3 | 未定义→1 | 必须修复（上游阻塞） | 与 PHY_MB 同一闭包：必须同时具备 CPU0 server、CPU1 client、mailbox driver 和 CPU1 运行域，不能单独改宏。 |
| PHY_MB | 1 | 0 | 必须修复（上游阻塞） | 权威注册 PHY channel ISR、等 ACK 最多 5 ms；我方仅本地 BK_OK fallback。缺 CP/CPU1 `mailbox_channel` / `mb_ipc` 闭包，禁止伪造 ACK。 |
| OTP_V1 | 1 | 未定义→0 | 必须修复（上游阻塞） | 权威 OTP 常量/读取为活路径；需随 `otp_driver_v1_1.c`、HAL、寄存器层和生成 `_otp.[ch]` map 一起导入。仅 item ID 已对齐；wrapper/flash shim 仍返回 BK_FAIL。 |
| FAST_CONNECT_INFO_ENC_METHOD / EASY_FLASH_V4 | 1（NULL/plain） / 1 | 未定义 / 未导入 | 必须修复（上游阻塞） | 权威以 EasyFlash V4 bk_get_env_enhance/bk_set_env_enhance 的 fast_connect_id 键持久化；本地未导入组件且 flash shim 拒绝写入。不能只开 plain 编码或伪造环境存储。 |
| MINIMUM_SCAN_RESULTS | 1 | 1 | 已实现（待板测） | 已选权威 `wpa_scan_res` ABI：producer、WPA consumer 和 `hostapd_intf` 释放 provider 均在当前 runtime source closure；需以板上 scan/connect 复测确认。 |
| WIFI_AUTO_COUNTRY_CODE | 1 | 1 | 已实现（待板测） | 已选权威自动国家码策略；现有 country-code scan 闭包在自动策略下对 12/13/14 置 `CHAN_NO_IR`，需板上 scan 复测确认。 |
| SHELL_ASYNCLOG | 1 | 0 | 必须修复（上游阻塞） | 权威 `shell_cmd_ind_out()` 是 shell task 的异步队列 provider；本地不存在该 provider，不能只开宏把 scan 输出改成未解析调用。现有同步 `bk_printf_ext/raw` 行为仍需独立处理。 |
| USE_MBEDTLS | 1 | 未定义 | 必须修复（上游阻塞） | 权威 WPA CMake 改用 `crypto_mbedtls.c` 并依赖 `psa_mbedtls`；本地只有源文件，缺 authority `psa_mbedtls` 组件/配置闭包。 |
| CRYPTO_INTERNAL | 未编入 | 实际编入 | 必须修复（上游阻塞） | 是 `USE_MBEDTLS` source-set 根因的从属结果；不得单独移除 internal crypto。 |
| WPA3 / PMF / SAE | 1 / 1 / 派生 | 未定义 | 必须修复（上游阻塞） | 权威只在 mbedTLS 闭包完整时选择 WPA3 的 dragonfly/SAE source set；不能单独开宏混用本地 internal crypto。 |
| WPA_PSK_CACHE | 1 | 未定义 | 必须修复（依赖阻塞） | 源和 `wlan_sta_gen_psk` provider 已在本地 runtime closure，但 worker 以 `rtos_delete_thread(NULL)` 自删；本地仅实现 non-NULL target cancel，启用会留下已完成 worker，且改变 PSK 请求的异步返回/时序。先修并验证 self-delete 语义。 |
| PHY_CHIP_ID_MASK | 0xFFFF0000 | 0xFFFF0000 | 已实现（host-build） | 权威 BK7258 sys_types.h 只比较 chip ID 高 16 位；本地 g_phy_os_variable 现在导出同一掩码。 |
| WPA_PSK_RELAX | 未定义 | 未定义 | 死分支 | internal PBKDF2 每 100 轮 delay 分支不活跃；WPA_PSK_CACHE 差异另案。 |

重要纠正：源树 cp/components/bk_libs/bk7258/config/sdkconfig 中的 CPU_CNT=2、
OTP_V1=0 不是此 d2 生成构建的预处理真值；实际
/tmp/armino-d2-build/bk7258/iperf/bk7258/config/sdkconfig.h 分别为 3 和 1。
因此先前探索中“OTP_V1 一致”的临时判断已撤销，最终分类以本节为准。

### 6.5 阶段 checkpoint 3：全量静态行为审核闭合

三份原始矩阵均已闭合。Checkpoint 3 后发现 _bk_ms_to_ticks 被错误分类为
“等价移植”且遗漏于唯一 ledger；纠正后原始“必须修复”记录为：wifi_os_funcs 28、
其他跨边界契约 32、CONFIG 16，共 76 条。它们不能按行数直接相加：同一最终行为同时出现在
函数表、变量表、闭源 provider 和配置矩阵中。

已按最终副作用、ABI、返回/阻塞语义和共享 provider 去重为 31 个唯一行为契约：

| 产物 | 数据行 | 结构校验 | SHA-256 |
|---|---:|---|---|
| /tmp/wifi-funcs-behavior-audit.tsv | 223 | 28 必须修复 | d9fb8090dfd90c54b5184d233a042723f33ad3c3db5b43d5e6f45b8a576a795a |
| /tmp/config-behavior-audit.tsv | 223 | 9 列、0 重复键 | eeec963dbf8735b67ac6edb8f729e1e90c1e362be497a9e32ed7a8a4fa4fe127 |
| /tmp/other-contract-behavior-audit.tsv | 298 | 32 必须修复 | 3e0156d5fba5533d0c9b4ecdb24346080554c4ec637d32380b0d3c56cc5f35f1 |
| /tmp/bk7258-behavior-audit/unique-fixes.tsv | 31 | 10 列、0 重复契约键 | 02f655578bd76d83168a06c8873d17706b2f819df1903f0a8d1a4f5d2af74312 |

唯一 ledger 的每行均带 source_matrices，可回溯所有被归并的原始槽位。高层分组为：

1. ABI/空回调、pbuf 与 tick 换算（9 项）；
2. RTOS 线程、信号量和 timer（4 项）；
3. CPU1 PHY IPC、OTP、DVFS/PHY 常量和传感/模拟/RF provider（10 项）；
4. 异步日志、fast-connect、scan ABI、mbedTLS crypto（4 项）；
5. HE、PMF、WPA3-SAE、国家码能力（4 项）。

分类已闭合不等于修复已完成。下一阶段只能以
/tmp/bk7258-behavior-audit/unique-fixes.tsv 的 31 个契约为输入，逐项判定
“可直接抄权威源码”或“NuttX 必要等价移植”，再按一类契约一批实施与构建验证。
不得仅翻开 CONFIG；例如 PHY_MB 依赖可 ACK 的 CPU1 运行域，OTP 依赖真实 OTP
provider，PMF/WPA3 依赖完整 crypto/source-set 和闭源 ABI。

### 6.6 阶段 checkpoint 4：首批自研 RTOS/tick 修复

本批严格限定为自研 wrapper/配置，不修改 vendored Wi-Fi/WPA C 源：

| 契约 | 状态 | 实施与证据 |
|---|---|---|
| MS_TO_TICKS_CONVERSION | implemented-host-build | _bk_ms_to_ticks 改为 MSEC2TICK(ms)；本配置为 1 ms/tick，ELF callback 为 identity。 |
| THREAD_PRIORITY_MAPPING | implemented-host-build | 恢复权威 Beken API 值 core=2、kmsg=3、wpas=5；权威 `BK_PRIORITY_TO_NATIVE_PRIORITY(p)=9-p`，故创建前以 pthread attribute 设置同一反向后的 NuttX priority，动态 rtos_thread_set_priority 也走相同映射。 |
| BOUNDED_COUNTING_SEMAPHORE | implemented-host-build | semaphore wrapper 保存 max_count；以短 IRQ-safe 临界区将 native count 满值检查与 nxsem_post 串行化，满时返回失败。 |
| THREAD_DELETE_SEMANTICS | implemented-host-build | 权威 NULL 为 vTaskDelete(NULL)；本地在 OSAL 创建的 detached pthread 中以 noreturn pthread_exit(NULL) 终止当前 worker。非空 handle 保持 pthread_cancel 并在成功或已结束时清 handle。 |

runtime-probe 已在绑定 slot 被完整清理、重新从当前 defconfig 生成并构建通过：

    command: ./build.sh vendor/beken/boards/bk7258/bk7258-devkit/configs/runtime-probe/ --cmake -b cmake_out/bk7258-worktrees/bk7258-wifi-rebuild--e2507a8ee482/build/runtime-probe -j12
    result: build completed successfully; L1 result=pass
    app.bin: 76887f23cf4bd99b1c7081c4ae0f71609c0fdb908b12838309e568cd7d642291
    nuttx.elf: fd69aaa7fa129ef6dfcebae17ee41a72969d0d959449b744bc3b8cb9a392862c
    config: CONFIG_BK7258_WIFI_WPA_TASK_PRIORITY=5

这仅证明编译、链接和 L1 映像契约。TXQ timeout、优先级调度顺序、full-semaphore
拒绝、target cancellation 的运行行为仍需专门 host/板上证据；本批没有打包、烧录或
连接热点。现有板子仍是先前的 runtime-probe + 真 AP 镜像，不能把它当成本批证据。

### 6.7 阶段 checkpoint 5：真实 ABI/返回值错误子批

本子批只处理生成函数表已声明、且会改变参数传递或返回寄存器语义的 10 个槽位；
刻意没有处理六个 UINT32 与 uint32_t 的名义类型差异，也没有修 GPIO/MCU-PS 的
底层硬件副作用。

| 范围 | 结果 |
|---|---|
| power-save ABI | _power_save_if_ps_rf_dtim_enabled、_power_save_forbid_trace、_ps_need_pre_process、_power_save_rf_sleep_check 现使用生成表精确 UINT8/UINT16/int/bool 声明；遮蔽配置下返回 authority 同样的确定零值。 |
| MAC-PS ABI | _mac_ps_bcn_callback、_mcu_ps_bcn_callback 接收 data,len；_mcu_ps_machw_reset/init 返回确定 UINT32 0。底层 MCU-PS 仍为 authority 同样的遮蔽 stub。 |
| GPIO ABI | _gpio_dev_unprotect_map/unmap 现在返回现有 provider 的 bk_err_t；provider 本身仍是 success/no-op，因此 GPIO_RF_MAPPING 仅 partial-abi-host-build，不能宣称 pinmux 已对齐。 |
| 未触碰 | 六个 tx-EVM UINT32/uint32_t 名义差异、网络/EVM/WAPI 其余不兼容槽、全部 GPIO/RF 硬件副作用。 |

runtime-probe 重新编译并 L1 通过：

    command: ./build.sh vendor/beken/boards/bk7258/bk7258-devkit/configs/runtime-probe/ --cmake -b cmake_out/bk7258-worktrees/bk7258-wifi-rebuild--e2507a8ee482/build/runtime-probe -j12
    result: build completed successfully; L1 result=pass
    app.bin: 18eb8e8128bcfcdc8321affc0aed26e2e9a8d19e9fce3738562cdba08e9ad215
    nuttx.elf: 64a6b4b0ea2a4e0ca07d507dd2e252edb40944d57cb35b53b4ecaafd3a83dcf3
    config: CONFIG_BK7258_WIFI_WPA_TASK_PRIORITY=5

正常 runtime-probe adapter 编译没有报告上述十个槽位的 incompatible-pointer-types。
额外按 compile_commands 中的原始命令重跑诊断时，命令工作目录缺少其相对 object
输出目录而返回非零；该次结果不能作为“严格诊断通过”证据。其留下的诊断仅为未触碰的
network/EVM/WAPI 槽和六项 UINT32/uint32_t 名义差异，未含本子批十项。L1 成功构建
才是本子批的有效编译证据。

### 6.8 阶段 checkpoint 6：M2 未 ACK 的板上复测与 DVFS 修复

烧入 checkpoint 5 镜像后，2.4 GHz 热点的 /tmp/0903-15.log 在六次完整关联中仍
复现相同边界：扫描、认证、关联和 M1 均成功；每个 M2 均进入 QoS TXQ，随后得到
done=1 acked=0 retry=0 swretry=0，三次后由本地断开。因此首批 RTOS/tick 与第二批
ABI 修复没有改变 M2 的空口 ACK 故障边界，也没有观察到回归。

> **对 `done=1 acked=0 retry=0 swretry=0` 的判读更正（2026-09-04）。**
> 这个组合**本身不是异常特征**，因此上文（以及本仓多处）把它当作故障签名来推理
> 是不成立的。
>
> 证据：`/tmp/0904-open-2.log` 用 `txtest` 向同一 BSSID 注入 8 个单播数据帧
> （tid=0，ethertype 0x88b5，走 `bmsg_tx_sender` 通用数据路径），共 42 行
> `txcfm`。其中帧 0 唯一一次投递的终态恰好是 `st=00000001`，即
> `done=1 acked=0 retry=0`；而帧 5/6/7 各经 8 次投递后到达 `st=00000009`
> （`ack=1`）。同一代码路径、同构的 host 侧字段，既产出该组合也产出成功 ACK。
> 所以它的含义只是「这一次投递未被确认，且 MAC 未（再）要求重传」，不含
> 「描述符被破坏」或「BC/MC 分支泄漏、MAC 认为无需 ACK」等信息。
>
> 据此，真正待解释的异常改写为两条：
>
> 1. **首次投递成功率过低**：tid=0 单播首投 0/12（open-1 4 帧 + open-2 8 帧），
>    而信号为 `level=-28` / `rssi=-40`。最终成功 3/8 全靠重传堆叠。
> 2. **M2 拿不到重传**：`/tmp/0904-wap-2.log` 18 帧 EAPOL（tid=7）对应 18 行
>    `txcfm` 与 18 行 `l2tx cfm`（1:1，即每帧恰好一次投递），`st` 恒为
>    `00000001`、`fl` 恒为 `00000000`；而 tid=0 可达 8 次投递并出现
>    `fl=00002001`（`TXU_CNTRL_RETRY | RC_TRIAL`）。M2 从不进入重传机制。
>
> 若第 1 条成立，第 2 条即可由「1 次机会 × 极低首投成功率」推出，两者可能同源，
> 不必假设两个独立缺陷。

此次日志同时排除两个旧假设：

1. D-cache 脏行不是当前根因。目标 CP 启动代码显式调用 up_disable_dcache，Wi-Fi
   SRAM 0x2808xxxx 因此为 non-cacheable；权威虽全局开 D-cache，但以 MPU 把
   0x28000000–0x3fffffff 配为 non-cacheable。两侧 EAPOL pbuf/描述符实际属性相同。
2. TX probe 的 aligned=0 不是 pbuf 对齐失败。它打印的是
   packet_addr = p->payload + sizeof(ETH_HDR_T)；Ethernet header 为 14 字节，
   因而 4-byte aligned payload 的 EAPOL 数据起点天然为 2 mod 4。后续 probe 应
   同时打印 payload 和 packet_addr，不能把后者作为 DMA 对齐失败。

仍活跃、可重复、且与权威直接不等价的路径是每次关联前的：

    dvfs: 5 tier needs vdddig voting (not ported); vote ignored
    cpu_freq vote module=26 freq=5 ret=-1

根因已定位到自研 hal_port_dvfs.c 的主动保护分支，而不是硬件自然拒绝。权威 PM 对
320M/480M 先记录 CPU-frequency VDDDIG high vote，再通过频率 ladder 在升频前提高
VDDDIG；本 port 的 ladder 已实现该电压/时钟顺序，却在入口处直接返回 BK_FAIL。

本批只改两个同一契约的点：

| 契约 | 状态 | 实施 |
|---|---|---|
| DVFS_HIGH_ENUM | implemented-host-build | g_wifi_os_variable._pm_cpu_frq_high：PM_CPU_FRQ_320M(5) 改为权威 PM_CPU_FRQ_480M(6)。 |
| 高档 VDDDIG/clock vote | implemented-host-build | 解除自研 320M/480M 的 BK_FAIL guard，让 live freq=5 进入已有的 VDDDIG-before-clock BK7258 ladder。 |

runtime-probe L1 验证：

    command: ./build.sh vendor/beken/boards/bk7258/bk7258-devkit/configs/runtime-probe/ --cmake -b cmake_out/bk7258-worktrees/bk7258-wifi-rebuild--e2507a8ee482/build/runtime-probe -j12
    result: build completed successfully; L1 result=pass
    app.bin: 53226d6b6d7221ccbe9dca0c1e23558b9e55c10b1e26cfceb3757c38c043b3d4
    nuttx.elf: 27f0fe5c22fabea40fec1a659d71ee27765bffbbe289e27af38b450a92bf6cf8

本批尚未打包、烧录或板测。下次同热点验收必须同时检查：

1. 关联前日志变为 high-tier VDDDIG/clock vote freq=5，且
   vote_cpu_freq(... freq=5) ret=0；
2. DVFS 状态寄存器/电压日志显示实际高档状态；
3. M2 的 acked 是否从 0 改变，及 M3/M4 是否出现。

当前唯一 ledger 共 31 项：implemented-host-build 6、partial-nonnull-host-build 1、
partial-abi-host-build 1、pending 23。当前 TSV 摘要：wifi_os_funcs 223 行，其中
必须修复21；唯一 ledger 31 行、10 列、零重复键。

### 6.9 压缩后唯一入口

任务 T5：BK7258 Wi-Fi 行为等价修复分批进行中（Checkpoint 6）。压缩后不得重跑
三份全量矩阵。当前板上仍是 checkpoint 5 镜像；DVFS 批须经新包、正确 full_flow
PTY 烧录和同热点 UART 复测后才可声明板上完成。其后再处理
rtos_delete_thread(NULL) 的 self-exit 语义，并将六个名义 UINT32/uint32_t 差异
独立成批；每批保持单一行为类别、记录构建命令和产物 SHA；
涉及硬件的契约必须附 BK7258 烧录步骤、UART 日志和实测结果，不能用 host/mock
宣告完成。
