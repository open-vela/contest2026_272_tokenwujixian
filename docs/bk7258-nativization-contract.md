# BK7258 原生化契约（阶段二）

本文件是「把一个 Armino 基线单元换成我们自己的实现」这件事的**契约与验收记录**。
每换一批，就在这里加一节：符号表 + 函数体要点 + 上板证据。

## 五条不变式（用户定的口径；第 5 条 2026-10-22 补）

1. **符号不变**
   - 用 `arm-none-eabi-nm --defined-only -g <旧 .o>` 抓出基线单元的**全部**导出符号；
   - 我们的实现必须**同名、同签名、同返回语义**全部提供 —— 签名取自基线自己的头文件，不另抄声明；
   - **“今天没人调用、被 `--gc-sections` 丢掉”不是少实现符号的理由**。镜像里在不在一回事，契约是否完整是另一回事；
   - 链接映证：`nuttx.map` 里对应的 `libarmino_*.a(<unit>.c.o)` 消失，符号的提供者换成我们的 `.o`。

2. **语义不变**
   - 逐**函数体**比对：寄存器与写入顺序、返回值与错误分支、状态量、时序；
   - 不是比签名，不是比 `diff` 文本。

3. **C 与 H 的写法原生 Vela 化**
   - `.c`：NuttX 风格（2 空格、`/* */`、花括号独占行、`#include <nuttx/config.h>` 在最前）；
   - `.h`：我们替换掉的单元，其**对外契约头也归我们**，放在 `chips/bk7258/include/`，
     用 NutxX 的 include guard 和排版；
   - 头文件只产生声明、不产生代码，所以"两边是否一致"用**预处理结果**证明，不需要新增 `.c`：
     用同一套编译参数分别预处理「只 include 权威头」和「只 include 我们的头」两个极小 TU，
     抽出涉及目标符号的声明行逐条比对（工具：`my_docs/bk7258-wifi/tools/header_equiv.py`，仓库外）；
   - 比对为**逐条相同**后，把我们的头放到与权威头**相同的相对路径**下，
     也就是芯片的 include 根本身（`chips/bk7258/include/driver/wdt.h` …），
     并在 CMake 里把该根目录排在基线 `-I` 之前 ——
     这才是"替换"，不是转发层：声明在我们文件里，权威头只是不再被先搜索到；
   - 头必须**按路径一对一替换**：`driver/wdt.h` 只放 `driver/wdt.h` 的内容、`driver/aon_wdt.h`
     只放它的内容。把两个面合并进一个头会让所有 include 点的可见声明集合变大，属于契约变更；
   - 内部 HAL 类型（`wdt_hal_t`、`gpio_hw_t`…）与私有头（`bk_wdt.h`、`wdt_driver.h`）暂不接管：
     它们是实现细节，且私有头没有"公共路径"可替换。

4. **名字按"它是什么"起，不按"它怎么来的"起**
   - 头与源文件的路径就是它替代的那个东西的路径：`chips/bk7258/include/driver/wdt.h` 就是 `driver/wdt.h`；
   - 不引入 `overrides/`、`_api`、`_shim` 这类只描述迁移动作的名字；
   - 判断标准一句：这个名字回答的是"它是什么"，还是"它怎么来的"？后者一律改掉。

5. **标准 ops 在中间，原实现在里面**
   - 对外的 Armino 形状符号是**兼容面**：其余模块（`pm`、`low_pwr`、`rosc_ppm`、`flash_notify`、
     `saradc_notify`、`bk_wifi_adapter`…）一行不改就能继续调用，这是"分批迁移"能成立的前提；
   - 外设要挂到 NuttX 的标准框架上：`uart_ops_s`、`rtc_ops_s`、`mtd_dev_s`、`dma_dev_s`、`adc_ops_s`、
     `watchdog_ops_s`、`pm_register`…；
   - **一个单元只有一份实现**，Armino 面和 `/dev/<node>` 共用它。判两件事，**不判调用方向**：
     (1) **寄存器一份**——同一个寄存器不许两面各写一套；
     (2) **状态不分叉**——lower-half 不留会和驱动状态量走散的副本，Armino 直接调 `bk_*` 之后，
     `/dev` 面读到的仍然对（从驱动的状态量现读）。
     ops 调 `bk_*`、或 `bk_*` 调 ops 都可以；上游 NuttX 的 `esp_wdt.c` / `esp_rtc.c` / `esp_timer.c`
     就是 ops 方法体调厂商 HAL；
   - **目标形态：状态归 lower-half**（用户 2026-10-23 口径）。上游是
     `/dev/xxx → ops（lower-half 持有状态）→ *_hal / *_ll（无状态）`，厂商驱动层不编进来。我们保留
     Armino 驱动层只为兼容调用方，所以做法是：驱动的状态量**搬进 lower-half 结构体**，`bk_*` 兼容面
     读写同一个结构体，调用方一行不改。上面两条是合格线，这一条才算原生化完成。
     样板：`bk7258_wdt.c` 的 `struct bk7258_wdg_s`（`lower` + 基线的 `s_wdt` / `s_wdt_driver_is_init` /
     `s_wdt_period` + 最后一次重装计数器的 tick）；
   - 判定方式：同一个语义必须两个面都跑通。RTC 的验收就是两组——设备口 `RTC_SET_TIME`/`RTC_RD_TIME`
     往返，内核口 `clock_settime`/`clock_gettime` 并确认 `/dev/rtc0` 跟着变；
   - **例外必须写明**：确实没有 NuttX 等价物的那一小块（例：AON RTC 的 µs 单调计数，框架只有秒级）
     留在驱动内部，并在本文档"ops 语义完整性"一类的表里写清理由；
   - **薄封装是合法的，判的是"基底"而不是"调用点长相"**（用户 2026-10-23 口径）：Armino 形状的
     `GLOBAL_INT_DISABLE/RESTORE`、`os_malloc/os_memset`、`BK_LOGx` 只要**下面接的是 NuttX**，就应当保留，
     不要去改调用点。实测证据：`rtos_disable_int()` → `up_irq_save()`（BASEPRI）；
     `os_malloc` → `os_malloc_debug` → `malloc` → `--redefine-syms` → `__wrap_malloc` → `kmm_zalloc`；
     `BK_LOGx` → `bk_printf_ext` → `syslog`。**改这三类调用点是 churn，不是原生化。**
   - 因此写真书只在两种情况下动手：**(a) 基底不是 NuttX**（下面接的仍是 Armino 寄存器/RTOS/堆）；
     **(b) 契约类型被换成"看起来更 NuttX"的类型**（例：签名里 `UINT32` 不能换 `uint32_t`，
     这套头里两者宽度相同但类型不同，换了直接 `conflicting types`）。
   - 推论：**一个外设如果 NuttX 有框架而我们没挂，它就是没做完**，不能算"已经原生化"。
     当前状态（2026-10-23 逐个读 ops 方法体核对；导出数用基线 `.o` 的 `nm --defined-only -g`
     对 `libarch.a` 做 `comm -23`，CP 构建 `nuttx.map` 11:16）：

     | 外设 | `/dev` 面 | 两面是否同一份实现 | 第 5 条 |
     | --- | --- | --- | --- |
     | RTC | `rtc_ops_s` 已挂 | 寄存器一份：ops 调 `bk_aon_rtc_get_us` / `bk_alarm_unregister`。状态不分叉：lower-half 只有 `lower`，时间从驱动现读，闹钟是 `bk_alarm` 链表上的一个节点 | 通过。板上（镜像 `a21c3d6d…`，`bk7258_drvtest rtc`）：设备口 `RTC_SET_TIME(2026-01-01)` 读回一致；内核口 `clock_settime(2030-01-01)` 后 `CLOCK_REALTIME`=1893456000、`/dev/rtc0` 跟着读到 2030-01-01；`RTC_SET_ALARM(+5s)` 收到 SIGALRM(14)。契约头（2026-10-26）：在已收的 `driver/aon_rtc.h` 之外再收 `driver/aon_rtc_types.h`、`aon_rtc_hal_64bit.h`（被替换单元是 `aon_rtc_hal_64bit.c`，`aon_rtc_hal.h` 不属于它）；以 `bk7258_aon_rtc.c` 的编译参数预处理比对，声明（忽略标点两侧空白）与宏差异均为 0；收头前后 `.text`/`.data` 逐字节相同、镜像 sha256 不变（`8d11ea48…`），板上证据沿用 |
     | 看门狗 | `/dev/watchdog0` 已挂 | 2026-10-23 状态搬进 lower-half：`struct bk7258_wdg_s` 持有 `drv`（原 `s_wdt`）、`driver_init`、`period_ms`、`feed_tick`，`bk_wdt_*` 读写同一个结构体。修掉的两处分叉：原 `getstatus` 的 `timeout` 读 lower-half 自存的 `timeout_ms`（`bk7258_flash_driver_ext.c:540-544` stop/start 之后与驱动周期不一致），`flags` 写死 `ACTIVE`（`bk_wdt_stop()` 后仍报 active）。现在 `ACTIVE` 跟 `init_bits` BIT(0)，`timeout` 读 `period_ms`；`feed_tick` 只在真正重装计数器的 `bk_wdt_start`/`bk_wdt_feed` 里更新（原来在 `bk_wdt_feed_handle` 里无条件更新：1 s 节拍下 `INT_WDG_FEED_PERIOD_TICK`=500 恒满足，平时两者等价；看门狗被 stop 后 `bk_wdt_feed` 返回 `NOT_INIT` 不重装，旧写法仍刷新时间戳，`timeleft` 就是假的）。对基线函数体的偏离：`bk_wdt_start`/`bk_wdt_feed` 各多一次 `clock_systime_ticks()` 写 `feed_tick`，寄存器写序与返回值不变。43/43 导出不变，`nm -u` 0，`decode pass`。板上（镜像 `0874d004…`，`bk7258_drvtest wdg`）：dev `SETTIMEOUT(10000)` 后 timeout=10000；Armino `bk_wdt_stop()` 后 `/dev/watchdog0` 报 active=0、timeout=8000（基线 `wdt_deinit_common` 把周期复位成默认值，`/dev` 面跟着变）；Armino `bk_wdt_start(8000)` 后 active=1；dev `STOP`/`START` 返回 0，active 0/1 跟随。契约头（2026-10-26）：在已收的 `driver/wdt.h`、`driver/aon_wdt.h` 之外，再收 `driver/wdt_types.h`、`driver/aon_wdt_types.h`、`wdt_hal.h`、`aon_wdt_hal.h`；六个头用 `sys_hal.c` 的编译参数预处理比对，声明与宏差异均为 0，`-H` 确认解析到我们的头；收头前后 CP 的 `.text`/`.data` 逐字节相同、打包镜像 sha256 不变（`8d11ea48…`），所以沿用该镜像的板上证据。停狗期间 `timeleft` 原先仍按上次重装倒数（7999、7987），已改为未 ACTIVE 时报 0；镜像 `a21c3d6d…` 上板复核：`after armino stop`、`after dev stop` 两行 `timeleft=0`，其余各行与上一轮一致 | 通过 |
     | UART | `/dev/console`、`/dev/ttyS0` | 两面都落在 `uart_ll_*`（`uart_hal_*` 是它的宏）。2026-10-23 起 `bk7258_lowputc()` 的 FIFO 轮询/写入和 `bk7258_lowsetup()` 的关中断/清中断也改走 `uart_ll_*`；反汇编仍是 `ldr [base,#0x18]` 测 bit20、`str [base,#0x1c]`，`lowsetup` 写序 `0x08←1, 0x20←0, 0x24←0xff, 0x10←cfg` 不变。状态：控制台 ops 自存 `priv->ie`；Armino 面能改 UART0 中断使能的 `bk_uart_init` 链在镜像里不可达（见批次 4 实现记录），所以今天不分叉——它一旦变得可达，`priv->ie` 就要改为现读 | 通过（例外：`lowsetup` 里 `GLOBAL_CTRL`、`CONFIG` 两次整字写保留——`uart_ll_soft_reset` 与逐字段 setter 都是读改写，换了就改变写入序列，违反第 2 条；SoC 中断路由 `BK7258_SYS_CPU0_INT_EN` 无 `uart_ll` 对应）。契约头（2026-10-26）：`driver/uart.h`、`driver/uart_types.h`、`uart_hal.h` 收归 `chips/bk7258/include/`；以 `bk7258_uart_driver.c` 和 `wifi_armino/compat/bk7258_armino_shell_api.c` 两组编译参数预处理比对，声明与宏差异均为 0，`-H` 确认解析到我们的头；收头前后 `.text`/`.data` 逐字节相同、镜像 sha256 不变（`8d11ea48…`） |
     | GPIO | `/dev/gpio0` 已挂 | 2026-10-26 改写。CP：Armino 面（`bk_gpio_*`、`gpio_dev_map/unmap`）改回基线函数体，全部经导出的 `s_gpio`（基线 `gpio_driver_t`）走 `gpio_hal_*`/`gpio_ll`；板级 helper（`bk7258_gpio_write/read/config_*/periph/uart0_*`）的 CFG 读写和外设复用改走 `gpio_ll_get/set_value`、`gpio_ll_get/set_perial_mode`，每个 helper 的读改写顺序不变；按 `#if` 分支检查，CP 编进来的代码里不再有直接 `getreg32/putreg32`。AP：不编基线、没有 `gpio_ll`，helper 继续直接写寄存器；其中 `bk7258_gpio_cfg_read/write` 必须是宏而不是 `static inline`——AP 是 `-O0`（`CONFIG_DEBUG_NOOPT`），函数形式给每次寄存器访问多一次调用，见下方按键对比；改成宏后 AP 中断路径各函数与改动前逐条指令相同，只有开机用的 `select_func_locked`、`periph` 不同；这是 AP 镜像里唯一的 GPIO 路径（例外）。导出：`gpio_hal` 27/27；补了 10 个（6 个 `gpio_*_sel`、`gpio_check_dev/capacity_match`、`gpio_get_interrupt_status`、`s_gpio`）；**推迟 13 个**，镜像里零引用——T12（PM）：`gpio_enter/exit_low_power`、`gpio_enable_interrupt_mult_for_wake`、`bk_gpio_retention_set/clr`、`gpio_retention_map_set/clr`、`s_gpio_bak_int_type_regs`、`s_gpio_bak_int_enable_regs`、`s_gpio_is_lowpower_keep_status`、`s_gpio_is_setted_wake_status`；T16（跨核）：`gpio_ipc`、`gpio_ipcsection`。契约头：`driver/gpio.h`、`driver/gpio_types.h`、`gpio_hal.h` 收归 `chips/bk7258/include/`，在 `bk7258_gpio.c`、`bk7258_gpio_hal.c`、`bk7258_driver.c` 三组编译参数下预处理比对差异 0，收头前后 CP/AP `.text`/`.data` 逐字节相同。CP 侧相对上一版的行为变化（按基线）：`gpio_dev_map` 要求引脚在 `usr_gpio_cfg.h` 的 `GPIO_DEFAULT_DEV_CONFIG` 里，复用前先关输出/输入/上下拉/中断；`bk_gpio_enable_interrupt` 前有 1000 次空循环；`bk_gpio_driver_init` 补上 `gpio_hal_init()`。`nm -u` 0，`decode pass`。板上（镜像 `29ad30f9…`，烧录日志 17:04 `Writing Flash OK`）：CP `bk7258_drvtest gpio` 写 1 读回 1、写 0 读回 0、恢复原值——`go_write` 返回 0 说明 `bk7258_gpio_config_output`（已改走 `gpio_ll`）确实把 GPIO40 置成了输出模式 同一镜像 AP 侧（用户观察，未留日志）：屏幕 UI 与状态灯正常；`nxrecorder` 录 `/dev/audio/pcm0c`、`nxplayer` 放 `/dev/audio/pcm0p`，能听到录音，即功放使能 GPIO 正常；AP `ping` 公网 10/10 按键（同一镜像）：KEY1/KEY2 单击 `hit`；KEY3（GPIO8）长按时 ai_agent 打出 `[ptt_button] PTT press → recording` 与 `PTT release → ASR`，期间 `/dev/audio/pcm0c` 采到 126 个块（峰值 1842–3108），即 GPIO8 两个边沿都经中断送到 `/dev/btn0`（ASR 失败是未配 API key，与 GPIO 无关）。按键丢边沿对比（都先跑 `ai_agent`，LVGL 在跑；数的是 `[key] missed edge`）：改动前 `8d11ea48…`：KEY1+KEY2 同按 14 次丢 0、单键 11 次丢 0；AP 用函数形式的 `29ad30f9…`：同按 4 次丢 3、KEY2 单击 9 次丢 1；AP 改宏后 `0cb717de…`（10-27 09:22 烧录）：同按 10 次丢 1、KEY2 单击 10 次丢 2、KEY3 丢 1 次按下沿，均由电平采样补回，手势照常触发，PTT press/release 正常。**未结论**：改宏后 AP 中断路径机器码已与改动前相同，CP 运行期也没有 GPIO 调用者，剩下的丢失找不到代码层面的来源；它要么是 10-14 记录过的老问题（`missed edge` 诊断正为此而加），要么与 LVGL 换屏时的负载有关（几次丢失都靠近 `idle rotate`）。样本小（改宏后 24 次事件丢 4，改动前 25 次丢 0），差异接近但不足以定论，需要同一方法、更大样本再对比 | 通过（剩余丢边沿另查） |
     | 定时器 | `/dev/timer1`（`CONFIG_BK7258_TIMER`，原 `CONFIG_BK7258_TIMER0` 改名，CP 默认开） | 2026-10-23 重写：删掉自带寄存器序列的旧 `/dev/timer0` 面；状态归 lower-half——每通道一条 `struct bk7258_timer_s`（`lower`、`isr`=原 `s_timer_isr[]`、`timeout_us`、上半部 `callback/arg`），`bk_timer_*` 读写同一张表；寄存器序列抽成 `bk_timer_program()`，两面共用；中断仍只由 `bk_timer_group_isr()` 分发，通道的 `isr` 槽指向 `bk7258_timer_tick()` 再调上半部回调。只暴露 `TIMER_ID1`：`ID0` 是 `bk_timer_delay_us()` 的自由计数器，`ID2` 是 1 s 喂狗，交给用户态会被 ioctl 停掉。导出：`timer_driver` 16/16、common `timer_hal` 7/7、bk7258 `timer_hal` 2/2（2026-10-26 补齐，对基线 `.o` 做 `comm -23` 为空）。契约头收归 `chips/bk7258/include/`：`driver/timer.h`、`driver/timer_types.h`、`timer_hal.h`，用 `sys_hal.c` 的编译参数预处理比对，声明与宏逐条相同（54/395/506 条声明、1786/2293/2349 个宏，差异 0），`-H` 确认解析到我们的头。寄存器路径只剩一条：`bk_timer_*` 全部改走 `timer_hal_*` / `timer_ll_*`，与基线分层一致；SYS 块的时钟门控、XTAL 选择、ICU 路由仍直接写 SYS 寄存器，归 T12 的 `sys_driver`。相对上一版的行为变化：编程通道时补上基线 `timer_hal_init_timer` 的中断状态清除；`bk_timer_delay_us` 改走 `timer_hal_delay_us`，每次读计数加 `rtos_enter_critical`；`timer_hal_cal_end_count` 按 SYS 的实际时钟选择算，不再写死 26 MHz。`nm -u` 0，`decode pass`。板上（镜像 `a21c3d6d…`，NuttX `timer` 示例，`/dev/timer1`，100 ms × 20）：`SETTIMEOUT` 后 timeout=100000；挂回调后 flags=2（HANDLER）；START 后每次采样 flags=3、`nsignals` 1→20 与采样逐一对应；STOP 后 flags=2、timeleft=0。`timeleft` 稳定在 99740–99855 µs：信号打断 `usleep`，所以采样都落在刚重装之后，计数器从 0 往上数时 `(end-cnt)/26` 正应接近满周期（若是倒数会接近 0），且数值有抖动，说明确实读到了计数器。 | 通过。10-26 `timer_hal` 改写后复测（镜像 `8d11ea48…`，烧录日志 15:52 `Writing Flash OK`）：`timer` 示例 flags 0→2→3→2、`nsignals` 1→20 逐次对应、STOP 后 timeleft=0，与改写前一致；同一镜像 `bk7258_drvtest wdg` 各行与上一轮相同，且 `initial` 的 `timeleft=7737` 说明 263 ms 前刚有一次 `bk_wdt_feed` 重装，即 `TIMER_ID2` 的 1 s 喂狗回调在新的中断清除路径上照常运行；同一镜像在 CP 上 `ping` 公网两轮各 10/10（`bk_timer_delay_us` 改走 `timer_hal_delay_us` 后 Wi-Fi 路径正常） |
     | 温度/电压 | `/dev/adc0` 未挂 | 随 T15 的 ADC | 未完 |
     | PSRAM | 无设备框架，标准面是堆区 | 2026-10-27：`psram_driver` + `psram_hal` 收成一个单元，寄存器走 `psram_ll_*`、SYS 位走 `sys_drv_psram_*`，早期初始化与 `bk_psram_init()` 共用 `psram_hw_init()`；29/29 导出，板上 `bk7258_psram_test` 两核分片均 PASS（见批次 14） | 通过（例外：AHBP 电调用 HAL 入口、通道位图用临界区、不建互斥量，理由见批次 14） |
     | Flash | `mtd_dev_s` 未挂 | — | 未完（T14） |

     被推翻的旧写法：本段原先把看门狗列为欠账、把 GPIO 写成"`/dev/gpio0` 未挂"、把 UART 写成
     "ops 调 `uart_ll_*` 而 Armino 面走 `uart_hal_*`"。前两条在代码补上后没有回写；第三条错在
     `uart_hal_*` 本身就是 `uart_ll_*` 的宏，两面在 ll 层已经是同一份，真正的缺口是 `bk7258_lowputc()`。
     同日还用过一个错误口径："第 5 条要求 Armino 面调 ops，ops 调 `bk_*` 算方向反了"，据此把 RTC、
     看门狗判为不通过。错在判了调用方向：上游 NuttX 的 lower-half 普遍是 ops 调厂商 HAL，
     第 5 条要防的是两份寄存器访问和走散的状态副本，不是谁调谁。不要再用这个口径。

