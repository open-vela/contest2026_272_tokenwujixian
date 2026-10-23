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
   - 兼容面**不许再自带一套寄存器访问**，而要落在 NuttX 的标准框架上：`uart_ops_s`、`rtc_ops_s`、
     `mtd_dev_s`、`dma_dev_s`、`adc_ops_s`、`watchdog_ops_s`、`pm_register`…；
   - **ops 的方法体就是原 Armino 的寄存器写法**：一个单元只有一份实现，Armino 面和 `/dev/<node>` 共用它；
   - 判定方式：同一个语义必须两个面都跑通。RTC 的验收就是两组——设备口 `RTC_SET_TIME`/`RTC_RD_TIME`
     往返，内核口 `clock_settime`/`clock_gettime` 并确认 `/dev/rtc0` 跟着变；
   - **例外必须写明**：确实没有 NuttX 等价物的那一小块（例：AON RTC 的 µs 单调计数，框架只有秒级）
     留在驱动内部，并在本文档"ops 语义完整性"一类的表里写清理由；
   - 推论：**一个外设如果 NuttX 有框架而我们没挂，它就是没做完**，不能算"已经原生化"。
     当前欠账：看门狗（`/dev/watchdog0`）、GPIO（`/dev/gpio0`）、温度/电压（`/dev/adc0`），
     以及 UART 的"两面未合一"（控制台 ops 直接调 `uart_ll_*`，Armino 面走 `uart_hal_*`）。

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
（排除）——**T17 的单元全部收敛完毕**，其中 10 个以"逐字节相同"收尾，2 个以"无传输层、整体删除"收尾。

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
