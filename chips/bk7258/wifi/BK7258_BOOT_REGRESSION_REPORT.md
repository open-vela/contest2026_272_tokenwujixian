# BK7258 启动回归调查报告

> 涵盖从"Wi‑Fi 适配后 CP 卡死"到"hard‑float FPU 启动缺口修复"的完整调查记录。

---

## 1. 问题现象

在 Wi‑Fi 适配之前，BK7258 CP+AP AMP 可稳定进入 `nsh>`，SysTick 正常。适配 Wi‑Fi 过程中，初步补齐接口并链接烧录后，CP 卡在 `up_initialize()` 返回之后、`board_late_initialize()` 之前，表现为：

- 串口输出 `BK\r\n` 后静默，或打出部分插桩标记后停止。
- 停止点在多次上电间**漂移**（非确定性）。
- 关闭 `CONFIG_BK7258_WIFI` 后如果未清 CMake 缓存，问题仍复现。

---

## 2. 启动序列与插桩标记

已验证的 CP 启动关键顺序：

```
__start()
  cpsid i
  up_disable_dcache()
  up_enable_icache()
  putreg32(_vectors, NVIC_VECTAB)
  .bss/.data 初始化
  bk7258_lowsetup()
  lowputc('B','K')              ← banner
  arm_earlyserialinit()
  nx_start()
    ├── up_initialize()
    │     ├── up_irqinitialize()        → A B C D
    │     ├── up_timer_initialize()     → E F (SysTick)
    │     ├── arm_addregion()           → P (PSRAM init)
    │     ├── arm_serialinit()          → G H I
    │     └── 其他子系统                → u v w x y z 0
    ├── up_putc('P')                    ← up_initialize 完成
    ├── drivers_initialize()
    │     ├── syslog / devnull / ...    → a b c d e f
    │     ├── rpmsg_initialize()
    │     ├── rpmsg_serialinit()        → r s t U V W X Y
    │     └── ...                       → Q
    ├── board_early_initialize()        → J
    └── board_late_initialize()         → K L M N
```

---

## 3. 排查路径与逐步定位

### 3.1 初始怀疑方向

| # | 假设 | 结论 |
|---|------|------|
| 1 | `arm_netinitialize()` 卡住 | 不成立：net init 在 `NETDEV_LATEINIT` 下不在此执行 |
| 2 | `libwifi.a/libbk_phy.a` 撑爆内存/改变段布局 | 部分成立：Wi‑Fi bss 把 `_ebss` 推高，SRAM heap 被压缩到 ~123KB |
| 3 | 闭源库带 `constructor` / `.init_array` | 未发现证据 |
| 4 | `.dtcm_sec_data` 段/全局对象覆盖 | 未发现证据（NuttX 链接脚本不使用该 section） |
| 5 | heap 起始/大小被 Wi‑Fi 段挤动 | 成立但非直接根因：heap 仍有 ~123KB，首次 malloc 能成功 |

### 3.2 精确定位过程

通过逐步插桩确认：

1. **卡在 `drivers_initialize()` 内部**（`P` 打出、`Q` 未打出）。
2. 进一步定位到 **`rpmsg_serialinit()` → `uart_rpmsg_init()`** 内部。
3. 该函数内 `kmm_zalloc(148)` 成功，`memset` 完成，但随后的 heap 操作（`mm_free_delaylist()` 或 `rpmsg_register_callback()`）随机卡死。
4. 停止点在 `n`、`o`、`<` 三处漂移，排除固定地址覆盖。

### 3.3 异步破坏假设

基于停止点漂移，提出"异步中断在 heap 非原子窗口打断执行"假设：

- SysTick 已在 `up_timer_initialize()` 使能。
- `drivers_initialize()` 里的 `kmm_malloc` 依赖 `mm_lock`（mutex/sem），调度器部分就绪时 lock 可能不完全有效。
- IRQ handler 可能触发 `kmm_free`（走 delay-list），与主流程交叉写坏 heap。

**Wi‑Fi MAC 中断假设被排除**：`bk_wifi_interrupt_init()` 仅由 `bk_wifi_init()` 调用，后者在手动 NSH 命令后才执行，`arm_addregion()` 时不可能已被调用。

### 3.4 Config 单变量实验