## 机械验收命令

```bash
# 1) 导出集合：旧 ⊆ 新
arm-none-eabi-nm --defined-only -g <旧 .o> | awk '{print $3}' | sort -u > /tmp/old.txt
arm-none-eabi-nm --defined-only -g <新 .o> | awk '{print $3}' | sort -u > /tmp/new.txt
comm -23 /tmp/old.txt /tmp/new.txt        # 必须为空

# 2) 旧成员退出镜像
grep -c 'libarmino.*(<unit>\.c\.o)' nuttx.map     # 必须为 0

# 3) 符号提供者换人
awk '/\.a\([^)]*\)[[:space:]]*$/ {prov=$NF} /^[[:space:]]+0x[0-9a-f]+[[:space:]]+[A-Za-z_][A-Za-z0-9_]*$/ {print $2"\t"prov}' nuttx.map | grep -P '^<symbol>\t'

# 4) 无未解析
arm-none-eabi-nm -u nuttx | wc -l         # 必须为 0

# 5) 段属性（RAM 常驻函数）
# 6) 交付镜像必须走 full_flow.sh
#    build_worktree.sh 只重建 build/cp/nuttx，不刷新 package/all-app.bin，也不重写 manifest。
#    拿 build_worktree 之后的旧包去烧，门禁会报：
#      error: cp L1 report SHA-256 differs from the manifest
#      error: independent decoder rejected the current package

arm-none-eabi-objdump -t <新 .o> | grep <symbol>   # .itcm_sec_code 等要与基线一致
```

---

## 批次 1：看门狗 + 复位（`684eb2c` / 上游 `3accd85`）

替换 `wdt_driver.c`、`aon_wdt_driver.c`、`wdt_hal.c`、`aon_wdt_hal.c`、`reset_reason.c`、`reboot.c`，共 **43** 个符号，落在：

- `chips/bk7258/bk7258_wdt.c` —— 30 个（wdt_driver 17、aon_wdt_driver 5、wdt_hal 5、aon_wdt_hal 3）
- `chips/bk7258/bk7258_reset_reason.c` —— 13 个（reset_reason 11、reboot 2）

### 函数体要点

- 双看门狗：`CONFIG_INT_WDT` 8 s（硬件，喂狗间隔 `BK_MS_TO_TICKS(8000)>>4` = 500 ms）+ `CONFIG_TASK_WDT` 16 s（软件，超时 `BK_ASSERT(0)`）；
- 时间基是 **AON RTC 毫秒**（`bk_aon_rtc_get_us()/1000`），**不是**调度 tick —— 任务看门狗存在的意义就是发现“调度器停了”，不能和它共用一个时钟；
- 任务看门狗是**惰性**的：`s_last_task_wdt_feed_tick == 0` 时 `bk_task_wdt_timeout_check()` 整体不生效，直到第一次 `bk_task_wdt_feed()`。这个惰性是契约的一部分，不能“顺手 arm”；
- `.itcm_sec_code` / `__IRAM_SEC` 属性照抄（注意：本构建里 `__IRAM_SEC` 展开为空，`.itcm_sec_code` 段实际落在 flash，属性一致但没有 RAM 语义）；
- 喂狗节拍来自**我们的** `bk7258_timer.c`（TIMER_ID2 1 s 回调），这是跨单元契约；
- 复位原因写在 AON PMU R0：CP 在 bits[4:11]（8 位，>0xff 拒绝），AP 在 bits[24:30]（7 位，>0x7f 拒绝）；`reset_reason_init()` 读 R7A 锁存后把两只都清成 `RESET_SOURCE_POWERON`。

### 两个只有上板才发现的坑

1. **写 AON PMU R0 必须带 R25 两把钥匙。** `aon_pmu_hal_set_r0()` = 写 R0 → `set_r25(0x424B55AA)` → `set_r25(0xBDB4AA55)`。裸 `putreg32` 写 index 0 看起来成功、读回不变（原 board 代码就是这么写的，所以复位原因从来没记上）。证据：`wdttest reason 0x2a` 之后 `R0=R7A=0x2a0002a3`。
2. **`!RST r0=… cp=… ap=…` 不是 AON PMU R0 的读数。** 四次观察全 0，其中两次 R0 里确有值。唯一可用的读数是 `bk_misc_get_reset_reason()`（上板用 `bk7258_wdttest peek` 的 `cached=`）。

### 分层决定

芯片复位序列从 `board/` 下沉到 `chips/`：`board_reset()` 只决定“哪种 status 对应哪种复位”，寄存器归 `bk7258_chip_reset()`。**不要**用 `bk_wdt_force_reboot()` 做芯片复位：`CONFIG_NMI_WDT_EN=1` 时它走 NMI 分支并**无条件覆盖 warm tag**，会破坏需要清 tag 的 bootloader 冷启动路径。

### 上板证据（镜像 `05335514…`）

启动正常；静置数分钟不复位；`bk_wdt_stop()` 真正关狗；`reboot` 记 0x1、下次启动 `cached=0x01`；`reboot bootloader` 记 0x2、`cached=0x02`；force-reboot 整片复位后正常回来；`wlan0` 关联 + DHCP + ICMP 10/10。

---

## 批次 2：温度 / 电压检测

替换 `temp_detect.c`、`bk_sensor.c`、`volt_detect.c`，共 **26** 个符号（temp_detect 10、bk_sensor 7、volt_detect 9），落在：

- `chips/bk7258/temp_detect/bk7258_temp_detect.c` —— 采样、转换、值存储、控制面、电压 daemon
- `chips/bk7258/temp_detect/bk7258_sensor_cal.c` —— OTP 校准读取（单独 TU，原因见下）

### 函数体要点

- **OTP 校准必须保留**：`CONFIG_OTP_V1=1` 时 `bk_sensor_init()` 先读 GADC/SDMADC 校准对并编程 SARADC/SDMADC 校准寄存器。板上 SDMADC 为空（开机那行 `uncali sdmadc value` 就从这里来），但 GADC 有效，所以 SARADC 那条校准要执行；
- `bk_sensor_init()` 在 `CONFIG_PHY_MB=1` 下还要 `mb_phy_ipc_init()` + `bk_phy_server_init()`；
- 值存储是 `g_sensor_info`（beken 互斥 + `float`），未采到时是 `NAN`，读回 `BK_ERR_TRY_AGAIN`。这两个错误码是契约（`pm_misc.c`、`bk_phy_server.c` 按它判断）；
- 温度 1 s → 15 s 的间隔切换（`ADC_TMEP_DETECT_INTERVAL_CHANGE` 之后），每次采样调 `rwnx_cal_do_temp_detect()`；
- 电压采样的 **SARADC 睡眠投票顺序**：释放 → 采样 → 通知 PHY → 重新投票，必须保持；
- `temp_detect_change_configuration()` 的默认值规则（0 → 依次回落 15 s / 阈值 / dist）与“参数变化才重配”的门控要照抄。

### 值存储：真正的写入者是**闭源 `libbk_phy.a`**

第一版移植在 worker 里把原始 ADC 码（温度 560、电压 796）写进 `g_sensor_info`，上板读到
`stored_temp=559.0 stored_volt=796.0`，看着"有值"。当时在源码树里 grep 不到任何 setter 调用者，
于是得出"基线从不写入、getter 恒返回 TRY_AGAIN"的结论 —— **这个结论是错的**。

`arm-none-eabi-nm -u libbk_phy.a` 显示这个闭源库**引用**了两个 setter：

```
U bk_sensor_set_current_temperature
U bk_sensor_set_current_voltage
```

也就是说数据流是：我们采样原始码 → `rwnx_cal_do_temp_detect()` / `rwnx_cal_do_volt_detect()`
→ **闭源 PHY 换算成工程单位并回调 setter** → 值存储 → `pm_misc.c` 打印 / `bk_phy_server.c`
透传给 PHY IPC 请求。我们的第一版实现把闭源库写好的换算值**覆盖**成了原始码 —— 那才是偏差，
而且方向很坏：`pm_misc.c` 会按"温度 %.2f"打印出 559。

撤掉我们的写入后，板上读到的是闭源库自己写的值：

```
nsh> bk7258_wdttest sensor
wdttest: raw_temp=557 (ret=0) stored_temp=27.0 (ret=0) stored_volt=1.0 (ret=0) init=1
```

温度 27.0 ℃ 是这块板子的正常芯片温度；两条独立路径可以互校：OTP 出厂校准温度 569
（历史日志 97 次一致）对应的原始码比现在高 12 LSB（≈2.6 ℃），与"校准在约 29.6 ℃ 做、现在 27 ℃"
吻合。电压 1.0 是 PHY 自己的单位，本仓无法独立换算。

**教训（已写进本文件顶部的不变式之外，单独记一条）：**
"某个符号有没有调用者"必须查**闭源库**，只 grep 源码树会得到相反的答案。`nm -u <lib>.a`
是最便宜的判据，这次就是它翻案的。

### 与基线不同的两处（已确认、已记录）

1. **daemon 形态**：基线是“Beken 定时器 + 消息队列 + daemon 任务”，本移植是**自调度的 LPWORK worker**。因此 `temp_detect_send_msg()` 在 post 点就地执行，返回值语义保持（0 = 接受，`BK_FAIL` = 未建立）。
2. **3 个符号被 gc**：`temp_detect_send_msg`、`volt_daemon_restart`、`volt_daemon_stop` 在基线镜像里活着，只因为它们被 daemon 的消息分发引用；我们没有消息队列，所以它们没有引用者。**符号仍然全量提供**（26/26），只是镜像里被丢弃；反过来 `temp_detect_start` 因我们的 `volt_daemon_init()` 调用进入了镜像（基线里它没有引用者）。

### 编译期坑：两个 ADC 头文件家族不能进同一个 TU

- 本文件按基线 `driver/adc.h` 采样；`bk_private/bk_saradc.h`（OTP 校准时需要 `saradc_desc_t`/`saradc_set_calibrate_val`）与 `wifi/hal_port/include/driver/hal/hal_adc_types.h` 的枚举**互相重定义**；
- 更隐蔽的是：`wifi/hal_port/include/` 里也有一份 `temp_detect.h`，如果它先于基线目录被搜索到，我们的 TU 就会同时看到两套 ADC 类型。修法是在 `chips/bk7258/temp_detect/CMakeLists.txt` 里用 `target_include_directories(arch BEFORE PRIVATE …)` 把基线组件目录排到最前；
- 校准那半因此拆成独立 TU（`bk7258_sensor_cal.c`），它只包含基线头文件；`saradc_desc_t tmp_detect_desc` 也随它落位。

### 验收状态

- 26/26 导出符号、`nm -u` = 0、三个旧 archive 成员在 map 里计数为 0、静态 SRAM 287,224 B（比批次 1 后少 44 B）；
- 上板（测试镜像 `16518826…`）：`raw_temp=560 (ret=0)`，落在 `ADC_TEMP_VAL_MIN=10` ~ `ADC_TEMP_VAL_MAX=1365` 之间；`wlan0` RUNNING，`ping` 3/3；
- **定量对照（温度）**：历史日志里 `temp in otp is:569` 出现 97 次（闭源 `libbk_phy.a` 打印的 OTP 出厂校准温度，常数，不是实时读数）。今天实时 `raw_temp=560`，差 9 LSB；`ADC_TMEP_LSB_PER_10DEGREE=46` ⇒ 1 LSB ≈ 0.217 ℃ ⇒ **差约 2.0 ℃**（板子自热的量级）。两者是同一量纲、同一插值表的输入，所以这是一个定量吻合，不是“量级看起来合理”。
- **电压没有历史参照**：OTP SDMADC 为空（`uncali sdmadc value:[0 0]`，历史日志出现 90 次，与今天的行一致），所以历史上不存在电压读数可比。`raw_volt=796` 只能做区间检查（0~2048 的中段）；`_volt_detect_calculate_voltage()` 只是对 10 个样本剔掉 0/2048 后取平均，物理单位换算在闭源 `libbk_phy.a` 里，本仓无法独立换算。**这一项是未定量证明的。**
- 上板复核（测试镜像 `b0d18bad…`）：`raw_temp=557`、`stored_temp=27.0`、`stored_volt=1.0`、`ret=0`、`init=1` —— 值存储由闭源 `libbk_phy.a` 写入，与我们的采样路径一致。

## 批次 3：GPIO HAL

替换 `middleware/soc/common/hal/gpio_hal.c`（+ 只给它提供 inline 的 `gpio_ll.c`），共 **27** 个符号，
落在 `chips/bk7258/bk7258_gpio_hal.c`。

### 分层

`chips/bk7258/bk7258_gpio.c` 是 **AON GPIO 块的 owner**（我们自己的驱动 + `bk_gpio_*` 面），
这一批补的是同一块的**另一个方向**：其余基线单元调用的 Armino hal API。两者都走 `gpio_ll`，
所以寄存器实现只有一份。今天唯一活着的调用点是 `pm.c` 的 `gpio_hal_switch_to_low_power_status()`
（进睡眠前把所有 GPIO 置输入态）。

### 函数体要点

- 全部 27 个函数体照抄权威实现，仍建在权威的 `gpio_ll_*` inline 上；
- 引脚存在性检查用权威的 `GPIO_DEV_MAP` 表，错误码逐一对齐：`BK_ERR_GPIO_CHAN_ID`、
  `BK_ERR_GPIO_INVALID_ID`、`BK_ERR_GPIO_INTERNAL_USED`、`BK_ERR_GPIO_NOT_OUTPUT_MODE`、
  `BK_ERR_GPIO_NOT_INPUT_MODE`、`BK_ERR_GPIOS_MAP_NONE`、`BK_ERR_GPIO_BITS_NUM`、`BK_ERR_NULL_PARAM`；
- `gpio_hal_get_output()` 在非输出模式下返回 `BK_ERR_GPIO_NOT_INPUT_MODE`（权威就是这么说反的，
  照抄，不改“更合理”的版本）；
- `gpio_hal_func_unmap()` 发现引脚被占用时**只警告并继续**，不拒绝；
- `gpio_hal_set_output_value()` / `enable_interrupt()` 都先检查方向，方向不对直接返回错误。

### 编译期坑

`GPIO_DEFAULT_DEV_CONFIG` 在 `CONFIG_GPIO_DEFAULT_SET_SUPPORT=1` 且 `CONFIG_USR_GPIO_CFG_EN=1` 时
来自 **`projects/wifi/iperf/cp/config/bk7258/usr_gpio_cfg.h`**，不是 `gpio_map.h`（后者被
`#if !CONFIG_USR_GPIO_CFG_EN` 关掉了）。所以 CP 的头文件路径里要多一条项目配置目录。

### 验收状态

- 27/27 导出符号（旧集合 ⊆ 新集合，无差异）；`gpio_hal.c.o` / `gpio_ll.c.o` 在 map 里计数为 **0**；
- `nm -u` = 0；静态 SRAM 287,224 B（与本批前一致）；
- **镜像里 `gpio_hal_*` 符号数为 0 —— 基线镜像里同样是 0**（没有任何单元真正引用它们，
  唯一调用点 `pm.c:1870` 在关闭的配置分支里），所以这与基线一致，不是回归。

### H 层的进度

| 单元 | 我们的头 | ABI 检查 |
| --- | --- | --- |
| 看门狗 / AON 看门狗 | `chips/bk7258/include/driver/{wdt,aon_wdt}.h`，芯片 include 根已排到基线之前 | ✅ 预处理逐条相同（25 + 16 条声明） |
| 复位原因 / 重启 | `chips/bk7258/include/reset_reason.h`，已排到基线之前 | ✅ 预处理逐条相同（11 条声明 + 20 条宏） |
| 温度 / 电压 | 仍用 `temp_detect.h`、`volt_detect.h` | — |
| GPIO HAL | 仍用 `gpio_hal.h`（公开 API 暴露权威定义的 ABI 结构体，见下） | — |
| **AON RTC** | `chips/bk7258/include/driver/aon_rtc.h`，芯片 include 根已排到基线之前 | ✅ 预处理逐条相同（10 条声明） |

**为什么 `gpio_hal.h` 这类头暂时不接管**：它的 API 表面带结构体（`gpio_hal_t`、`gpio_map_t`）。
C 不允许两个不同的匿名结构体共用同一个 typedef 名，所以"我们的头与权威头逐条比对"这个判据对它们不成立；
接管只能靠"复制结构体 + 在两边 TU 里钉死 sizeof/offsetof"，保证弱一档。等这些结构体有独立类型头时再做。

切换前后 43 个符号在镜像里的"在/不在"与基线**零差异**，`nm -u` 为 0，静态 SRAM 未变。

**一条方法学补充：镜像哈希不能当作"行为未变"的证明。**
`chips/bk7258/wifi_armino/bk7258_armino_start.c:56` 里
`build_version[] = __DATE__ " " __TIME__;`，所以**任何一次真正重编都会改变镜像哈希**。
判断"改头没改行为"要看：43/26/27 个符号在镜像里的"在/不在"是否与基线一致、`nm -u` 是否为空、
`static SRAM` 是否未变 —— 本次三项全部不变（287,224 B）。

---

## 批次 4：UART（进行中）

两个单元，共 **67** 个符号：

| 单元 | 符号数 | 行数 | 状态 |
| --- | --- | --- | --- |
| `middleware/soc/common/hal/uart_hal.c` | 10 | 156 | ✅ 已替换 |
| `middleware/driver/uart/uart_driver.c` | 57 | 1754 | ✅ 已替换（见下） |

### 分层

`chips/bk7258/bk7258_uart.c` 是**我们自己控制台驱动**（NuttX 的 UART0）。这一批补的是 Armino 侧的
`bk_uart_*` 面：`printf_base.c`、`bk_cli`、`at_server`、`ate` 这些仍在镜像里的单元调用它。
先做 HAL（寄存器层），driver 面（57 个符号）随后。

### uart_hal 的函数体要点

- 初始化顺序照抄：关中断 → 模式 → 数据位/停止位 → 校验 → 波特率 → FIFO 阈值 → 停止检测时间 →
  流控 → 唤醒配置；
- **波特率分支按权威保留**：`CONFIG_SYSTEM_CTRL` 打开时用 `UART_SCLK_APLL` 走 120 MHz 分支，
  而 `#else` 分支比较的是 `UART_SCLK_DCO` —— 看着别扭，但这是硬件一直在跑的那条；
- 流控极性固定写 1/1、低水位固定 1；
- `uart_hal_get_system_interrput_en_status()`（权威就拼错了 interrupt）按 UART_ID_0/1/2 读
  `sys_ll_get_cpu0_int_0_31_en_*`，其它 id 返回 0。

### 控制台驱动改用同一层（第 1 步，已完成）

`chips/bk7258/bk7258_uart.c` 原本自己 `putreg32/getreg32/modifyreg32` 操作 UART0 寄存器，
现在 **12 处走 `uart_ll_*`**：

| 原操作 | 现在 |
| --- | --- |
| `modifyreg32(UART_CONFIG, … RX_EN)` | `uart_ll_enable_rx()` |
| `putreg32(0, UART_INT_ENABLE)` ×2 | `uart_ll_set_int_enable_status(…, 0)` |
| `putreg32(priv->ie, UART_INT_ENABLE)` ×3 | `uart_ll_set_int_enable_status(…, priv->ie)` |
| `getreg32(UART_FIFO_PORT) >> 8` | `uart_ll_read_byte()` |
| `FIFO_STATUS & RD_READY` / `WR_READY` / `bit17` | `uart_ll_is_fifo_read_ready()` / `is_fifo_write_ready()` / `is_tx_fifo_empty()` |
| `getreg32/putreg32(UART_INT_STATUS)` | `uart_ll_get_interrupt_status()` / `uart_ll_clear_interrupt_status()` |

剩下 2 处是 **SoC 中断路由**（`BK7258_SYS_CPU0_INT_EN`），属于另一个块、`uart_ll` 没有对应接口，保持原样。

替换前逐一核对过偏移与位定义与 `middleware/soc/bk7258/soc/uart_struct.h` 一致；
反汇编确认 `bk7258_uart_receive` 仍是 `ldr r0, [r3, #28]`（0x1c = FIFO port），与替换前同一寄存器。

### 验收（uart_hal）

- 10/10 导出符号，旧集合 ⊆ 新集合，无差异；
- `uart_hal.c.o` 在 `nuttx.map` 里计数 **0**；
- 镜像里 `uart_hal_*` 符号 **7 个，与基线完全相同**（其余 3 个在基线里也被 gc）；
- `nm -u` 为 0；静态 SRAM 287,224 B（未变）；43 个 WDT/reset 符号面零差异。

### uart_driver 的 57 个符号：契约表

**它不是死代码。** `bk_uart_driver_init()` 在 `middleware/driver/common/driver.c` 里被调用，而且
Armino 的日志通路就是它：

```
bk_printf / BK_LOGx  →  components/bk_system/printf.c + printf_base.c
        →  bk_uart_write_byte / uart_write_string   ← uart_driver.c
        →  uart_ll  →  寄存器
```

板上那些 `wifi:I(...)`、`wifi9d:I(...)`、`cal:W(...)` 行就是这么出来的，所以我们自己控制台的
`/dev/console` 与它共用同一个 UART0 硬件。**移植错了会先丢日志，再丢控制台。**

按调用者是否真在镜像里分组（`.o` 出现在 `nuttx.map` 才算）：

| 组 | 符号（节选） | 镜像内调用者 |
| --- | --- | --- |
| **主路径**（初始化/配置/读写） | `bk_uart_driver_init/deinit`、`bk_uart_init/deinit`、`bk_uart_set_baud_rate/data_bits/parity/stop_bits`、`bk_uart_set_enable_rx/tx`、`bk_uart_read_bytes`、`bk_uart_write_bytes`、`bk_uart_wait_tx_over`、`bk_uart_is_tx_over`、`uart_write_byte`、`uart_write_string` | `driver.c`、`printf_base.c`、`printf.c`、`command_line.c` |
| **中断面** | `bk_uart_register_rx_isr/tx_isr`、`bk_uart_take_rx_isr`、`bk_uart_recover_rx_isr`、`bk_uart_enable_rx/tx_interrupt`、`bk_uart_disable_rx/tx_interrupt`、`uart0/1/2_isr`、`uart_get_interrupt_status`、`uart_get_int_enable_status`、`uart_clear_interrupt_status` | `cli_main.c`、`shell_task.c` |
| **FIFO / 软 FIFO** | `bk_uart_enable_sw_fifo`、`bk_uart_disable_sw_fifo`、`uart_get_length_in_buffer`、`bk_uart_set_rx_full_threshold`、`bk_uart_set_tx_empty_threshold`、`bk_uart_set_rx_timeout` | `cli_main.c`、`cli_uart.c`（后者不在镜像里） |
| **PM 钩子** | `bk_uart_pm_backup`、`bk_uart_pm_restore`、`uart_id_to_pm_uart_id` | `shell_task.c`、`at_task.c`；前两者在本构建里是 `return BK_OK` 空壳（`CONFIG_UART_PM_CB_SUPPORT` 未定义，反汇编已证） |
| **ATE / FR 旁路** | `uart_write_byte_for_ate`、`uart_write_byte_for_fr`、`bk_uart_get_ate_detect_gpio`、`bk_uart_get_rx_gpio` | `ate.c`、`at_task.c` |
| **DMA** | `bk_uart_is_rx_dma_enabled` | 无外部调用者 |
| **杂项** | `bk_uart_disable_rx/tx`、`bk_uart_disable_hw_flow_ctrl`、`bk_uart_set_hw_flow_ctrl`、`bk_uart_is_in_used`、`uart_read_byte`、`uart_read_byte_ex`、`uart_read_ready`、`uart_write_ready`、`uart_clock_enable/disable`、`uart_wait_tx_over`、`print_hex_dump` | `print_hex_dump` 被 wpa 的 `ctrl_iface.c` 用；其余无外部调用者 |

必须保留的语义要点（实现时逐条对）：

1. **`bk_uart_driver_init()` 幂等**，首次调用时初始化 ISR 表、统计并从 `bk_printf_init()` 起日志通路；
2. `bk_uart_init()` 通过 `uart_hal_init_uart()` 下发配置，`bk_uart_deinit()` 走 `uart_hal_stop_common()`；
3. **TX 阻塞语义**：`bk_uart_write_bytes()` 与 `bk_uart_wait_tx_over()`/`bk_uart_is_tx_over()` 的"等 FIFO 空"判据要一致（日志行不能截断）；
4. **ISR 注册表**：`bk_uart_register_rx_isr(id, fn, arg)` 的回调签名与调用上下文（中断里、不清中断状态由驱动做）；
5. **软 FIFO**：`enable_sw_fifo` 之后 `uart_get_length_in_buffer()` 的读数语义；
6. **ATE 旁路**：`uart_write_byte_for_ate/for_fr` 用的不是同一个写路径（历史遗留），别合并；
7. `print_hex_dump` 是 wpa 的调试输出，输出格式（每行字节数、前缀）必须一致。

### uart_driver 的实施顺序（每段独立可回滚）

1. 主路径 + 中断面（日志与控制台必需）；
2. FIFO/软 FIFO + 杂项；
3. DMA / ATE / FR / PM 空壳；
4. 全 57 个到位后，`uart_driver.c` 从 `excluded_units` 出局。

---

## 批次 5：日志传输（`printf.c` + `printf_base.c`）

26 个符号（`printf.c` 20 + `printf_base.c` 6），落在 `chips/bk7258/bk7258_printf.c`。

### 为什么从这一层动手

`BK_LOGx` 展开成 `bk_printf_ext(level, tag, fmt, ...)`，而**两个闭源库也直接调它**
（`libwifi.a` 引用 `bk_printf_ext`/`bk_printf_raw`；`libbk_phy.a` 另加 `bk_get/set_printf_sync`）。
所以这一层是**所有厂商日志的唯一入口**，包括闭源库内部的日志。

原链路（`CONFIG_SHELL_ASYNCLOG=1`）：

```
BK_LOGx → bk_printf_ext → printf.c 组前缀 → shell_log_out_port（bk_cli 环形缓冲）
        → shell_log_flush → bk_uart_write_bytes → uart_driver → uart_ll
```

新链路：

```
BK_LOGx → bk_printf_ext（我们的）→ 组同样的前缀 → syslog()
```

于是：日志通道收敛到 NuttX；`printf.c`/`printf_base.c` 出局；**`uart_driver` 不再是日志的必经之路**。

### 保住了什么

- 线上格式不变：`[cpu0:][tag:]E|W|I|D|V(ms):`（历史日志可比）；
- 级别过滤、按模块禁用与白名单（`bk_disable_mod_printf`/`bk_enable_white_list`/`bk_white_list_state`）、
  `bk_set_printf_enable`、`bk_set/get_printf_sync`、`CONFIG_PRINTF_BUF_SIZE`（256）截断；
- `printf_is_init` 门控、`bk_printf_deinit` 里 `printf_lock_deinit()` 的调用顺序。

### 有意改变的一处

`bk_printf_init()` **不再调用 `bk_uart_init()`**。UART0 归 NuttX 控制台驱动（`bk7258_uart.c`），
日志层再去初始化一次会把控制台的端口重新编程。`bk_get/set_printf_port()` 仍记录端口号（API 语义），
但不再触碰硬件。

### 验收