| 对照 | PSRAM | FPU/hard‑float | DEBUG_MM | 网络 | Wi‑Fi 静态链接 | 真机结果 |
|------|-------|----------------|----------|------|----------------|----------|
| bk7258-psram 基线 | 是 | 否 | 否 | 否 | 否 | ✅ 可启动 |
| no-Wi‑Fi runtime-probe | 是 | 是 | 是 | 是 | 否 | ❌ EFP1a |
| FPU-only | 是 | 是 | 否 | 否 | 否 | ❌ EFP1a |
| DEBUG_MM-only | 是 | 否 | 是 | 否 | 否 | ✅ PSRAM 成功→GHI |
| network-only | 是 | 否 | 否 | 是 | 否 | ✅ nsh> |

**结论**：`CONFIG_ARCH_FPU=y`（hard‑float）是唯一在单变量实验中稳定对应 EFP1a 的配置差异。

---

## 4. 根因

### 4.1 已确认缺口

BK7258 的 `__start()` 直接进入 `nx_start()`，**未调用 NuttX 提供的 `arm_fpuconfig()`**。该函数因无调用者被链接器裁掉。

结果是：镜像带有 VFP ABI（`Tag_FP_arch: FPv5/FP-D16`、`Tag_ABI_VFP_args: VFP registers`），但 **CPACR 的 CP10/CP11 权限、CONTROL.FPCA 与 FPCCR 策略没有在启动时建立**。

`arm_fpuconfig()` 的作用：
- 开放 CPACR 的 CP10/CP11（允许 FP 指令执行）；
- 设置 CONTROL.FPCA；
- 修改 FPCCR 的自动/延迟 FP 保存策略；
- 让异常入口和上下文切换使用扩展 FP frame。

### 4.2 为什么表现为 PSRAM 初始化失败

PSRAM power path 本身不需要浮点指令。最合理解释：

- hard‑float ABI 与未建立的 FPU context policy 不兼容；
- PSRAM 时序窗口内如果发生首次异常（如 SysTick），异常入口会尝试保存 FP frame → 因 CPACR 未开放而产生 UsageFault 或未定义行为；
- 表现为 PSRAM `bk7258_psram_delay()` 期间被中断打断后不返回。

### 4.3 `CONFIG_DEBUG_MM` 的干扰

DEBUG_MM‑only 对照触发的 assertion 是诊断代码在 idle task 中调用 `kmm_checkcorruption()`，而 `mm_foreach()` 明确断言 `!sched_idletask()`。**这不是 heap corruption 证据**，只是检查调用位置不符合前置条件。

---

## 5. 修复

### 5.1 修复内容

在 `__start()` 中，于 `.data/.bss` 就绪、IRQ 仍由 PRIMASK 屏蔽时，调用 `arm_fpuconfig()` 并执行 `dsb/isb`：

```c
void __start(void)
{
  __asm__ volatile ("cpsid i" : : : "memory");

  // ... cache, vectors, .bss/.data ...

  arm_fpuconfig();                    // ← 新增
  __asm__ volatile ("dsb\n\tisb" : : : "memory");

  bk7258_lowsetup();
  // ...
  nx_start();
}
```

### 5.2 真机验证

修复后 hard‑float + PSRAM CP 的关键输出：

```
... EFP1abcdefg2345678R
[BK7258] PSRAM id=0x8d08 size=16777216 added to heap
GHIuvwxyz0PabcdefrstUlnoqQVghijmMWpPXYZ...
[AMP] CP RPTUN master initialized (Mailbox IRQ)
NuttShell (NSH)
nsh> [AP] release seq=1 vector=0x02160000
```

证明：
- hard‑float PSRAM early‑init 越过原 EFP1a 回归点；
- PSRAM 0x8d08 探测和 16 MiB 第二 heap 注册成功；
- RPMsg UART、Timer0、RPTUN 和 CP AP‑release 路径正常；
- 进入可交互 `nsh>`。

---

## 6. 已排除项