- 26/26 导出符号（旧集合 ⊆ 新集合）；`printf.c.o`/`printf_base.c.o` 在 map 里计数 **0**；
- `nm -u` 为 0；静态 SRAM 287,136 B（比替换前少 88 B）；
- 43 个 WDT/reset 符号面零差异；
- 上板后发现并修掉两处：前缀时间戳单位（`CONFIG_USEC_PER_TICK=1000`，tick 即毫秒，初版误除成秒）与
  `bk_sensor_init()` 重复调用（`components_init.c:271` 已拥有，我们在 `temp_detect_init` 里又调了一次，
  导致 `uncali sdmadc value` 每次启动打印两遍）；
- **一处镜像差异**：`printf_lock_deinit` 不在镜像里（导出仍在 `.o` 里）。原因是它函数体无副作用，
  GCC 把唯一调用优化掉后符号被收集；基线里它活着只是因为那次调用跨 TU、编译器无法判定。
  不为一个 map 条目添加假的副作用。
- 待上板：日志仍按要求格式出现、控制台正常。

### uart_driver 移植工作表（源码已通读，实现未开始）

**本构建实际编译的部分**（用于估算工作量，**不是裁剪依据**）：`CONFIG_UART_RX_DMA` / `TX_DMA` 未定义 ⇒
DMA 路径不编译；`CONFIG_UART_PM_CB_SUPPORT` 未定义 ⇒ `pm_backup/restore` 是两个 `return BK_OK`；
`CONFIG_UART0_FLOW_CTRL` 未定义 ⇒ `uart_init_gpio` 的流控分支不编译；`CFG_CLI_DEBUG` 未定义；
`CONFIG_SPE=1` ⇒ `UART_CHECK_SECURE()`（`DEV_IS_SECURE()` 断言）保留。

**但实现必须 follow 配置，不按当前配置裁剪。** 权威里的 `#if CONFIG_UART_RX_DMA` / `CONFIG_UART_TX_DMA` /
`CONFIG_UART_PM_CB_SUPPORT` / `CONFIG_UART0_FLOW_CTRL` / `CFG_CLI_DEBUG` 分支要**原样保留**（连同 DMA 辅助函数、
`uart_id_to_dma_dev`、`uart_rx_dma_read_fifo_frame` 等），当前不编译只是当前配置的结果。
理由与 `bk7258_wdt.c` 保留 `CONFIG_INT_AON_WDT` 段、`bk7258_printf.c` 保留
`CONFIG_SYS_PRINT_DEV_MAILBOX` 分支一致：打开某个配置组合时不能撞到"缺实现"。
按当前配置裁剪等于把契约面从 57 个符号扩成"57 个符号 + 若干配置组合"，属于隐藏的契约退让。

**镜像里真正是活的只有 7 个符号**（`nuttx.map` 实测）：`bk_uart_driver_init`、`uart_id_to_pm_uart_id`、
`bk_uart_pm_backup/restore`（空壳）、`bk_uart_get_rx_gpio`、`bk_uart_get_ate_detect_gpio`、`uart_wait_tx_over`。
其余 50 个当前没有引用者（Armino CLI 已被我们的 compat shell 切掉输入，日志已改走 syslog）。
验收基线因此是：**替换后镜像里仍是同样的 7 个符号**。

**状态结构**（照抄）：
- `uart_driver_t { uart_hal_t hal; uint8_t id_init_bits; uint8_t id_sw_fifo_enable_bits;
  uint32_t pm_backup[UART_PM_BACKUP_REG_NUM]; uint8_t pm_bakeup_is_valid;
  bool rx_dma_enable; dma_id_t rx_dma_id; bool tx_dma_enable; dma_id_t tx_dma_id; }`
- `s_uart[SOC_UART_ID_NUM_PER_UNIT]`（`hal.hw` 分别指向 `SOC_UART0/1/2_REG_BASE`）
- `s_uart_rx_isr[] / s_uart_tx_isr[]`（`{callback, param}`）、`s_uart_rx_kfifo[]`、`s_uart_sema[]`（`{rx_blocked, rx_int_sema}`）
- `s_last_uart_rx_isr`（`take/recover` 用）

**分片与依赖**：

| 片 | 内容 | 依赖 |
| --- | --- | --- |
| 1 | `uart_clock_enable/disable`、`uart_interrupt_enable/disable`、`uart_init_gpio`、`uart_deinit_{tx,rx}_gpio` | `sys_drv_dev_clk_pwr_up`、`sys_drv_int_enable/disable`、`gpio_dev_map/unmap`、`bk_gpio_pull_up` |
| 2 | `uart_id_init_kfifo`、`uart_id_{init,deinit}_common`、`bk_uart_init/deinit`、`driver_init/deinit` | `kfifo_alloc/free`、`uart_statis_*`、`rtos_init/deinit_semaphore`、`bk_pm_sleep_register_cb` |
| 3 | 配置组：baud/data_bits/stop_bits/parity/enable_rx/tx/threshold/rx_timeout/flow_ctrl | `uart_hal_set_*`、`clk_get_uart_clk` |
| 4 | 读写组：`uart_write_byte/string/ready`、`bk_uart_write/read_bytes`、`read_byte(_ex)`、`get_length_in_buffer`、`wait_tx_over`、`is_tx_over` | `uart_hal_*`、`kfifo_*`、`rtos_get/set_semaphore` |
| 5 | 中断组：`register/take/recover_{rx,tx}_isr`、`uart_isr_common`、`uart0/1/2_isr`、`uart_get/clear_interrupt_status`、`isr_register_functions` | `bk_int_isr_register(INT_SRC_UARTn)`、`UART_STATIS_*` |
| 6 | 旁路：`enable/disable_sw_fifo`、`get_ate_detect_gpio`、`get_rx_gpio`、`write_byte_for_ate/for_fr`、`print_hex_dump`、`is_in_used`、`is_rx_dma_enabled`、`pm_backup/restore` 空壳、`uart_id_to_pm_uart_id` | 无 |

**必须照抄的行为**（容易漏）：
1. `uart_write_byte()` 先等 `fifo_write_ready` 再写；
2. `uart_write_string()` 在 `\n` 前补 `\r`（除非前一个已经是 `\r`）；
3. `uart_read_fifo_frame()` 在 kfifo 满时**仍然继续读空硬件 FIFO**（否则死循环），只是不 put；
4. `bk_uart_read_bytes()` 两条路：软件 FIFO 打开 → kfifo + 信号量等待（`rx_blocked` 门控、超时返回
   `BK_ERR_UART_RX_TIMEOUT`）；未打开 → 直接轮询 `uart_read_byte_ex()`；
5. `bk_uart_take_rx_isr()` 会先关软 FIFO、再换回调；`recover` 时若 `id == bk_get_printf_port()` 则不重新打开软 FIFO；
6. `bk_uart_wait_tx_over()` 用 `tx_fifo_count + 1` 与 `UART_CLOCK/(clk_div+1)` 估算等待微秒，再 `bk_delay_us()`；
7. `bk_uart_init()` 的顺序：关 TX 中断 → 清状态 → 注册 ISR → `uart_hal_init` → 中断使能 → 时钟/GPIO/kfifo/sema →
   选时钟源 → `uart_hal_init_uart` → `uart_hal_start_common`；并且**已初始化就直接返回 OK**；
8. `bk_uart_deinit()` 逆序：`uart_id_deinit_common`（停 HAL、复位配置、关中断/时钟、释放 kfifo 与信号量）。

**实现顺序**：1→2→3→4→5→6，每片写完单独编译（不接线），57 个齐了再一次性 exclude `uart_driver.c`。

### uart_driver 的实现记录

落在 `chips/bk7258/bk7258_uart_driver.c`（1942 行，含全部 `#if` 分支）。**57/57 导出符号，与权威逐条相同**；
`uart_driver.c.o` 在 map 里计数 0。

所有配置分支照抄：`CONFIG_UART_RX_DMA` / `CONFIG_UART_TX_DMA` 的 DMA 全套、`CONFIG_UART_PM_CB_SUPPORT` 的真实
backup/restore、`CONFIG_UART0_FLOW_CTRL` 的 CTS/RTS 复用、`CFG_CLI_DEBUG`、`CONFIG_SPE` 的
`UART_CHECK_SECURE()`。当前配置下 DMA/PM 分支不参与编译，但代码在。

**镜像里仍然是同样的 7 个活符号**（与替换前一致）：

```
bk_uart_driver_init  bk_uart_get_ate_detect_gpio  bk_uart_get_rx_gpio
bk_uart_pm_backup    bk_uart_pm_restore           uart_id_to_pm_uart_id
uart_wait_tx_over
```

**13 个符号从镜像里消失，原因是日志批的连带效应，不是丢失**：`bk_uart_init/deinit` 在基线里唯一的调用者是
`printf_base.c` 的 `bk_printf_init()`（它会 `bk_uart_init(print_port, &cfg)` 重新配置 UART0）。
日志改走 syslog 后我们特意不再从日志层初始化 UART，这条链就断了，于是
`uart_enter_deep_sleep`（它调 `bk_uart_set_enable_rx/tx`、`bk_uart_disable_tx_interrupt`）、
`uart_isr_register_functions`（它引用 `uart0/1/2_isr`）、以及 `uart_write_byte/string`、
`uart_read_byte_ex`、`uart_clock_enable/disable` 一起失去引用。**导出集合完整**，这些路径当前不可达。

顺带确认 `uart_wait_tx_over` 不依赖被 gc 的初始化：`uart_hal_wait_tx_over()` 是
`uart_ll_wait_tx_over()` 的宏，内部直接读 UART0/1/2 的寄存器，不用 `s_uart[]` 的状态。

验收：`nm -u` 0；静态 SRAM 287,136 B（未变）；WDT 批 43 符号零差异。

---

## 批次 6：移除 Armino CLI 与触摸驱动（不是替换，是删除）

### 为什么删

`components/bk_cli` 从 shell_uart 被替换后就**没有输入设备**了：它照样在启动时被
`bk_init.c:210/214` 初始化、注册一整张命令表、拉进二十来个 `cli_*.c`，但没有人能敲命令进去。
日志改走 syslog 之后，它连"异步日志出口"这个唯一实际作用也没了。

### 排除它需要补的 7 个符号

删 `bk_cli` 后链接器一次列全了缺口（`shell_assert_out` 114 次、`shell_log_flush` 107 次、
`shell_cmd_ind_out` 10 次、`shell_set_log_cpu` 6 次、`shell_set_uart_port`/`shell_set_log_level`/
`bk_cli_init` 各 1 次），由 `compat/bk7258_armino_shell_api.c` 提供：

| 符号 | 我们的实现 |
| --- | --- |
| `shell_log_flush()` | 空操作 —— 日志已同步写 syslog，没有环形缓冲要 flush |
| `shell_assert_out()` | **不是空操作**：走 `up_putc()` 直写控制台（等价于权威的 `emergency_uart_write_buf`），`bContinue == false` 时保持"停在这里" |
| `shell_cmd_ind_out()` / `shell_set_log_cpu()` | 空操作（没有 CLI 提示符、没有日志归属核） |
| `shell_set_uart_port()` | 转发 `bk_set_printf_port()`，与权威一致 |
| `shell_set_log_level()` | 存值；现在没有代码按它过滤 |
| `bk_cli_init()` | 返回 0（kNoErr） |

`at_server` **必须保留**：`bk_init.c:275/333` 在启动时调 `at_server_init()`/`atsvr_app_init()`，
它的命令设备是 `shell_uart`（由 `compat/bk7258_armino_shell.c` 提供）。曾经尝试连 `at_server` 一起排除，
链接器立刻要 `at_server_init`、`atsvr_app_init`、`at_svr_env` 等 8 个符号，所以它不是叶子。

### 连带收益：触摸驱动可以整体删除

`touch_driver_v1_1` 唯一的引用者是 `cli_pwr.c` 的 `bk_touch_register_touch_isr`。`bk_cli` 一走，
这个引用消失，整个触摸控制器驱动（438 行、25 符号）随之可以排除——**不需要移植**，因为这块板子
从不驱动触摸面板。

### 实测影响

| 指标 | 清理前 | 清理后 | 变化 |
| --- | --- | --- | --- |
| 镜像符号数 | 8,210 | 7,756 | **−454** |
| 静态 SRAM | 287,112 B | 276,704 B | **−10,408 B** |
| `.text` | ~1,060 KB | 1,003,324 B | **−~57 KB** |
| `libarmino_bk_cli` 成员 | 10 | 0 | 全出局 |
| `touch_driver_v1_1` 成员 | 1 | 0 | 出局 |
| `nm -u` | 0 | 0 | — |
| 43 个 WDT/reset 符号面 | — | — | 零差异 |

### 验收状态（已上板）

镜像 `232d99a0…`（`all-app.bin`，源码 mtime 早于打包时间），CP `nsh>` 实测：

| 检查项 | 结果 |
| --- | --- |
| 启动日志格式 | `cpu0:tag:LEVEL(ms):` 保持，毫秒时间戳正确 |
| 关联流程 | `wapi psk` / `wapi essid` → scan → assoc → 4-way → DHCP 全通 |
| 地址 | `[ARMINO] wlan0 ip 192.168.110.166` |
| 外网 | `ping www.baidu.com` 4/4，26–48 ms |
| `nm -u` | 0 |
| `libarmino_bk_cli.a` 成员 | 0 |
| 静态 SRAM | 266,624 B |

**唯一有意改变的一行日志**：`cpu0:cli:I(...):BK STA got ip4` 不再出现。它由被删的
`cli_netif.c` 打印，是 CLI 的回调，不是协议栈的输出；DHCP 本身由
`[ARMINO] wlan0 ip …` 与 `[KW:]me dhcp done vif:0` 证明。

### 为什么"符号少了 454 个"不等于风险

判断顺序必须是结构性的，不是计数：

1. **穷举**：静态链接 + `--gc-sections` 下，任何被保留的代码引用被删符号都会在链接期报
   `undefined reference`。链接通过且 `nm -u` 为 0 ⇒ 没有幸存代码依赖它们。
2. **反向**：被删的东西在运行时原本做什么。`bk_cli_init()` 承担三件事 —— 注册命令表
   （无输入设备）、创建 `shell_task` 线程（消费 shell UART 输入 + 异步日志环）、
   `create_log_handle_task()` 创建 `log_hanlder` 线程（排空同一个环）。日志改 syslog 后
   环没有生产者，后两件事的消费者已无对象。
3. **残留引用逐条核**：`shell_log_out_port` 的唯一引用在 `platform_stub.c.o`（该段被 gc）；
   `create_log_handle_task` 只在被排除的归档内部；`shell_log_flush` 真正的活调用者是
   `at_server`，已给 no-op。**符号总数不能当判据**，它随 gc 两个方向都动。

---

## 批次 7：`stack_base` + `dd`（5 个符号，零活符号）

两个都是"读导出集合就知道规模"的小单元，一起做是因为彼此无关、风险都接近零。

### `stack_base.c` → `chips/bk7258/bk7258_stack_base.c`（3 符号）

`code_addr_is_valid` / `stack_mem_dump` / `arch_parse_stack_backtrace`，**镜像里 0 个活的**。
它们是异常 dump 的**读半边**；写半边（`arch_is_enter_exception`、`bk_system_dump`、dump hook）
仍在 `trap_base.c` 里，那是后续批次。

保留的分支：`stack_mem_dump` 里的 `#if CONFIG_DEBUG_VERSION || CONFIG_DUMP_ENABLE`（打印过程中
喂 WDT：`bk_task_wdt_feed()` / 异常态下 `bk_wdt_force_feed()`）。当前两个宏都未定义，该分支不参与
编译，但代码在。`code_addr_is_valid` 的判定是"奇数地址 + 在 flash text / ITCM / IRAM 内"，权威里
还有一版更严格的（要求前一条指令是 bl/blx）已被注释掉，照原样保留。

构建期需要一处新 include：`<driver/flash_partition.h>` → `partitions.h` → 生成的
`CONFIG_APPLICATION_PARTITION_OFFSET`，所以 chip 目标的 include 列表加了
`middleware/boards/bk7258/partitions` 与 `build/bk7258/iperf/partitions` 两个目录。

### `dd.c` → `chips/bk7258/bk7258_dd.c`（2 符号）

`g_dd_init` / `g_dd_exit`，**这张表在当前配置下是空的**，而且这是忠实而不是遗漏：
`CONFIG_SOC_BK7236XX` 已定义 ⇒ 选小表；小表唯一的条目又要 `CONFIG_FLASH_ORIGIN_API`，它未定义。
于是 `sizeof(dd_init_tbl) == 0`，循环计数 0，两个函数立刻返回。镜像里只有 `g_dd_init` 是活的
（`driver.c:312` 调用）。

大表的九个分支（sctrl / flash-origin / DSP / mailbox / BT / FFT / IrDA / MAC-PHY-bypass / SD 卡）
全部保留，每个分支连同它需要的头文件一起走同一个 `#if`，配置一翻就落到完整表而不是空表。

### 验收

| 项 | 结果 |
| --- | --- |
| 导出符号 | **5 / 5**，与权威逐条相同 |
| 机器码 | **5 个函数逐字节相同**（14 / 29 / 38 / 16 / 15 条指令） |
| `stack_base.c.o` / `dd.c.o` 在 map | 均 **0** |
| 镜像内活符号 | `g_dd_init` 仍在（与替换前的 1 个一致），其余 4 个仍被 gc |
| `nm -u` | 0 |
| 静态 SRAM | 266,624 B（**未变**） |
| `.text` | 1,003,324 → 1,003,332 B（+8，对齐） |

---

## 批次 8：`driver`（启动编排，2 个符号）

`middleware/driver/common/driver.c` → `chips/bk7258/bk7258_driver.c`。

`driver_early_init()`（中断控制器 / AON PMU / TRNG / eFuse）与 `driver_init()`（全部驱动初始化）都在
启动路径上，**两个都是活符号**。这个单元没有逻辑，只有顺序，所以移植的唯一要求就是顺序与门控完全
一致：GPIO 先于 ATE（ATE 借用 UART TX 脚做检测）与 UART；UART 起来之前不许打日志；mailbox 的
`ipc_init` → `mb_ipc_init` → `bk_ipc_init` 三步；PSRAM 在最后（这条是本仓库对基线做过的唯一本地改动，
注释一并保留）。

配置门控的实际取值：`CONFIG_POWER_CLOCK_RF` **未定义** ⇒ 150 行的 `power_clk_rf_init()` 整段不编译
（函数保留在同一个 `#if` 里，连同一个非 `static` 链接属性）；`CONFIG_CHIP_SUPPORT`、`CONFIG_SPI`、
`CONFIG_I2C`、`CONFIG_QSPI`、`CONFIG_SDIO_HOST`、`CONFIG_CALENDAR`、`CONFIG_TOUCH_PM_SUPPORT`、
`CONFIG_HW_ROTATE_PFC`、`CONFIG_LIN`、`CONFIG_TRUSTENGINE` 均未定义。

两处构建层面的注意：

- **`CONFIG_SPI` 同名冲突由本文件自己处理**。NuttX 的 `<nuttx/config.h>` 定义 `CONFIG_SPI` 指它自己的
  SPI 层，而 Armino 这个名字的意思是"Armino SPI 驱动被编译进来了"，它没有。基线单元靠
  `compat/bk7258_armino_nuttx_config.h` 解决，chip 层 TU 只能自己 `#undef`。这也让
  `nuttx_config_compat_components` 里的 `driver` 条目失去了意义，一并清空（列表本身保留，注释说明原因）。
- **`bk_ipc_init()` 用本地 extern 声明，不引入 `bk_api_ipc.h`**。那个目录（`middleware/driver/mailbox`）
  的兄弟头文件会与 `mb_ipc/armino/middleware/driver/mailbox/mailbox_driver_base.h` 重复定义
  `MAILBOX0..3` 与整个 `mailbox_*_t` 家族——凡是这两个头文件家族进同一个 TU 就报 redeclaration。
  签名取自 `bk_api_ipc.h:135`。

新增 include 目录三个：`middleware/driver/common`（dd_pub.h / drv_model.h）、`components/easy_flash`
与 `components/easy_flash/easy_flash_V4.X/inc`（`bk_ef.h` → `easyflash.h`）。

### 验收

| 项 | 结果 |
| --- | --- |
| 导出符号 | **2 / 2**（`driver_early_init`、`driver_init`） |
| 机器码 | **两个函数逐字节相同**（22 B / 128 B） |
| `driver.c.o` 在 map | **0** |
| 镜像内 | 两个符号仍由 `arch/libarch.a(bk7258_driver.c.o)` 提供 |
| `nm -u` | 0 |
| `.text` / `.data` / `.bss` | 1,003,332 / 10,080 / 266,624 B —— **与批次 7 后完全相同** |

批次 7 + 8 合并上板（镜像 `50ce970f…`）：

| 检查项 | 结果 |
| --- | --- |
| `driver_init` 的 PSRAM 段 | `cpu0:I(138):driver_init: psram init ok` |
| EasyFlash（`driver_init` 调用） | `cpu0:ef:I(98):EasyFlash V4.1.0 is initialize success.` |
| Wi-Fi bring-up | `cpu0:wifi:I(431):wifi inited(1) ret(0)` |
| AP 侧 | `[AP] stage=scheduler-running`、console attach、`nsh-ap>` 就绪 |
| 日志格式 | `[ts] cpu0:tag:LEVEL(ms):` 不变 |
| 堆起点 | `H28096CE8:00008A18`（0x8A18 = 35,352 B，较 CLI 批前 +10,424 B） |

---

## 批次 9：`drv_model`（设备模型，12 个符号）

`middleware/driver/common/drv_model.c` → `chips/bk7258/bk7258_drv_model.c`。

`DD_MAX_DEV` 个槽位的设备表（状态、引用计数、`DD_OPERATIONS *`）加上 open/close/read/write/control
分发。句柄是"槽位号 + 高半字魔数 `0xA5A50000`"，所以每个入口都先验魔数与下标。临界区用权威的
`GLOBAL_INT_DISABLE/RESTORE`（即 `rtos_disable_int()` / `rtos_enable_int()` 的保存-恢复对）——状态和
引用计数的更新在同一次关中断里完成，ISR 与任务之间不会丢更新。

`ddev_control()` 比其它入口多一道 `idx >= DD_MAX_DEV` 检查：它与 `ddev_get_id_from_handle()` 的
`DD_DEV_TYPE_END` 上界重复，但类型值落在 `DD_DEV_TYPE_END` 与 `DD_MAX_DEV` 之间的设备会越界读表，
所以照抄保留。

活符号：`drv_model_init`、`ddev_get_id_from_handle`、`ddev_control`。

### 编译期坑：第二个 `drv_model_pub.h`

`wifi/hal_port/include/` 里有一份**精简副本**，没有 `DD_DEV_TYPE_END` 和 `DD_HANDLE_MAGIC_MASK` /
`ID_MASK`；而 `drv_model.h` 的 `DD_MAX_DEV` 正是 `(DD_DEV_TYPE_END & 0xFFFF)`。arch 目标的 include 列表
原本不含 `bk_private/legacy`，于是 `"drv_model_pub.h"` 解析到 hal_port 那份，编译报
`DD_DEV_TYPE_END undeclared`。修法：把 `middleware/driver/include/bk_private/legacy` 加到 CP 块里，
位置在 hal_port 之前。legacy 目录只有 5 个条目，与 hal_port 唯一同名的就是这一个头文件。

（这是与 `temp_detect.h` 同一类的影子头问题：**先看第二份副本是谁，再改 include 顺序**。）

### 验收

| 项 | 结果 |
| --- | --- |
| 导出符号 | **12 / 12** |
| 机器码 | **12 个函数逐字节相同**（4–144 B） |
| 目标文件 | 权威 694 / 0 /364；我们 694 / 0 /364 —— 三段全同 |
| `drv_model.c.o` 在 map | **0** |
| `nm -u` | 0 |
| `.text` / `.data` / `.bss` | 与批次 8 后完全相同 |

---

## 批次 10：`arm`（CM33 核面，9 个符号，94 行）

`middleware/arch/cm33/arm.c` → `chips/bk7258/bk7258_arm.c`。

`arch_init()` 与 `smem_reset_lastblock()` 在权威里就是**空函数**（2 字节 `bx lr`），照抄；`arch_init`
之所以是活符号，是 `trap_base.c` 引用它。其余是核级原语：`arch_sleep`/`arch_deep_sleep`（WFI 加/清
SLEEPDEEP，分别放在 `.iram` 与 `.itcm_sec_code` 段）、`check_IRQ_pending`（64 位挂起掩码）、
`arch_atomic_{set,set_return,clear}` 与 `arch_fence`（LDREX/STREX、DSB/ISB）。

**段属性照抄**，即使本 port 的 `flash.ld` 没有 ITCM 区：权威也是这么编的，去掉属性等于改变链接器
放置这段代码的自由度。

新增两个 CMSIS include 目录（`CMSIS_5/CMSIS/Core/Include`、
`CMSIS_5/Device/Beken/armstar`）：`SCB`、`__LDAEX/__STREXW/__STL`、`NVIC_GetAllPendingIRQ` 都在那里，
arch 目标原先不含 CMSIS。

### 验收

| 项 | 结果 |
| --- | --- |
| 导出符号 | **9 / 9** |
| 机器码 | **9 个函数逐字节相同**（2–28 B） |
| `arm.c.o` 在 map | **0** |
| 镜像内 | `arch_init`(0x…a4) 与 `smem_reset_lastblock`(0x…a6) 相邻且各 2 字节 —— 与权威一致 |
| `nm -u` | 0 |
| `.text` | 1,003,332 → 1,003,340 B（+8，对齐） |

---

## 批次 11：`sys_audio_driver`（音频系统控制面，42 个符号）

`middleware/driver/sys_ctrl/sys_audio_driver.c` → `chips/bk7258/bk7258_sys_audio_driver.c`。

42 个函数，没有一个 `#if`，全是同一个三步形态：取 sysctrl 临界区 → 调一个 `sys_hal_*` 寄存器助手
→ 释放 → `SYS_DRV_SUCCESS`。其中两个（`sys_drv_aud_select_clock`、`sys_drv_aud_clock_en`）在写寄存器
前后用 `sys_amp_res_acquire()`/`release()` 包住，因为改音频时钟或 PLL 分频要占着模拟资源。

活符号 3 个：`sys_drv_apll_en`、`sys_drv_aud_audbias_en`、`sys_drv_aud_aud_en`（音频设备注册路径）。
`sys_hal_*` 助手本身未动。

这一批的 42 个函数体是**机械重排**生成的（去掉制表符、改成 2 空格缩进、注释换成 NuttX 风格），而不是
手抄——手抄 42 个四行函数只会引入笔误，而机器码比对会把任何笔误解释成"不同"。

### 验收

| 项 | 结果 |
| --- | --- |
| 导出符号 | **42 / 42**，集合差异为空 |
| 机器码 | **42 个函数逐字节相同** |
| `sys_audio_driver.c.o` 在 map | **0** |
| 三个活符号 | 由 `arch/libarch.a(bk7258_sys_audio_driver.c.o)` 提供 |
| `nm -u` | 0 |
| `.text` | 1,003,340 → 1,003,348 B |

批次 9–11 合并上板（镜像 `383fb680…`）：启动序列与批次 7/8 逐行一致（`psram init ok`、`EasyFlash success`、
`wifi inited(1) ret(0)`、AP `scheduler-running`），`wapi` 关联 → DHCP `192.168.110.166`，
`ping www.baidu.com` **10/10，21–38 ms**。CLI 批之后 `BK STA got ip4` 那一行不再出现，与此前记录一致。

---

## 批次 12：`trap_base`（异常与 dump 核心，22 个符号，864 行）

`middleware/arch/cm33/trap_base.c` → `chips/bk7258/bk7258_trap_base.c`。

这个单元只在其它东西都失效时才运行，所以它的结构由"此刻还能信什么"决定：不用锁、不用分配器、不用
日志队列；输出走 `emergency_uart_putc()` 直写 FIFO 寄存器，用有界自旋等"FIFO 满"位。CP 与 AP 不能
同时 dump——所有权是共享 SRAM 固定区（`FIXED_ADDR_*`，`ram_regions.h` → `CONFIG_PWR_MNG_ADDR+32..52`）
里的 Peterson 锁加首尾魔数：先到者封区、拿锁、dump、直接重启且不释放；后到者在 `wfe()` 里永久等待。
区域损坏则 `shared_repair()` 重封；连重封都失败才走兜底（写 `EXCEPTION_DUMPER`，读不回来自己 id 的
那个永久等待）。六个 `s_cpuN_int_*_en_value` 快照先读各核中断使能寄存器再清零，让 dump 看到故障时刻
的状态。

关闭的分支全部保留：`CONFIG_MEMDUMP_ALL`（外设与 SRAM 全 dump）、`CONFIG_DEBUG_VERSION` /
`CONFIG_DUMP_ENABLE`（整个 `rtos_dump_system` 体）、`CONFIG_AP_PSRAM_HEAP_*`、
`CONFIG_AON_PMU_REG0_REFACTOR_DEV`，以及只在 `CONFIG_SOC_BK7236XX` 下编译的寄存器窗口。

### 两个编译期陷阱

1. **`-fsigned-char`**。权威整棵 Armino 树带这个开关。缺了它，`emergency_uart_write_string()` 里
   `*string` 从 `ldrsb.w`（带符号加载）变成 `ldrb`——字节 ≥0x80 时进 FIFO 的值不同。
   `set_source_files_properties()` **不生效**：`arch` 目标是在 `arch/arm/src` 创建的，源文件属性受
   目录作用域限制。正解是 `set_property(SOURCE … TARGET_DIRECTORY arch PROPERTY COMPILE_OPTIONS
   "-fsigned-char")`。**规则：移植涉及 `char` 值的 Armino 单元必须带上基线的 `-fsigned-char`。**
2. **`cm_backtrace.h`** 会拉进 `cmb_def.h` → `FreeRTOS.h`（不该进 chip 层 TU），本文件只用了一个原型，
   改成按 `cm_backtrace.h:43` 的签名本地声明。

### 验收

| 项 | 结果 |
| --- | --- |
| 导出符号 | **22 / 22** |
| 指令 + 重定位 | **21 / 22 逐条相同** |
| 唯一差异 | `bk_null_trap_handler` 里 `BK_ASSERT(0)` 的 `__LINE__`（860→912），行号固有 |
| `trap_base.c.o` 在 map | **0** |
| 13 个活符号 | 全在；`g_wdt_handler_lr` 与 6 个快照在 `.bss` |
| `nm -u` | 0 |
| `.text` | 1,003,348 → 1,003,356 B |

### 上板（探针，镜像 `aa0c6332…`）

探针 `bk7258_drvtest trap` 走完整环路：封区 → 抢 Peterson 锁（内部 `__disable_irq()`）→ 读回所有权
→ 重新封区 → 恢复中断 → 直写 UART。连跑两次结果相同，证明可重复、不留残留：

```
trap: lock -> enter=1 ap=0; after repair -> enter=0trap: data_addr_is_valid(sram+0x100,+0x200)=1 data_addr_is_valid(0,32)=0
trap: int_en snapshots cpu0=00000000/00000000 cpu1=…/… cpu2=…/…
trap: emergency uart path ok
trap: done (interrupts restored, shared area re-sealed)
```

### 探针本身的 bug（已修，值得记）

第一版探针用 `up_irq_save()/up_irq_restore()` 包住 `arch_set_enter_exception()`，结果**整块板子带着
`PRIMASK=1` 继续跑**，随后 HardFault。原因：**这个 port 的 `up_irq_save/restore` 走 BASEPRI
（`mrs/mrs BASEPRI`，掩码 0x80），而 `__disable_irq()` 走 PRIMASK**，两套掩码互不相干，restore 归还不了
PRIMASK。改成 `getprimask()/setprimask()` 后正常。

真实调用者不受影响：`user_except_handler()` 以 `bk_reboot_ex()` 结束、从不返回，所以权威那个
`__disable_irq()` 不需要归还——**只有"跑完还要继续用板子"的探针才会把它暴露成故障**。教训：
在这个 port 里混用 `up_irq_save` 与 `__disable_irq` 会漏掉一个掩码。

---

## 批次 13：排除 `at_server`（不是替换，是删除；T17 收尾）

`components/at_server` 整块出局：6 个成员（`_at_server.c` / `at_server.c` / `atsvr_core.c` /
`atsvr_port.c` / `_atsvr_func.c` / `at_task.c`）、1,697 行、52 个导出符号、39 个镜像内符号。

### 为什么是删而不是移植

和批次 6 的 CLI 同一个理由，而且这次有原文可引：我们的 `compat/bk7258_armino_shell.c` 顶部写着
"output through up_putc(), **no input** (NuttX's NSH owns the console input)"。`at_server` 的命令设备与
事件设备就是这个 `shell_uart`，所以**没有 AT 命令会进来，也没有东西能出去**。它的 7.5 KB 代码
仍在开机时被初始化（`bk_init.c:275/334`），只是空转。

### 需要补的 8 个符号

`compat/bk7258_armino_shell_api.c`（与 CLI 那批同一个文件）：