| 假设 | 排除依据 |
|------|----------|
| Wi‑Fi runtime/MAC IRQ 在 PSRAM 前运行 | `bk_wifi_interrupt_init()` 只由手动 NSH 命令后的 `bk_wifi_init()` 调用 |
| Wi‑Fi 静态链接是 PSRAM 回归的必要条件 | no-Wi‑Fi control 仍得到 EFP1a |
| `modifyreg32(ANA_REG13)` 被 `bk7258_analog_write()` 劫持 | 最终 ELF 确认 PSRAM path 直接读改写 `0x44010134` |
| 网络或 `NETDEV_LATEINIT` 是必要条件 | network-only control 到达 nsh> |
| PSRAM 基础初始化不可用 | non-FPU DEBUG_MM-only 完成供电、时钟、ID、probe 和第二 heap region 注册 |
| heap 容量不足导致 crash | 124,864 B 可用，当前只分配约 8.3 KiB |
| heap metadata 损坏 | DEBUG_MM assertion 是 idle task 检查位置不当，非 corruption |

---

## 7. ABI 约束

`chips/bk7258/wifi/ABI_AUDIT.md` 记录的 vendor 库 ABI 约束：

- `libwifi.a` 使用 ARM hard‑float 调用约定，`Tag_ABI_VFP_args` 为 VFP registers；
- 使用 FPv5-SP-D16；
- Wi‑Fi archive 与其回调表边界必须与 NuttX 保持同一 ABI。

NuttX 在 `CONFIG_ARCH_FPU=y` 且未选择 `CONFIG_ARM_FPU_ABI_SOFT` 时生成：
```
-mfpu=fpv5-d16 -mfloat-abi=hard
```

软浮点、softfp 或只为 Wi‑Fi archive 局部换 ABI 都会破坏包含浮点参数/返回值的调用约定，不能作为 production Wi‑Fi 的替代方案。

---

## 8. 工程策略

### 8.1 hard‑float 显式门控

`CONFIG_BK7258_HARD_FLOAT` 是 ABI policy：

- 普通 PSRAM/RPMsg/网络的无 Wi‑Fi 基线：保持 hard‑float **关闭**；
- 链接 vendor `libwifi.a` 的配置：显式打开 hard‑float，匹配 vendor ABI；
- 不允许以关闭 hard‑float 的配置去链接 vendor Wi‑Fi archive；
- 不应通过关闭 PSRAM、关闭网络或把 `bk_wifi_interrupt_init()` 改空来掩盖问题。

### 8.2 FPU 启动缺口已修复

- 保留 `__start` 中的早期 `arm_fpuconfig()` + `dsb/isb`；
- 后续 Wi‑Fi runtime 调用在此基础上继续推进；
- 不能再以关闭 hard‑float 作为 PSRAM 或 Wi‑Fi 的兼容手段。

### 8.3 后续验收边界

已证明：
- ✅ hard‑float + PSRAM 可启动至 NSH、RPMsg UART、RPTUN 与 AP release；

尚未证明：
- ❓ `libwifi.a` 可在 PSRAM-enabled hard‑float 配置中安全链接和执行；
- ❓ PSRAM 的 DMA/cache coherency；
- ❓ 非-idle 上下文的 heap checker 无 metadata assertion；
- ❓ Wi‑Fi RF 校准 / scan / association 完整路径。

---

## 9. 关键文件索引

| 文件 | 用途 |
|------|------|
| `chips/bk7258/bk7258_start.c` | CP 启动入口，`arm_fpuconfig()` 调用点 |
| `chips/bk7258/bk7258_allocateheap.c` | heap 分配 + PSRAM `arm_addregion()` |
| `chips/bk7258/bk7258_psram.c` | PSRAM 硬件初始化 |
| `chips/bk7258/bk7258_sysctrl.c` | analog/PMU/power gate |
| `chips/bk7258/wifi/bk7258_wifi_adapter.c` | capability table (`g_wifi_os_funcs`) |
| `chips/bk7258/wifi/ABI_AUDIT.md` | vendor 库 ABI 审计 |
| `chips/bk7258/wifi/CAPABILITY_TABLE_GAP.csv` | 表项缺口清单 |
| `nuttx/sched/init/nx_start.c` | NuttX 启动主流程 |
| `nuttx/drivers/drivers_initialize.c` | 驱动初始化入口 |
| `nuttx/drivers/serial/uart_rpmsg.c` | RPMsg UART（卡点所在） |