| 符号 | 我们的实现 |
| --- | --- |
| `at_server_init()` / `atsvr_app_init()` | 空 / 返回 `ATSVR_OK` |
| `atsvr_register_commands(...)` | 返回 `ATSVR_OK`。没有传输层，拒绝注册只会让调用方记录一个它无法处理的失败 |
| `atsvr_cmd_rsp_ok/error()`、`atsvr_output_msg()`、`atsvr_event_sender()` | 空——没有地方可发 |
| `_at_svr_env` | **真存储**（全零）。`wifi_at.c` 取它的地址并读字段，全零正是"从未初始化的 at_server"会留下的状态 |

`wifi_at.c` 与 `bk_system_at.c` **保留**：它们是调用方，不是服务端。

### 实测影响

| 指标 | 清理前 | 清理后 | 变化 |
| --- | --- | --- | --- |
| `.text` | 1,003,356 B | **997,472 B** | **−5,884 B** |
| `.data` | 10,080 B | 10,068 B | −12 |
| `.bss` | 266,624 B | 266,456 B | −168 |
| `libarmino_at_server` 成员 | 6 | 0 | 全出局 |
| `nm -u` | 0 | 0 | — |

### T17 状态

`uart_driver`（批次 4）、`stack_base` / `dd` / `driver` / `drv_model` / `arm` / `trap_base`（批次 7–12）、
`sys_audio_driver`（批次 11）、`sys_touch_driver`（批次 7 前）、`touch_driver_v1_1` 与 `at_server`
（排除）——**T17 的单元按前 4 条全部收敛完毕**（2026-10-23 按第 5 条复核：GPIO 两面未合一、
UART 剩 `bk7258_lowputc()`，见文首"当前状态"表），其中 10 个以"逐字节相同"收尾，2 个以"无传输层、整体删除"收尾。

批次 13 上板（镜像 `309486ca…`）：启动序列与前一批逐行一致（`psram init ok`、`EasyFlash success`、
`wifi inited(1) ret(0)`、AP `scheduler-running`、`nsh-ap>` 就绪），没有 AT 相关报错。堆起点
`H28096C30:00008AD0`（较前 +184 B，与 at_server 的 `.bss` −168 B 及对齐相符）。

---

## 阶段二第二批：T13 RTC（第 1 步，`aon_rtc_hal` 28 个符号）

`cp/middleware/soc/common/hal/aon_rtc_hal_64bit.c` → `chips/bk7258/bk7258_aon_rtc_hal.c`。

这一层是纯转接：每个函数从 `hal->id` 解析块基址，再调一个 `aon_rtc_ll_*` 内联，寄存器布局仍留在权威的
`aon_rtc_ll.h` / `aon_rtc_struct.h`。`aon_rtc_hal_t` 是这一块的实现细节，照旧留在权威头里（与 `uart_hal_t`、
`gpio_hal_t` 同一个 struct-typedef 墙）。

它上面是 RTC 驱动（tick、闹钟链表、两路唤醒），再往上接 PM、flash-notify、SARADC-notify 的 64 位 tick
读取——所以这一层先落地，驱动单元（`aon_rtc_driver_64bit.c`，1,390 行 / 15 符号 / 6 活）留到下一步。

### 验收

| 项 | 结果 |
| --- | --- |
| 导出符号 | **28 / 28**，集合差异为空 |
| 指令 + 重定位 | **28 / 28 逐条相同** |
| `aon_rtc_hal_64bit.c.o` 在 map | **0** |
| 镜像内活符号 | 11 个全部仍由 `arch/libarch.a(bk7258_aon_rtc_hal.c.o)` 提供 |
| `nm -u` | 0 |
| `.text` / `.data` / `.bss` | 997,472 / 10,068 / 266,456 B —— **与替换前完全相同**（纯转接层的预期） |

---

## 阶段二第二批（下）：AON RTC 挂上 NuttX 框架，以及本批的 H 层

### 一份实现，一个兼容面

口径修正（用户 2026-10-22）：不是"两个平行的面"，而是**一份实现 + 一个 Armino 形状的兼容面**。
逐模块把所有调用方改成标准 ops 不现实（`pm` / `low_pwr` / `rosc_ppm` / `flash_notify` /
`saradc_notify` / `wifi_adapter` 六处跨三个子系统），兼容面正是让迁移可以分批做的原因。
但**标准 ops 那一侧必须语义完整且正确**，不能是"探针够用"的浅实现。

```
rtc_ops_s (/dev/rtc0, up_rtc_* → CLOCK_REALTIME)
        │
        ├──► bk_aon_rtc_get_us() ──► aon_rtc_hal ──► aon_rtc_ll ──► 寄存器
        ▲
bk_aon_rtc_*（pm / low_pwr / rosc_ppm / flash_notify / saradc_notify / wifi）
```

`chips/bk7258/bk7258_rtc.c` 里**直接读寄存器的次数为 0**：`rdtime` 调的就是原生化的
`bk_aon_rtc_get_us()`。所以两个面落在同一份实现上，不存在两套时间逻辑。

### 框架接入

- `rtc_ops_s` 实现 `rdtime` / `settime` / `havesettime`，经 `rtc_initialize(0, …)` 暴露为 `/dev/rtc0`；
- 同一张 ops 表经 `up_rtc_set_lowerhalf()` 交给 `arch_rtc.c`，于是 `up_rtc_getdatetime()` /
  `up_rtc_settime()` 自动成立，`CLOCK_REALTIME` 与 `clock_settime()` 都落到这里；
- `up_rtc_initialize()` 由我们提供（返回 `OK`；计数器本身由驱动 init 启动）。

四个配置开关缺一不可，这是踩出来的：`CONFIG_RTC`（框架头）→ `CONFIG_RTC_DRIVER`（`rtc_lowerhalf_s`
与 `/dev/rtc0` 上半部）→ `CONFIG_RTC_DATETIME`（日历时间，并会拉入 `up_rtc_*`）→
`CONFIG_RTC_ARCH`（`arch_rtc.c` 这个桥，缺它 `up_rtc_*` 全无定义）。

**注意这是新增能力，不是契约变更**：Armino 那 16 + 28 个符号一个没少、签名没动、语义没动；
新增的是平台的第二个面（`/dev/rtc0`、`CLOCK_REALTIME`），Armino 的 `4 条不变式` 约束的是被替换的单元。

### H 层：`driver/aon_rtc.h` 已接管

按规则第 3 条补做（这一批最初漏了）：

- 文件落在与权威头相同的相对路径 `chips/bk7258/include/driver/aon_rtc.h`，用 NuttX 的 include guard 与排版；
- 证明：`header_equiv.py` 用同一套编译参数分别预处理两边，**10 条声明逐条相同**；
- 替换效果：芯片 include 根已排在基线 `-I` 之前，`<driver/aon_rtc.h>` 现在解析到我们的文件；
- "改头不改行为"的三项证据：`.text` / `.data` / `.bss` = **1,003,296 / 10,076 / 266,464 B**，与换头前
  完全相同；符号面零差异；`nm -u` 为 0。

### 还没做的（RTC 的最后一块）

**alarm 面没有接框架。** `bk_alarm_register/unregister` 仍是自己那份链表 + tick 比较器，
而 `rtc_ops_s` 的 `setalarm` / `rdalarm` / `cancelalarm` 未实现。也就是说 `pm` / `low_pwr_*` 的深睡唤醒
还走纯 Armino 路径。这块要和 T12（PM）一起做才合理：那份链表就该成为 NuttX alarm 面的实现，
`bk_alarm_*` 退化为薄封装。

### ops 的语义完整性（逐个方法）

| `rtc_ops_s` 方法 | 实现 | 语义 |
| --- | --- | --- |
| `rdtime` | ✅ | 计数器 + epoch → 日历字段（秒级） |
| `settime` | ✅ | 入参在 `CONFIG_RTC_DATETIME` 下是**日历**（不是秒数），用 `timegm` 换算；只移动 epoch，从不写计数器；**并重排已挂的 alarm** |
| `havesettime` | ✅ | 恒 true（软件 epoch 可写） |
| `setalarm` | ✅ | 绝对时间 → 剩余 tick → 驱动的按名链表，一次性（`period_cnt=1`） |
| `setrelative` | ✅ | 转成绝对时刻后走同一条路 |
| `rdalarm` | ✅ | 返回设定的绝对时间；未挂时 `-ENOENT` |
| `cancelalarm` | ✅ | 按名注销，清状态 |
| `setperiodic` / `cancelperiodic` | ❌ | `CONFIG_RTC_PERIODIC` 未开，框架不会调用 |
| `destroy` / `ioctl` | ❌ | 可选方法，框架对 NULL 有检查 |

两条必须写下来的语义要点：

1. **alarm 是绝对时刻，不是延时。** 所以 `settime` 之后挂着的 alarm 必须重排——这一点最初漏了：
   alarm 被换算成 delta 写进硬件，时钟一改，它就会在错误的墙钟时间触发。现在 `settime` 会调
   `bk7258_rtc_arm()` 重算。
2. **µs 单调计数不在框架语义里。** `rdtime` 只有秒（未开 `CONFIG_RTC_HIRES`，而且 32.768 kHz 的计数器
   当系统定时器不合适）。所以 `bk_aon_rtc_get_us()` 不能改成"包一层 ops"：`flash_notify` /
   `saradc_notify` / `wifi_adapter` 要的是 µs/ms 单调量，那部分只能留在驱动里。这是兼容面存在的原因，
   不是偷懒。

**两个上板才发现的语义点**（2026-10-22）：

1. `settime` 的入参形状随配置变。开了 `CONFIG_RTC_DATETIME`，框架与 `arch_rtc.c` 的 `up_rtc_settime`
   都先 `gmtime_r` 再调 ops，所以进 ops 的是**日历字段**；我最初把 `tm_sec` 当成 epoch 秒，结果是
   `RTC_SET_TIME` 返回 0 但时间变成 1970（`tm_sec == 0`）。修法：`timegm` 整组日历。
2. **写 `/dev/rtc0` 不会移动内核的 `CLOCK_REALTIME`。** 在 NuttX 里这是两个对象：内核时钟在启动时由
   RTC 播种一次（`clock_basetime` → `up_rtc_getdatetime`），之后只有 `clock_settime()` 会经
   `up_rtc_settime` 回到我们的 ops。所以验收要分两条：设备口做 `RTC_SET_TIME`/`RTC_RD_TIME` 往返，
   内核口做 `clock_settime`/`clock_gettime` 并确认 `/dev/rtc0` 跟着变。

**观测到的一个边界**：BK7258 只有一个 AON RTC 单元（`AON_RTC_ID_2` 仅在 `SOC_AON_RTC_UNIT_NUM > 1` 时
存在），框架 alarm 与 `pm` / `low_pwr` 的唤醒 alarm 共用同一个单元的按名链表。

---

## 阶段二第三批：T14 Flash/MTD（第 1 片，`flash_hal`）

`cp/middleware/soc/common/hal/flash_hal.c` → `chips/bk7258/bk7258_flash_hal.c`（36 行 / 2 符号）。

从最底层开始：这个单元只做两件事——从 `hal->id` 解析块基址交给 `flash_ll_*` 内联，以及把状态寄存器
里的保护位解码成一个值。上面的 `flash_partition` / `flash_server` / `flash_notify` / `flash_driver` /
`flash_driver_ext` / `flash_bypass` 依次跟进，PSRAM 两个单元走 NuttX 内存侧而不是 MTD。

**这一族的 NuttX 落点是 `mtd_dev_s`**（`drv->erase/write/read/byteinfo/imaxgeo/erasesize`，`mtd_register()`
挂成 `/dev/mtdN`），所以底层先落地，MTD 驱动就架在它上面；分区表随后由 `mtd_partition` 生成，
文件系统可以直接用 littlefs。

### 验收

| 项 | 结果 |
| --- | --- |
| 导出符号 | **2 / 2** |
| 指令 + 重定位 | **2 / 2 相同** |
| `flash_hal.c.o` 在 map | **0** |
| `nm -u` | 0 |
| `.text` / `.data` / `.bss` | 与替换前完全相同 |

**操作教训（2026-10-22）**：源码改完后，用于烧板的镜像必须由 `full_flow.sh` 产出。`build_worktree.sh`
只重建 ELF，`package/all-app.bin` 与 manifest 仍停在上一版，于是完整性门禁报
`cp L1 report SHA-256 differs from the manifest` / `independent decoder rejected the current package`。
症状是门禁拒绝，不是镜像损坏——重跑 `full_flow.sh` 即恢复（本次 `decode pass` + `integrity gate: pass`）。

---

## 阶段二第三批：看门狗的标准面（`/dev/watchdog0`）

`chips/bk7258/bk7258_wdt.c` 尾部新增一节 NuttX face：`watchdog_ops_s` 的
`start / stop / keepalive / getstatus / settimeout`，`watchdog_register("/dev/watchdog0")`。
**方法体全部落在原有的 `bk_wdt_start/stop/feed` 上**——这就是第 5 条不变式：Armino 符号是兼容面，
`/dev/watchdog0` 是标准面，两边同一份实现。

暴露的是**中断看门狗**（`CONFIG_INT_WDT`，默认 8 s），因为它是唯一会真的咬的那只；任务看门狗是
1 s 定时器钩子里的软件截止检查。

`getstatus` 的 `timeleft` 与 `flags`（`WDFLAGS_ACTIVE | WDFLAGS_RESET`）都取自**同一个记账变量**
`s_wdt_last_feed_tick`：它由系统的周期喂狗点 `bk_wdt_feed_handle()`（`bk_timer` 每 1 s）以及本设备的
`keepalive`/`start`/`settimeout` 共同写入。

第一版用的是 per-device 的 `last_feed`，上板 `timeleft` 直接读 0——因为系统的 1 s 钩子走的是
`bk_wdt_feed()`，不经过这个设备，`last_feed` 永远是 0，`elapsed` 就等于开机时长。**标准面必须报告硬件
真正所处的状态，不能自己猜一份。**

**用之前要知道的一件事**：`bk7258_timer.c` 的 1 s 钩子本来就在调 `bk_wdt_feed()`，所以硬件始终被系统
喂着——`KEEPALIVE` 只是立刻补一次，`STOP` 才是真的解除武装。因此这个设备按**诊断**用，不要让应用
去当宠物喂它。

### 验收

| 项 | 结果 |
| --- | --- |
| 框架进镜像 | `watchdog_register` 在符号表，`CONFIG_WATCHDOG=y` |
| `.text` | 1,009,168 → 1,010,900 B（+1,732：框架 + 我们 5 个 ops） |
| `nm -u` | 0 |
| 探针 | `bk7258_drvtest wdg` → GETSTATUS / SETTIMEOUT / GETSTATUS / KEEPALIVE，不碰 STOP |

---

## 阶段二第三批：GPIO 的标准面（`/dev/gpio0`）

`chips/bk7258/bk7258_gpio.c` 尾部新增 `gpio_operations_s` 与 `gpio_pin_register()`：
`go_read` → `bk7258_gpio_read()`，`go_write` → `bk7258_gpio_write()`。**ops 的方法体就是这一层原有的
读写函数**，没有第二套寄存器路径——第 5 条不变式。

引脚归属交给板级：`bk7258_gpio_pin_register(dev, minor)` 收引脚、方向、minor 号，默认只暴露
**GPIO40（红色状态灯）**。选它的理由是"写错的代价可见且无害"，而且它是板上唯一已知的输出；
它与 `bk7258_status_led.c` 共用这个脚，所以**探针读完会写回原值**，这不是所有权声明。

### 验收

| 项 | 结果 |
| --- | --- |
| 框架进镜像 | `gpio_pin_register` 在符号表，`CONFIG_DEV_GPIO=y` |
| `.text` | 1,010,900 → 1,013,220 B（+2,320：gpio 上半部 + 我们的 ops） |
| `nm -u` | 0 |
| 探针 | `bk7258_drvtest gpio` → read / write1 / read / write0 / read / 还原 |

**上板发现的一处语义错误（已修）**：第一版 `go_read` 调的是 `bk7258_gpio_read()`，而它与它镜像的
Armino `bk_gpio_get_input/get_output` 一样，报的是**配置寄存器里的使能位**，不是电平。于是
`write(0)` 之后读回仍然是 1 —— 因为输入使能位根本没变。正确的读回对象是**输出锁存位**
`BK7258_GPIO_CFG_OUTPUT`，那才是驱动实际输出的电平。

这也是第 5 条的一个实例：ops 的方法体不能随便挑一个"名字看起来对"的函数，必须挑**语义上对**的那个。
输入脚要读焊盘时没有现成 API（`gpio_hal_get_input` 是使能位），这一点要在扩展多引脚时补。

上板（镜像 `e5317a9b…`）：`bk7258_drvtest wdg` →

```
wdg: flags=0x3 timeout=8000 timeleft=7791
wdg: SETTIMEOUT(10000) -> 0
wdg: after set timeout=10000
wdg: KEEPALIVE -> 0
```

`timeleft=7791` = 8000 − 209 ms，即距上一次 1 s 喂狗的实际间隔，证明它读的是真实喂狗点而不是自己猜的
计数。`flags=0x3`（ACTIVE|RESET）、`SETTIMEOUT` 真的改了周期。

---

## 阶段二第三批：T14 第 2 片 `flash_partition`

`cp/middleware/driver/flash/flash_partition.c` → `chips/bk7258/bk7258_flash_partition.c`（426 行 / 9 符号）。

分区名 → {地址, 大小, 选项} 的表（生成在 `partitions_gen.h`）、地址与写权限检查，以及把逻辑偏移翻译成
物理地址的 read/write/erase 入口。两处值得写下来：

- **物理/逻辑地址不是简单相加**：每 34 个物理字节承载 32 个逻辑字节
  （`FLASH_PHYSICAL_ADDR_UNIT_SIZE` / `FLASH_LOGICAL_ADDR_UNIT_SIZE`），落在每组低 32 之外的地址是被
  **拒绝**而不是换算的；
- **写权限是每个分区的选项位**，偏移入口与按地址入口都要检查。

这一层是 MTD 的天然来源：分区表直接喂 `mtd_partition`，下面的 read/write/erase 已经很像 `mtd_dev_s` 的
方法体。flash 驱动单元替换之前，它仍站在 Armino 的 `bk_flash_*` 上。

### 第三次踩同一个坑：影子头

`wifi/hal_port/include/` 里也有一份 `flash_driver.h`——没有 `FLASH_MAX_SIZE`、`FLASH_API_MAGIC_CODE`、
`FLASH_SECTOR_SIZE_OFFSET`，而且它对 `flash_op_status_t` 的定义与基线版冲突（编译器报
`conflicting types`）。修法与 `temp_detect.h`、`drv_model_pub.h` 一致：把
`middleware/driver/flash` 排到 hal_port 之前。

**这条现在可以算规律了**：`wifi/hal_port/include/` 是一套"精简副本"集合，替换任何跨到它的单元时先查
同名头，再决定 include 顺序。

### 验收

| 项 | 结果 |
| --- | --- |
| 导出符号 | **9 / 9** |
| 指令 + 重定位 | **9 / 9 相同** |
| `flash_partition.c.o` 在 map | **0** |
| `nm -u` | 0 |
| `.text` | 1,013,220 → 1,013,732 B（+512：分区表与检查逻辑的少量重排） |

上板（镜像 `250b5cb4…`）：`bk7258_drvtest gpio` →

```
gpio: read(before)=0
gpio: write(1) -> 0
gpio: read(after write 1)=1
gpio: write(0) -> 0
gpio: read(after write 0)=0
gpio: restored to 0
```

电平跟着写走，读回的是真实锁存值。**RTC / 看门狗 / GPIO 三个标准面全部上板验证完毕**（`/dev/rtc0`
含 alarm 与内核面、`/dev/watchdog0`、`/dev/gpio0`）。

---

## 阶段二第三批：T14 第 3 片 `flash_driver`（核心，30 符号）

`cp/middleware/driver/flash/flash_driver.c` → `chips/bk7258/bk7258_flash_driver.c`（1,053 行 / 30 符号）。

SPI flash 命令层（读/写/擦、状态寄存器、QE、line mode）、建在状态寄存器上的**保护位状态机**，以及每个
操作都必须走的跨核协调（`mb_flash_op_prepare/finish`——控制器是 CP/AP 共用的）。

两条给后来者的性质：

- **每个操作都被 `mb_flash_op_prepare()/finish()` 夹住**，这不是可选项，AP 也在用同一个控制器；
- **保护配置写在 flash 自己的状态寄存器里**，这里写错可能让器件进入只有上电或解锁序列才能恢复的保护态。
  所以本文件不含任何推测性改动。

### 验收

| 项 | 结果 |
| --- | --- |
| 导出符号 | **30 / 30**，集合差异为空 |
| 指令 + 重定位 | **30 / 30 逐条相同** |
| `flash_driver.c.o` 在 map | **0** |
| `nm -u` | 0 |
| `.text` | 1,013,732 → 1,013,672 B（−60，重排） |
| 门禁 | `decode pass` + `package integrity gate: pass` |

上板（镜像 `3cf190fd…`）：启动第一屏就是三个标准面注册成功

```
[BK7258] /dev/gpio0 registered OK
[BK7258] /dev/watchdog0 registered OK
[BK7258] /dev/rtc0 registered OK
...
cpu0:ef:I(120):EasyFlash V4.1.0 is initialize success.
cpu0:I(152):driver_init: psram init ok
cpu0:wifi:I(440):wifi inited(1) ret(0)
```

EasyFlash 初始化成功这一行是关键证据：它读 ENV 分区，走的是**我们已经替换的** `flash_partition` +
`flash_driver`。

---

## 阶段二第三批：T14 第 4 片 `flash_driver_ext`

`cp/middleware/driver/flash/flash_driver_ext.c` → `chips/bk7258/bk7258_flash_driver_ext.c`（527 行 / 7 符号）。

快速擦除、按字拷贝助手、**BLE 与 Wi-Fi 共享控制器时的分片写**，以及"射频正在连接时先把 flash 操作挡住"
的 BLE 睡眠钩子。它继承下层（`flash_driver` / `flash_server`）的队列与锁纪律——这正是写操作不会撞上射频
的原因。

| 项 | 结果 |
| --- | --- |
| 导出符号 | **7 / 7** |
| 指令 + 重定位 | **7 / 7 相同** |
| `flash_driver_ext.c.o` 在 map | **0** |
| `nm -u` | 0 |
| `.text` | 1,013,672 → 1,013,680 B（+8，对齐） |

---

## 阶段二第三批：T14 第 5 片 `flash_bypass`

`cp/middleware/driver/flash/flash_bypass.c` → `chips/bk7258/bk7258_flash_bypass.c`（1,237 行 / **1 个导出**）。

`flash_bypass_op_write()` 手工接管 SPI 控制器：存下 SPI 与时钟寄存器、软件复位该块、自己驱动命令字节流、
再全部还原——用于驱动层处在"无法走正常操作"的状态时仍能发一条 flash 命令。它的调用者是我们已替换的
`flash_driver.c` 的深睡进/出路径（`:978`、`:997`），闭源库不引用它。

### 一个被证据否掉的想法：只搬导出的那个函数

那个函数是自包含的（只用 `REG_READ/REG_WRITE` 与 `rtos_disable_int()`），文件其余 1,200 行都是它自己的
备选命令路径、static、出构建后不可达。所以第一版只搬了它——**源码逐行相同（129 行，去注释零差异），编译
结果却不同**：

```
权威:  sub sp, #20   … mov r6,r2 / mov r8,r0 / mov r7,r1 / bl rtos_disable_int / ldr r3,[pc,#352]
我们:  sub sp, #28   … mov r7,r1 / mov r6,r2 / mov r8,r0 / bl rtos_disable_int / mov r9,r0
```

多 8 字节栈、多一个活跃寄存器。原因不是源码也不是 flag——**用同一套 flag 编译权威源能逐字节复现构建产物**，
说明是编译上下文：整文件里它有三个同 TU 的调用者，GCC 的跨过程寄存器分配因此给出不同的寄存器/栈形状。
把整文件搬过来就**逐字节相同**了。

**结论**：`static` 助手"没人用"不等于"可以不搬"；判断标准只能是"搬过来的这份 TU 编译出什么"。

| 项 | 结果 |
| --- | --- |
| 导出符号 | **1 / 1** |
| 指令 + 重定位 | **1 / 1 相同**（整文件搬运后） |
| `flash_bypass.c.o` 在 map | **0** |
| `nm -u` | 0 |
| `.text` | 与上一片相同（该函数此前已在镜像里） |

---

## 阶段二第三批：T14 第 6 片 `flash_server` + `flash_notify`

| 单元 | 行数 | 导出 | 镜像内活符号 |
| --- | --- | --- | --- |
| `flash_server.c` | 560 | 3（`bk_flash_svr_init/deinit`、`calc_crc32_xor`） | 1（`bk_flash_svr_init`） |
| `flash_notify.c` | 213 | 7（operate-status 两个、`mb_flash_ipc_init`、`mb_flash_op_prepare/finish`、注册/注销通知） | 3（`mb_flash_ipc_init`、`prepare`、`finish`） |

第 5 片学到的教训直接套用：**两个文件都整份搬运**，不抽函数。验收 3/3 与 7/7 逐字节相同。

`flash_notify` 那三个活符号是整个 flash 族的纪律所在——每个 flash 操作前后都要
`mb_flash_op_prepare()` / `mb_flash_op_finish()`，因为控制器是 CP/AP 共用的；`mb_flash_ipc_init()`
在建链时把这条通路搭起来。

| 项 | 结果 |
| --- | --- |
| 导出符号 | **3 / 3** 与 **7 / 7** |
| 指令 + 重定位 | 两组全部相同 |
| 两个成员在 map | 均 **0** |
| `nm -u` | 0 |
| `.text` | 与上一片相同（两族符号此前已在镜像里） |

---

## 判据升级：从"逐字节相同"到"MMIO 序列相同"

### 为什么要换

前 20 多个单元用的是**搬运 + 逐字节证明**：函数体原样搬过来、只改排版，机器码与权威对象逐字节相同。
这只满足不变式 1、2，**不变式 3（C 写法原生 Vela 化）没有做到**——`GLOBAL_INT_*`、`UINT32`、`os_*`、
`BK_LOG*` 全都留在原地。用户 2026-10-22 指出："两个文件都整份搬运，那不就是没有原生化了吗"——成立。

而第 5 条规则原话是"ops 的**方法体就是原 Armino 的寄存器写法**"，说的是**寄存器写法**，不是源码文本。
所以重写函数体是允许的、也是要求的；判据应该是"同样的寄存器访问序列与语义"，不是"同样的机器码"。

### 新判据与工具

`my_docs/bk7258-wifi/tools/mmio_seq.py`（仓库外）比对两个对象里同名符号的：

- **数据访问序列**：`ldr/str/ldrb/strb/...` 的**偏移量**与**顺序**（寄存器编号不参与比较——重写允许分配不同）；
- **字面量池加载**：MMIO 基地址就是从这儿来的，等于在比"访问的是同一块寄存器"；
- **调用目标**：`bl <symbol>` 的符号名。

用法：`mmio_seq.py <旧.o> '*' -- <新.o> '*'`，退出码 0 表示全部一致。

### 工具自证：它在已知相同的样本上跑，并抓出一个真缺陷

正样本是已搬运的 flash 族（机器码逐字节相同，序列必然相同）：`flash_notify` 7/7、`flash_server` 3/3、
`flash_driver_ext` 7/7 全部一致——工具可信。

但 `flash_partition` 报了 2 个差异：`toHex` / `toHexByte` / `toHexStream`。

```
权威(toHex): uxtb r3,r0 / sub.w r2,r3,#48 / cmp r2,#9 / bhi …
我们(toHex): mov r3,r0  / subs r0,#48    / uxtb r2,r0 / cmp r2,#9 / bls …
```

**这是真问题，而且暴露了我两处方法错误**：

1. 之前"9/9 逐字节相同"是拿**standalone 编译**（Armino flags）比的，不是**构建真正产出的对象**
   （arch 目标的 NuttX flags）。两者对 `char` 的处理不同 —— 就是 `trap_base` 那个 `-fsigned-char` 坑；
2. 只比 standalone 就永远发现不了这种"flag 差异导致的构建产物不同"。

修法同 `trap_base`：`set_property(SOURCE bk7258_flash_partition.c TARGET_DIRECTORY arch PROPERTY
COMPILE_OPTIONS "-fsigned-char")`。修完 8/8 一致。

**规则**：以后凡断言"逐字节相同"，比的对象必须是**构建产物**；standalone 编译只能用来快速迭代。

### 全量扫描结果（2026-10-22，第 1 步）

工具：`my_docs/bk7258-wifi/tools/mmio_seq.py`。基线：用 `armino_units.tsv` / `component_flags.tsv` 记录的
组件参数**重新编译权威源码**（`driver` 组件还要补上构建当时用的 `-include compat/bk7258_armino_nuttx_config.h`
与 NuttX 的 include 路径，否则那份基线不是基线）。逐符号按"数据访问偏移与顺序 + 字面量符号 + 调用目标"比对。

**22 个已搬单元，18 个一致，4 个有差异：**

| 单元 | 差异符号数 | 定性 |
| --- | --- | --- |
| `bk7258_printf.c` | 16 | **预期内**。批次 5 有意重写：sink 改成 `syslog`、`bk_printf_init()` 不再碰 UART0、加 mod-disable 列表。这是行为变更，不是缺陷。 |
| `bk7258_wdt.c` | 8（`aon_wdt_hal_set_period`、`bk_aon_wdt_feed/get_period/set_period`、`bk_task_wdt_stop/timeout_check`…） | **待定性** |
| `bk7258_uart_driver.c` | 4（`bk_uart_disable_rx/tx`、`uart_wait_tx_over`、`uart_write_byte_for_fr`） | **待定性** |
| `bk7258_gpio_hal.c` | 2（`gpio_hal_func_map/unmap`） | **待定性** |

工具本身在过程中修了三处（都是自证环节抓出来的）：

1. 调用目标解析把 `bl 0 <sym>` 里的 `0` 当成了符号名；
2. 字面量池**偏移**（`lit:<sym+0xNN>`）是布局噪声，必须归一化掉，否则任何尺寸变化都误报；
3. **尾调用**（`b.w` + `R_ARM_THM_JUMP24`）没算成调用，把 `uart_wait_tx_over`（编译成一条尾跳）读成 0 条指令。

结论：这条路线可用，但**"待定性"的 3 个单元必须先定性**——要么是文档里记过的有意变更，要么是缺陷。
在定性完成前，不得再把这三处称为"逐字节相同"。

### 定性：`gpio_hal`（2 个）——真缺陷，已修

`gpio_hal_func_map/unmap` 在权威里 `HAL_LOGW`/`GPIO_LOGW` 都是**宏**（`GPIO_LOGW` 来自
`middleware/driver/gpio/gpio_driver_base.h:26`），两份源码逐行相同。但我们的 TU **没有那个 include**，
于是 `GPIO_LOGW` 成了隐式函数调用（对象里出现 `R_ARM_THM_CALL GPIO_LOGW`，而镜像里根本没有这个符号）。

今天不炸的唯一原因是批次 3 之后 `gpio_hal_*` 全部被 gc 掉——**一旦有人调用就是链接错误**。

修法：`#include "gpio_driver_base.h"`，并把 `middleware/driver/gpio` 加到芯片 include 列表。修完
`gpio_hal_func_map` 37/37、`gpio_hal_func_unmap` 11/11 一致。

**这一类缺陷值得单独记**：`HAL_LOGW` 这种"日志宏间接依赖一个头"在搬运时最容易漏——源码照抄不会报错，
因为它只影响一个宏的可见性，隐式声明在 C 里只是 warning（本仓库加 `-Wno-error`）。

### 定性：`uart_driver`（4 个）——编译上下文的 inlining 差异，接受

| 符号 | 差异 | 判定 |
| --- | --- | --- |
| `uart_wait_tx_over` | 基线把 `uart_ll_wait_tx_over` **内联**成 14 条寄存器读；我们编译成一条尾跳 `b.w uart_ll_wait_tx_over` | 行为等价：读的是同一个**未改动**的 ll 内联，只是没在调用点展开。同 `flash_bypass` 的 IPA 现象 |
| `bk_uart_disable_rx/tx` | 静态助手 `uart_deinit_rx_gpio` 在基线里被内联（出现 `gpio_dev_unmap`+`bk_gpio_pull_up` 两次调用），我们保留为调用 | 同上，源码逐行相同 |
| `uart_write_byte_for_fr` | 只有一条 `tail:.rodata.__FUNCTION__` 的差异 | **工具噪声**：尾调用判定把一个 rodata 重定位当成了跳转目标 |

结论：`uart_driver` **不动代码**，把这条记进文档；它和 `flash_bypass` 同属"整文件 TU 上下文效应"。

**留给下次的第一件事**：`wdt` 的 8 个符号定性（`aon_wdt_hal_set_period`、`bk_aon_wdt_feed/get_period/
set_period`、`bk_task_wdt_stop/timeout_check`…）。其中 `bk_task_wdt_*` 可能是批次 1 记录过的有意变更
（惰性 arm），其余需逐个对照。

### 工具还修了 4 处，其中一处让上一轮结论作废

| # | 缺陷 | 后果 |
| --- | --- | --- |
| 4 | `objdump --disassemble=<sym>` 在 `-ffunction-sections` 下按**地址**匹配，同地址多节时返回错节 | 读数全错（`wdt` 那8个"差异"就是这么来的） |
| 5 | 符号值带 **Thumb 位**（`0x1`），没掩掉 | 切片起点错，整函数读空 |
| 6 | 偏移按**十六进制**打印（`#0x600`） | 大偏移访问全丢 |
| 7 | **`str.w` / `ldr.w` 的点后缀没匹配** | **最常见的宽访问全被丢弃** —— 上一轮"22 个里 18 个 OK"因此作废 |

修法：整对象反汇编一次，按 readelf 的节归属 + 符号偏移切片；`value &= ~1`；偏移用 `int(x, 0)`；内存操作正则允许 `(?:\.\w+)?`。

### 重扫（工具可信之后）

| 单元 | 可比符号 | 差异 | 定性 |
| --- | --- | --- | --- |
| `printf` | 26 | 16 | ✅ 有意重写（批次 5：syslog sink、mod-disable 列表） |
| **`trap_base`** | 22 | **2**（`arch_is_enter_exception`、`arch_set_enter_exception`） | ❓ 待定性 |
| **`uart_driver`** | 57 | **5**（`bk_uart_disable_rx/tx`、`uart_wait_tx_over`、`uart_write_string`、`uart_write_byte_for_fr`） | ❓ 待定性 |
| **`wdt`** | 30 | **8** | ❓ 待定性 |
| 其余 18 个 | — | 0 | ✅ 序列一致 |

**教训**：判据工具本身必须先自证。这轮工具修了 7 处缺陷，其中第 7 处（`str.w`）会把任何差异都吃掉——如果没在已知样本上验证，我会拿一份"全部通过"的报告去继续重写，而真相是根本没比到东西。

### 三个待定性单元的结论（全部可解释，无缺陷）

**`trap_base`（2 个）—— 共享区搬家。** 两边算的是同一个字段，只是区域基址不同：

| | 基址字面量 | 偏移 | 最终地址 |
| --- | --- | --- | --- |
| 基线 | `0x2805D000` | `#0xb30` | `0x2805DB30` = 厂商 PWR_MNG(`0x2805DB00`)+0x30 |
| 我们 | `0x2809F000` | `#0x730` | `0x2809F730` = 我们的 PWR_MNG(`0x2809F700`)+0x30 |

`+0x30` 正是 `FIXED_ADDR_EXCEPTION_DUMPER`。PWR_MNG 区域由板级 SRAM 划分搬过
（`pm/nuttx_port/include/ram_regions.h`），所以地址不同是**记录在案的设计**。

**`wdt`（8 个）—— 源文件合并带来的内联差异，加一处我有意的记账。**

权威把它拆成 4 个文件（`wdt_driver` / `aon_wdt_driver` / `wdt_hal` / `aon_wdt_hal`），跨 TU 无法内联；
我们合成 `bk7258_wdt.c` 一个文件，于是 `aon_wdt_hal_set_period`↔`aon_wdt_ll_set_period` 之间被内联、
`bk_aon_wdt_feed` 里那次 `hal` 调用被折成 `ll` 调用。**寄存器与顺序不变**，只是展开层次不同。
`bk_wdt_feed_handle` 多出的 `clock_systime_ticks` + 一次 `str`，是看门狗标准面那批加的喂狗记账（有意）。

**`uart_driver`（5 个）—— 同一类 TU 上下文差异 + 一处工具噪声。**
`uart_wait_tx_over`（基线内联 ll、我们尾跳）、`bk_uart_disable_rx/tx`（静态助手 `uart_deinit_rx_gpio`
内联与否）、`uart_write_string`（同）；`uart_write_byte_for_fr` 只是 `tail:.rodata.__FUNCTION__` 的误判。

### 定性小结

| 单元 | 结论 |
| --- | --- |
| `printf` | 有意重写（批次 5） |
| **`gpio_hal`** | **真缺陷，已修**（缺 `gpio_driver_base.h` → `GPIO_LOGW` 变隐式调用） |
| `trap_base` | PWR_MNG 区域搬家，板级设计 |
| `wdt` | 4→1 文件合并导致的内联差异 + 一处有意记账 |
| `uart_driver` | TU 上下文内联差异 + 工具噪声 |

**净结果：22 个已搬单元里，1 个真缺陷（已修），其余全部可解释。** 这条判据可以用于重写了。

---

## 第一个"真重写"：`flash_notify`

搬运版只满足不变式 1、2。这一轮把它按 NuttX 惯用法重写，判据换成 MMIO 序列 + 错误分支 + 上板。

### 改了什么（写法规格）

| 项 | 搬运版 | 重写版 |
| --- | --- | --- |
| 静态量命名 | `s_flash_op_notify`、`flash_erase_ipc_state` | `g_flash_op_notify`、`g_flash_erase_ipc_state`（NuttX 文件级静态量用 `g_`） |
| 类型 | `u32`、隐式 int | `uint32_t`，`stddef.h` 显式引入 |
| 静态助手 | `send_flash_op_state()` | `bk7258_flash_send_op_state()`（带芯片前缀，见下） |
| ISR 命名 | `mb_flash_ipc_tx_cmpl_isr` | `bk7258_flash_ipc_tx_cmpl_isr` |
| 魔数 | 裸 `2000` / `5000` | `FLASH_WAIT_ACK_SPINS` / `FLASH_WAIT_ACK_TIMEOUT_US`，并写清单位是微秒 |
| 条件编译 | `#if 1` 包住整段"新设计" | 去掉 `#if 1`；`#if CONFIG_FLASH_MB` 的**两个分支都保留** |
| extern 声明 | 文件头裸放 | 移到使用点旁，参数写成 `uint32_t` |
| 注释 | Armino 中文/缩写 | 说明"为什么"：5 ms 窗口为什么不用系统 tick、为什么超时返回 `BK_OK` 而不报错 |

**刻意没改的**（改了就是语义变更）：
- 返回值类型 `bk_err_t`、`BK_OK`/`BK_FAIL`/`BK_ERR_FLASH_WAIT_CB_NOT_REGISTER` —— 调用方按它判断；
- `mb_chnl_*` / `flash_op_status_t` / `.itcm_sec_code` 段属性；
- **等待循环的双重边界**（2000 次 + 5 ms，且循环耗尽返回 `BK_OK`）。把超时改成错误会把"对端慢"变成"flash 写失败"；
- `bk_aon_rtc_get_us()` 作为微秒时钟（系统 tick 是 1 ms，对 5 ms 窗口太粗）。

### 验收

| 项 | 结果 |
| --- | --- |
| MMIO 序列 | **7 / 7 与基线一致**（数据访问偏移与顺序、字面量、全局调用目标） |
| `.text` | 1,013,680 B —— **与重写前完全相同**（写法变了，机器码几乎不变） |
| 门禁 | `decode pass` + `package integrity gate: pass` |
| 上板 | 待烧（镜像 `708f840b…`）——flash 在启动路径上，EasyFlash 那行是关键证据 |

### 工具的第 8 次修正

第一次比较时 `mb_flash_op_prepare/finish` 报了差异，原因是静态助手改名
（`send_flash_op_state` → `bk7258_flash_send_op_state`）。**静态符号是 TU 内部的，重写允许改名**，
所以工具改为：LOCAL binding 的函数在调用序列里归一成 `<static>`，只比**全局**调用目标。
这样既不放过"调错了全局函数"，也不因重命名误报。

上板（镜像 `708f840b…`）：三个标准面注册成功，`EasyFlash V4.1.0 is initialize success`（走我们重写过的
`flash_notify` + 分区表 + flash 驱动），`psram init ok`、`wifi inited(1) ret(0)`、`nsh-ap>` 就绪。
**第一个真重写通过验收。**

---

## 第二个真重写：`drv_model`

Armino 惯用法密度最高的文件（51 处 / 443 行），只有 12 个短函数。

### 改了什么

| 项 | 之前 | 之后 |
| --- | --- | --- |
| 临界区 | `GLOBAL_INT_DECLARATION/DISABLE/RESTORE`（包在 `rtos_disable_int()` 上） | NuttX 的 `irqstate_t flags` + `up_irq_save()` / `up_irq_restore()` |
| 清零 | `os_memset()` | `memset()`（`<string.h>`） |
| 头 | `<os/mem.h>`、`<os/os.h>` | `<string.h>`、`<nuttx/irq.h>` |

**签名里的 `UINT32` 没有换**——这套头里 `uint32_t` 是 `unsigned long`，而权威头声明的是 `UINT32`（`unsigned int`），
换成 `uint32_t` 会与 `bk_drv_model.h` 冲突（`conflicting types`）。契约类型不因"看起来更 NuttX"而改。

也没有用 `enter_critical_section()`：那个符号在本 TU 的链接闭包里没有定义（NuttX 非 SMP 下它是宏，
且 sched 库不在芯片层链接表里），`up_irq_save()` 是同等惯用法且无额外依赖。

### 验收

| 项 | 结果 |
| --- | --- |
| 数据访问序列 | **逐条一致**（10/7/3/1 条访问，偏移与顺序相同） |
| 调用序列 | 差异只有 `rtos_disable_int/enable_int` 被内联、`os_memset`→`memset` —— 都是换惯用法的预期 |
| 全量扫描 | 22 个对象里 18 个一致，其余 4 个仍是已定性的（printf/trap_base/uart/wdt） |

工具因此加了第 9 条规则：**惯用法等价表**（`os_memset↔memset`、`os_malloc↔kmm_zalloc`、
`rtos_disable_int↔内联`、`bk_printf_ext↔syslog`…）。表外的调用目标仍必须逐条相同——这样既不放过
"调错了业务函数"，也不因换惯用法误报。

`drv_model` 重写上板（镜像 `7abc97dc…`）：启动序列与重写前逐行一致（三个 `/dev` 节点、`EasyFlash V4.1.0
is initialize success`、`psram init ok`、`wifi inited(1) ret(0)`、`nsh-ap>`）。

顺带验了一条复位路径：AP 上 `reboot bootloader` → `Rptun driver reset remote cp reason: 3` →
CP 侧 `FATAL: Reset by remote core: ap reason: 3` → `Rptun driver reset remote ap reason: 3` →
`set ap rr: 0x2` → `!RST` → 完整重启。两核都复位、AP 原因落盘，BootROM 冷启动链走完（串口下载窗口有时限，
没有工具在等就继续启动，所以最终回到 `nsh-ap>`）。

---

## 批次 14：PSRAM（`psram_driver` + `psram_hal`，29 个符号，T14 的存储半边）

替换 `cp/middleware/driver/psram/psram_driver.c`（16 符号）与
`cp/middleware/soc/common/hal/psram_hal.c`（13 符号），共 **29** 个符号，落在
`chips/bk7258/bk7258_psram.c`。这是同一块控制器上**两套并行实现**的收口：该文件此前已有一份
为 `arm_addregion()` 写的自带寄存器序列的 bring-up，和 Armino 那两个单元各配一遍同样的控制器。

### 逐函数比对（第 2 条）：以权威为准

寄存器偏移（`REG2=0x08`、`REG4=0x10`、`REG5=0x14`、`REG8=0x20`、`REG9=0x24`、
`REGA=0x28`、`REGB=0x2c`）与 `PSRAM_MODE6`/`PSRAM_MODE7`、三组芯片 ID 都对得上。
差异全在旧端口这一侧，逐条按权威改回：

| 位置 | 旧端口 | 权威 |
| --- | --- | --- |
| `cmd_read` | 单次等待、超时返回 `-ETIMEDOUT` | 5 次尝试，每次失败后 `sf_reset(0)`、`sf_reset(1)`、`cmd_reset` 再重发；耗尽返回 0 |
| `cmd_write` | 有界等待 | 无限等待命令位清除 |
| APS6408L 配置 | 写 addr0 后直接读 addr4 | 写 addr4 之前多一次 `cmd_read(0)` |
| 上电 | 直接写 `ANA_REG13`/`CLKDIV2`/`DEV_CLK_EN` | `sys_drv_psram_psldo_vset(1,0)` → `sys_drv_psram_ldo_enable(1)` → `bk_delay_us(1000)` → AHBP 上电 → `sys_drv_psram_psram_disckg(1)` → `set_clk(80M)` → `sys_drv_dev_clk_pwr_up` → `delay(3000)` |
| 电压 | 隐含在 ANA_REG13 位里 | `bk_psram_set_voltage(PSRAM_OUT_1_95V)` 显式一步 |
| 120 MHz | 直接改 CLKDIV2 | `psram_hal_set_default_clk()` → `set_clk(PSRAM_120M)`（sel=1, div=1） |
| 芯片 ID 记忆 | 无 | easyflash `psram_id`，由 `bk_psram_id_auto_detect()` 起线程写入 |

### 分层与一份实现（第 5 条）

寄存器走权威的 `psram_ll_macro_def.h`，SYS/时钟位走 `sys_drv_psram_*` 与
`sys_drv_dev_clk_pwr_up`，所以这个块只有一份program。PSRAM 在 NuttX 里没有设备框架，
它的"标准面"就是**堆区**：`bk7258_psram_initialize(size, id)` 只做探测与上报，
硬件序列和 `bk_psram_init()` 共用同一个 `psram_hw_init()`——早期调用把
`bk7258_psram_init_done` 置位，`driver_init()` 里的 `bk_psram_init()` 随后直接返回。

三处写明理由的偏离：

1. **AHBP 电源**用 `sys_hal_module_power_ctrl()` 而不是
   `bk_pm_module_vote_power_ctrl()`：这段在 `arm_addregion()` 里跑，PM 模块还没起；
   且 PSRAM 是常驻堆区，引用计数没有意义。T12 把权威 `pm.c` 搬过来后，这里换回 submodule 投票即可。
2. **写通通道位图**用临界区代替权威的 Armino 互斥量，语义相同（一次位图更新是原子的）。
3. **`bk_psram_init()` 不建互斥量**：第一个调用者在 `nx_start()` 之前，之后只有读标志，没有并发可防
   （权威自己也用 `rtos_is_scheduler_started()` 跳过互斥量）。

另有一处**新增**（非权威）：`bk7258_psram_probe()` 在加堆前对首尾各写一组 3 个模式并恢复原值，
防止"能应答 ID 但存不住数据"的片子把分配器搞坏。

### 契约头（第 3 条）

`driver/psram.h`、`driver/psram_types.h`、`psram_hal.h` 收归 `chips/bk7258/include/`，
用 `bk7258_psram.c` 与 `bk7258_driver.c` 两组编译参数预处理比对：声明 120/207/457 条、
宏 1425/1425/2163 个，差异均为 0。`psram_driver.h` 只有日志标签宏，不是契约，未收。

### 验收

| 项 | 结果 |
| --- | --- |
| 导出符号 | **29 / 29**（对两个基线 `.o` 的 `nm --defined-only -g` 做 `comm -23`，为空） |
| `psram_driver.c.o` / `psram_hal.c.o` 在 map | **0** |
| `nm -u` | CP / AP 均 0 |
| 打包 | `decode pass` |
| 收头前后 CPU 代码 | `.text`/`.data` 逐字节相同，镜像 sha256 不变 |

板上（镜像 `de7a3d62…`）：

- 开机 `cpu0:I(126):driver_init: psram init ok`；CP 侧 PSRAM 行、AP 侧
  `[BK7258] PSRAM id=0x0000 size=16777216 split: core region 60b00000-60ffffff`（AP 分支固定
  `device_id=0`、只读设备确认 CP 已初始化，不重复探测）。
- `bk7258_psram_test 4096` → `PASS: 4096 KiB, 3 patterns`，分配地址 `0x60b00010`（AP 分片）；
  CP 分片 `bk7258_psram_test 4915` → `PASS: 4915 KiB`，地址 `0x60610618`。
- `free` 的 `Umem` 总量 5278192 B，与 `CONFIG_BK7258_PSRAM_CP_HEAP_SIZE=0x500000` 吻合，
  说明 5 MiB 分片确实进了堆。
- 说明：测试默认 8192 KiB 大于单核分片 5 MiB，会 `malloc failed`——这是默认值与分区不匹配，
  不是回归；用 ≤4915 KiB 即可。

已知未验：`ping`（本次没跑）。T14 的另一半是 flash 族 7 个单元挂 `mtd_dev_s`，仍未做。
