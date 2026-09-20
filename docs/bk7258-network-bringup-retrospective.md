# BK7258 网络 bring-up 复盘

## 现在到哪了

扫描、关联、四次握手、ARP、ICMP、DHCP、DNS 都在板上跑通过,能 ping 通公网。

两份证据日志,PSK 都已脱敏:

| 日志 | 内容 |
|---|---|
| `evidence-20260915/bk7258-dhcp-ping-clean.log` | 干净固件(无探针、无 `DEBUG_NET_INFO`)ping 网关 `4 packets transmitted, 4 received, 0% packet loss` |
| `evidence-20260915/bk7258-dns-public-ping.log` | `inet addr:192.168.1.8 DRaddr:192.168.1.1`,域名解析后 ping 百度 `10 packets transmitted, 10 received`,RTT 29 到 56 ms |

`dhcpc_request: Got IP address` 那行只在 0915-3 出现过,当时开着 `DEBUG_NET_INFO`。删掉那个配置之后它不再打印,所以现在 DHCP 成功看 `ifconfig` 的 `inet addr`。

最后一节列了六个未解决项。其中扫描失败率约 30%,会让任何一次板上验证有三成概率白跑,重敲一次 connect 即可。

## 根因清单

按失真层分组。每条都是一个独立机制,不能合并。

### 移植期的初始化与链接

**1. vendor 校准链缺 libm,且调用顺序错位**(`ed35cd5`、`8744247`)

症状:两次不同的递归 assert 崩溃,都发生在闭源校准代码里。

根因有两个。`vnd_cal_set_auto_pwr_thred()` 用 `ceil()` 算 TSSI 阈值,而 `CONFIG_LIBM` 没开,调用落到未实现的数学路径。另一处是 `vnd_cal_overlay` 被放在 init 链的头部,早于 osal/phy/rf/SARADC 就绪,`vnd_cal_set_epa_config` 里闭源的浮点日志路径撞上递归 assert。

修法:打开 `CONFIG_LIBM`(Armino 的 newlib 本来就带完整 libm),并按 authority 的顺序 `app_phy_init -> bk_rf_adapter_init -> vnd_cal_overlay -> app_wifi_init` 重排。

**2. authority 模块被手写 stub 和重复定义顶替**(`2a58eac`、`b3d0231`)

根因:TRNG 的 `driver_early_init` 步骤完全缺失,GPIO 的 `gpio_dev_map`/`unmap` 是 `return BK_OK` 空壳,而 `analog_shim.c` 和 `hw_driver_shim.c` 里 14 个手写的 `sys_ll_*` 定义与逐字复制来的 Armino `sys_ll.h` 冲突。

修法:直接导入 authority 的 trng(13 文件)、gpio(16 文件)、pm(32 文件),用 `cmp` 验证与 armino 侧逐字节相同,删掉被取代的手写重复项。

判据:关联恢复,`chan_survey` 14/14。

**3. power_clk_rf_init 移植了但没接进调用链**(`51bf1f0`)

根因:函数已经落在 `hal_port` 里,但 `bk7258_wifi_initialize` 从没调它。缺的是 ROSC 校准(ANA_REG6 序列)、温度检测使能、模拟时钟使能、R41 rosc-to-wifi 路由。

修法:按 Armino 的 `driver_early_init` 顺序,接在 `vnd_cal_overlay` 之后、`bk_wifi_init` 之前。

### adapter 契约与闭源库的隐含假设

这一组的共同点是:编译链接全都干净,只在运行时失败。

**4. 中断屏蔽寄存器的返回值契约反了**(`88299e0`)

症状:每次扫描都以 10 秒主机超时结束,`recv_cnt=0`,init 之后再没有 MAC 中断,ke 事件调度器一整轮跑不到 32 次。

根因:authority 的 `sys_drv_int_*disable` 返回**清位之前的整个使能寄存器**,好让调用者精确恢复它打断的状态(`rw_msg_tx`/`rw_task` 的 `WIFI_INT_DISABLE .. WIFI_INT_RESTORE` 就把这个值直接喂 `enable()`)。这个移植返回 0,于是恢复变成 `enable(0)`,永久屏蔽。症状完全对上:init 期间中断正常,之后再也没有。

那两个宏在树内没有调用者,但四个函数都能从闭源 `libwifi.a` 经 adapter 表进来,配对关系我们看不见。

修法:disable 变体返回改前的使能寄存器,spinlock 下显式 `getreg32`/`putreg32` 做 RMW。

判据:`scanu_confirm status=0 recv_cnt=104 result=32 time=1.67s`,`isr 33=87 34=26 36=27`(之前全是 0),`ke_sched[1..225]`(之前整个 10 秒只有 [1])。

同一版镜像里还落了 60M 的 cpu_freq vote,所以单变量归因不成立,两者都来自 authority,都保留。

**5. 闭源库假设新分配的内存已清零**(`88299e0`)

根因:闭源代码里有先分配后直接假定为零的地方(`sta_info_tab` 已证实)。FreeRTOS 下它的堆是一个 `.bss` 数组所以看不出来,而 NuttX 的堆在 Wi-Fi init 时已经是脏的。

修法:`ke_malloc` 和 `os_malloc_wifi_buffer` 自己清零。这不是在声称 FreeRTOS 保证清零,只是补上闭源库实际依赖的前提。

**6. dcache flush 是空 stub,而 DCACHE 是开的**(`5540f23`)

根因:`_flush_all_dcache` 在 os_funcs 表里是空实现,两侧的 `CONFIG_ARCH_DCACHE` 都是 y。`glue/include/cache.h:12-16` 早就写着这是"MAC 数据路径上的静默数据损坏"。

修法:按 authority 做 Clean+Invalidate。同表还修了 `_pm_low_voltage_delta_wakeup_delay_in_us` 从 0 改成 188(authority 的 `ceil(6*1e6/32000)`),并把 `_delay`、`_rc_drv_set_rf_en`、`_sta_ip_start` 退回 NULL。authority 三个都留空且能跑,说明库会跳过;我们原先给 `_delay` 填了个单位靠猜的毫秒睡眠,一旦库真去调它会卡住 CFM 处理。

**7. pbuf ABI 两处错位**(`88299e0`)

根因:layer 的 switch 差了一位,`BK_PBUF_RAW` 落空成 NULL;`get_rx_pbuf_type` 该返回序数的地方返回了 lwIP 的位域。

### 关联与四次握手

**8. 非 PM_V2 分支缺两个 IOCTL 派发**(`5540f23`)

症状:关联从未被确认。`rw_msg_send timeout for 6173`,`Failed to get BSSID`,然后 wpa_supplicant 在能置 ASSOCIATED 之前就本地 deauth 了。

根因:`rw_task.c` 里非 PM_V2 的 `core_thread_main()` 少了 `BMSG_SOFTWARE_IOCTL_TYPE` 和 `BMSG_HARDWARE_IOCTL_TYPE` 两个 case,于是被 `rw_msg_tx.c:112-118` 路由到硬件发送器的 `ME_GET_BSS_INFO_REQ` 掉进 `default` 被丢弃,CFM 永不返回,5 秒后超时。vendor 默认配置是 `CONFIG_PM_V2=y`,所以上游从不会碰到。

修法:从 PM_V2 的兄弟变体逐字搬过来两个 case。

判据:超时和 `Failed to get BSSID` 消失,ASSOCIATED 先 4/5 后 3/3。

**9. EAPOL 没有分流,四次握手不开始**(`5540f23`)

根因:`ethernetif_input()` 把 EAPOL(0x888E)和其他 802.3 帧一起交给 NuttX 协议栈,在那里被丢弃。这个义务在 `glue/include/wpa_compat/sk_intf.h:20-22` 里已经记着,但没实现。

修法:在 netif 路径之前分流给 `ke_l2_packet_tx()`,连自echo 过滤一起从 vendor 的 bridge(`cp/.../port/wlanif.c:331`)转写过来。

判据:`ASSOCIATED -> 4WAY_HANDSHAKE`,`WPA: RX message 1 of 4-Way Handshake`,PTK/KCK/KEK/TK 推导出来,`Sending EAPOL-Key 2/4`。

这一条也解释了为什么后来需要 `02526fd` 那种仪表:关联成功完全不能证明数据路径通,因为 EAPOL 在 `rx_submit()` 之前就被分走了,而 M1/M3 在 802.11 层是不加密的。

### 驱动到协议栈的交接

**10. iob 里的帧位置错了一个以太头**(`d8e3bcd`)

症状:`ifconfig` 显示 `Received=11 Errors=0`,而 `IPv4=0 ARP=0`,`arp_in` 每帧报 `Invalid hardware type`,ping 拿到 `ENETUNREACH`。

根因:`rx_submit()` 沿用 lwIP 约定,整帧从缓冲区起点开始。NuttX netdev lowerhalf 要求 `IOB_DATA` 指向 L3 载荷,以太头放在前面的预留区(`netdev.h:206-207`)。协议栈每个读者都偏了 14 字节,`arp_in()` 把目的 MAC 前两字节读成了 `ah_hwtype`。

修法:改用 `netpkt_alloc()` + `netpkt_copyin()`,让框架处理负偏移,不自己算。错误路径的 `iob_free_chain()` 一并换成 `netpkt_free()`,否则配额不归还。

判据:`arp_in` 报错归零,`ifconfig` 的 `IPv4` 计数非零,ping 通网关。

细节见 `bk7258-rx-nuttx-vs-lwip.md`。

**11. 大帧跨 IOB 链后静默丢弃**(`3b10c92`)

症状:ping 通了,DHCP 拿不到地址。三次 DISCOVER 每次 3.0 秒超时,一次 REQUEST 都没有,没有任何错误日志。

根因未确诊。已确证的是:352 字节的 OFFER 是这个驱动交出去的第一个装不进单个 IOB 的帧(`IOB_BUFSIZE` 默认 196),而 ping 验证过的 ARP(42)和 ICMP(98)都是单 IOB。

绕开:`CONFIG_IOB_BUFSIZE=640`。`640 - 14 = 626` 大于 `NET_ETH_PKTSIZE=590`,收帧不再跨链。代价是 IOB 池多占约 10.4 KB 堆。

判据:同一帧的驱动侧记录从 `io_len=182 chained=1` 变成 `io_len=338 chained=0`,DHCP 随之走完握手。

**12. netdev 的协议栈侧字段没填**(`8a6220c`)

症状:netdev 注册成功,但发帧失败,原因与 vendor 路径无关。

根因:`d_mac` 是零,carrier 从未升起,`vif_idx` 保持静态初始化的 0。

修法:在 `bk7258_wifi_lower_register()` 里从 `bk7258_wifi_sta_own_mac()` 填 `d_mac`,carrier 和 `vif_idx` 挂到关联事件上。这是 authority 侧 `low_level_init()` 填 lwIP netif 的对应动作。

### 中断上下文

**13. vendor 从硬中断调 printf**(`2d6a5eb`)

症状:`semaphore.h:518` assert。只在 `0914-vela-17` 一轮出现过,那之前 16 轮没有,那之后 7 轮也没再现。

根因:`printf()` 经 `flockfile()` 拿 stdout 的 FILE 锁,`nxmutex_wait()` 第一个 `DEBUGASSERT` 就是 `!up_interrupt_context()`。而 vendor 把 `bk_printf_ext` 注册成 Wi-Fi 和 PHY 两个 adapter 的 `._log`,MAC 硬件中断处理里会调它。

修法:换 `syslog`。`syslog_write.c:65` 在中断上下文自动降级为非阻塞,`syslog_device.c:261` 设备未就绪时返回 `-ENOSYS` 而不 assert。暴露这个 bug 的崩溃转储本身就是从中断上下文经 syslog 打出来的。

同一条规则在 `bk7258_wifi_osal_queue_send_common()` 里已经写过,这个文件漏了。

**14. UART ISR 不按使能位分发**(`5766e76`)

症状:上面那个 `serial.c:2064` 的 assert 更容易被撞上。

根因:ISR 只用 `0xff` 掩 INT_STATUS,从不查 `priv->ie`。TX_READY 报告的是 FIFO 有写空间,所以它几乎一直置位,包括 `bk7258_uart_txint(dev, false)` 已经把它从 `priv->ie` 和 INT_ENABLE 里清掉很久之后。于是每次 RX 中断都额外跑一次 `uart_xmitchars()`,而发送缓冲是空的。

这次白跑有代价:`uart_xmitchars()` 进门就 `uart_spinlock(dev, true)`,也就是 spin_lock 加 sched_lock,配对的解锁在计数回到 0 时会走 `nxsched_unlock()` 到 `nxsched_merge_pending()`,可能从中断上下文里切任务。让它在每个 RX 中断都跑,而不是只在真有待发数据时跑,把那条路径的窗口拉宽了。

修法:清除仍用未掩码的 `raw`,否则未使能的位会 latch 在 INT_STATUS 里永久重触发中断线;分发只看 `raw & priv->ie`。树内驱动都这么做,`stm32_serial.c` 每个分支都同时判 `priv->ie` 和状态位,这个驱动是异类。

边界:修掉了一处确定的偏离,但没关掉那个 assert。合法的 TX 中断照样会调 `uart_xmitchars()`,危险路径还在,这只是不再无谓地走它。

### 启动与内存

**15. PM 初始化关掉了 PSRAM 所在的电源域**(`51a3aef`)

症状:`mm_foreach()` assert,而分配器自己的记账看起来完好,`free=` 每次探测都逐字节相同。

根因:`sys_hal_power_config_default()` 把 AHBP 电源域关掉,PSRAM 是它的子模块。PSRAM 是 16.86 MiB 堆里的 16 MiB,一次寄存器写让 99.5% 的活跃堆内容失效。原先这个调用在运行时 init 里,那时 PSRAM 已经进堆一整个启动周期。

修法:PM 硬件初始化移到 `nx_start()` 之前。

**16. 链接脚本的孤儿段没被初始化**(`e74d83e`)

症状:`mb_chnl_open()` 读到 `log_chnl == 0`,无任何诊断。

根因:Armino 用 `__attribute__((section(".dtcm_sec_data")))` 标记了一些对象。这个移植只在 MEMORY 里声明了 sram 和 flash,链接器把 `.dtcm_sec_data` 当孤儿段放在 `.data` 之后:在普通 SRAM 里,但在 `_sdata.._edata` 之外。启动拷贝只走 `_sdata.._edata`,`.bss` 清零从 `_sbss` 开始,落在缝隙里的数据既不初始化也不清零。

修法:把 `.dtcm_sec_data` 折进 `.data`。

**17. 释放从核前检查了不该检查的位**(`a8321d6`)

根因:`sys_hal_power_config_default()` 通过置 halt 和 pwr_dw 把两个从核关掉,所以 `board_start_cpu()` 运行时 halt 本就是置位的,而释放流程要求它已清零。这个检查在 PM 初始化前移之后才第一次被真正执行到。

### 时序余量

**18. AP 双核 SMP 让 PBKDF2 超出 BSS 有效期**(`7d7436c`,workaround)

症状:关联必失败,扫到的 BSS 在关联工作项执行前就被回收。

根因:PBKDF2 推导 PMK 在 AP 跑 SMP 时要 12.87 秒,`BSS_EXPIRATION_AGE` 是 10 秒。关掉 AP 的 SMP 降到 2.74 秒。AP 自身在运行时调度器的 spinlock 上死锁。

这是绕开,`CONFIG_SMP=y` 是团队基线,真因解决后应该恢复。

### 可观测性

这三条不是 bug,但没有它们前面的排查都做不了。

**19. NSH 网络工具没编进去**(`bf6fb3f`):`ifconfig`/`ifup`/`renew`/`ping` 全部不可用,关联通了也没法配地址或造流量。DHCP 客户端还需要 `NET_BROADCAST` 和 `NET_SOCKOPTS` 同时开(`dhcpc.c` 用 `SO_RCVTIMEO`、`SO_BINDTODEVICE`、`INADDR_BROADCAST`,而 `udp_input.c` 的广播接收要求两者都在)。

**20. 日志噪声淹没证据**(`e7f563a`):SARADC 采样器每秒一轮,每轮无条件打四条 LOG_INFO。一次抓包里约 7% 是这些行,把 wpa_supplicant 的关联轨迹挤出了可见窗口。

**21. 收帧计数与 ARP 表不可见**(`02526fd`)

关联成功完全不能证明数据路径通。EAPOL 在 `rx_submit()` 之前就被分流走了(见第 9 条),而 M1/M3 在 802.11 层不加密,所以四次握手跑完也说不出有没有一个普通数据帧到过协议栈。这个 commit 是补仪表,它自己没修任何失败。

- Kconfig 里 `select ARCH_HAVE_NETDEV_STATISTICS`,`NETDEV_STATISTICS` 才能打开。上游只有 `NET_RPMSG_DRV`、`NET_DUMPPACKET`、`DM9X_NINTERFACES`、`ENC28J60_REGDEBUG` 会 select 它,lowerhalf 驱动默认摸不到。计数本身是 `netdev_upperhalf.c` 做的(六处 `NETDEV_RXPACKETS`/`TXPACKETS`/`RXDROPPED`),正是这个驱动挂进去的那一层。此后 `ifconfig` 才有 RX/TX 包数、错误、丢弃。

  第 8 条那个偏移 bug 就是靠这组计数发现的:`Received=11 Errors=0` 而 `IPv4=0 ARP=0`,没有这些数字只能看到"ping 不通"。

- `NET_ARP_IPIN` 和 `NET_ARPTAB_SIZE=48` 跟 r528s3-gemini-s1 那个已知能用的移植对齐。`ARP_IPIN` 顺带是一个收帧证明:开了它之后任何收到的 IP 包都会学一条 ARP 表项,所以 ARP 表一直空就说明根本没有 IPv4 帧进来。

- `bk7258_wifi_reclaim()` 保持空实现,但注释写清了为什么。它不是 stub:`netpkt_free()` 自己归还配额(`netdev_upperhalf.c` 里对 `quota_ptr` 的 `atomic_add`),而 `transmit()` 在返回前就释放了 netpkt,所以配额在消耗它的同一个调用栈上就还掉了,这个驱动从不会在 transmit 之后继续持有 netpkt。这个 op 仍然注册,因为不注册的话 `netdev_upper_can_tx()` 没有恢复路径,任何一次配额丢失会从"损失一个轮询周期"变成永久致命。

这个 commit 还撤回了两个关于 `ENETUNREACH` 的假设,没让它们悬着:`ifconfig` 配地址不会把接口拉下来(`SIOCSIFADDR` 在 `netdev_ioctl.c:1162` 只做比较、赋值、通知 netlink,从不碰 `IFF_UP`);TX 配额也没有泄漏,理由就是上面那条 `netpkt_free()` 的读法。

**22. DNS 客户端没编进去**(`87e6927`)

症状:任何域名都解析不了,失败在发包之前。

```
nsh> ping www.baidu.com
ERROR: ping_gethostip(www.baidu.com) failed
```

根因:`CONFIG_NETDB_DNSCLIENT` 没开,于是 `netlib_obtainipv4addr.c:81-91` 整段被编掉,DHCP 已经拿回来的 nameserver 被静默丢弃。租约里本来就带着它,0915-3 那轮打过 `Got DNS server 192.168.190.241`。

修法一行。`NETDB_DNSCLIENT` 自己会 select `LIBC_NETDB` 和 `NET_SOCKOPTS`,依赖的 `NET` 和 `NET_UDP` 都已经在了。`DEFAULT_SMALL` 是关的,解析缓存保持默认 8 条。

判据:`ping www.baidu.com` 解析出 `110.242.74.102` 并 10 发 10 收。路由不需要另外配,`CONFIG_NET_ROUTE` 保持关闭对单网卡是对的,`arp_send()` 在目的地址不在本子网时会退回用 `dev->d_draddr`,而那是 DHCP 设好的。

## 方法上的教训

这部分比上面的清单更可复用。

**1. 探针要有独立预算。** `rx#` 探针的 24 帧预算在 52 秒就打满,而 DHCP 发生在那之后。后加的 DHCP 探针如果共用这个计数器,会静默输出零行,白一轮固件。

**2. 判据要真的能分辨两种情况。** 有一轮为了看 `No listener on UDP port` 而重编重烧,结果 0 条。回头读代码才发现 `udp_input.c:349-357` 对广播包找不到 listener 时只做 `dev->d_len = 0`,不打日志也不计数。而那一轮所有帧都是广播,0 条警告和"全被丢"、"全被收"两种情况都兼容。这一烧完全白费。

**3. 缺失的日志也是证据,但要先确认那条日志会不会打。** `parseoptions` 里四个 `nerr("Packet too short ...")` 全部静默,而 `DEBUG_NET_ERROR` 是开的,这支持 `len <= 0`(循环一次不执行)而不是内容错。反面例子是第 2 条:那次的"没有日志"什么也不说明。

**4. 自己发出去的包是天然对照组。** 广播 DISCOVER 会以回声形式收回来,里面装着 dhcpc 真正用的 xid 和 chaddr。拿它跟 OFFER 逐字段比,不需要看到 dhcpc 的私有状态,也不用改上游代码。

**5. 上游代码不改,在自己这侧取证。** `apps/netutils/dhcpc` 属于上游,按仓库规矩改它要单独发 PR。`dhcpc_parsemsg()` 的三个门任一不合就 `return 0`,任何 debug level 都打不出一行。做法是在驱动的探针里原地复现 `parseoptions` 的遍历,直接在帧上算出 `msgtype`。

**6. 烧写前先验证要看的字符串真在 ELF 里。**

```
strings nuttx.elf | grep -E "dhcp#%u dport|Received OFFER from"
```

`ninfo` 只在对应 `DEBUG_*` 打开时才编译进去,少一个配置就是一轮白烧。

**7. 烧写前先验证控制台活着。** 有一次跳过这步,连续两轮 `LinkCheck Timeout` / `GetBus fail`,最后靠拔插 USB 断电才恢复。自动 `reboot bootloader` 要求 NSH 控制台能响应,板子 assert 卡住之后按复位键不够。验证方法是发一个回车看有没有 `nsh> ` 回显,注意空闲控制台被动读是 0 字节,那个结果什么也不说明。

**8. 用 mtime 证明提交的源码就是实测的固件。** 每次提交前对比源文件 mtime 和打包时刻,能落在 commit message 里当硬证据。

**9. 一次只改一个变量。** `IOB_BUFSIZE` 那次同轮还留着 BOOTP 广播位的改动,所以在 commit 里写清了广播位不是原因(改完 OFFER 变广播,DHCP 仍然失败),只是消掉一个变量。

**10. 症状词只能当路由桶。** "ping 不通"、"关联不上" 对应过至少五个互不相干的机制。24 轮抓包里,先用计数对比把范围切开,再读代码,比直接钻源码快得多。

## 尚未解决

**跨 IOB 链丢包的机制。** 手算 `iob_copyout()` 对这个帧是正确的:偏移 28,第一个 IOB 拷 154,第二个拷 156,合计 310,正是 DHCP 报文长度。所以读代码解释不了这个失败,而三轮都是同一图形。`chain#` 探针测的是 `rx_submit` 时刻,链在那里是对的;从那里到 `udp_recvfrom` 之间还有 `netdev_iob_replace`、`eth_input`、`ipv4_input`、`udp_input`,没有测过。当前配置下这段是死代码,谁把 `NET_ETH_PKTSIZE` 提到 626 以上会再撞上。

**`sched_unlock` 的 assert。** 出现过两次,都来自 `sched_unlock()` 的 `DEBUGASSERT(lockcount > 0)`。那个宏在调用点展开,所以报的是先执行到的调用者,不是弄坏计数的人:0915-6 报 `serial.c:2064`,在 ping 的 `printf` 路上;0915-12 报 `task_exithook.c:479`,renew 退出时。含义是解锁那一刻计数已经为 0。

我们这侧查过的三条路径都清白。ISR 回调用 `lockbal` 探针测了四轮(0915-13、15、16、17),零输出,而同期 MAC 中断很忙,单 0915-13 就有 16 次 `bmsg_rx_handler` 和 10 条 `txcfm`,它们只能经我们的 trampoline 进来。临界区 shim 在 `CONFIG_SMP=n` 下等于 `up_irq_save()`,不碰计数。崩溃栈上的 `nxsem_post` 也不碰,`sem_post.c:185` 的 `sched_lock()` 被 `PRIORITY_INHERITANCE` 和 `PRIORITY_PROTECT` 双关编掉了。

一个推断在这里作废。我曾把 `xPSR` 低 9 位等于 11(SVCall)读作"assert 落在上下文切换里",但 `_assert()` 自己就是经 `SYS_assert_handler` 进来的(`syscall.h:503`、`arm_svcall.c:299`),所以这个 build 里每个 assert 都会报 SVCall。那个字段对原始现场零信息量,建立在它上面的推理一并撤回。

之后四轮没再现,基线 2/26,不构成修好的证据。下一条待查线索:`task_exithook.c:469/479` 那对 `sched_lock`/`sched_unlock` 是两个独立的宏,各自调一次 `this_task()`,而 `task_exit.c:100-107` 的注释写着此时"就绪队列头与正在运行的线程不对应",它因此在 `:109` 直接 `rtcb->lockcount++` 而不调 `sched_lock()`。`nxtask_exithook` 是否落在那个窗口里,没有验证。

顺带一处适配层的审查结论,查完保持原样:把 `bk7258_wifi_enter_critical_cb` 同时注册进 `_rtos_enter_critical` 和 `_rtos_disable_int` 与权威一致,权威的 `rtos_disable_int_wrapper()` 同样返回 `rtos_enter_critical()`(`third_party/.../bk_wifi_adapter.c:829`),用真正 `rtos_disable_int()` 的是 PHY adapter。

**扫描约 30% 失败。** 24 轮统计:`chan_survey` 打满 14 个信道则关联 15/18 成功;中途停则 0/6,`lmac_connect_req` 根本不发。失败的 6 轮里有 4 轮发生在 RX 路径改动之前,是既有问题。`iob_trimhead` 与扫描失败的相关性(见另一份文档)是同一现象的早期观察,当时也没找到机制。

**PBKDF2 仍慢一个数量级。** 关掉 AP 的 SMP 后是 2.74 秒,理论值应在 0.4 秒以内,余量只剩 7 秒多。候选是 flash XIP 取指开销和 D-cache 配了但从未启用。

**AP 的 SMP 死锁。** `7d7436c` 是 workaround,团队基线是 `CONFIG_SMP=y`。

**`txcfm` 探针仍在,尚未定去处。** `rwnx_tx.c` 的 `rwnx_txdesc_free()` 里那个配对确认探针每帧打一行,前 48 帧无条件打、之后每 64 帧一次。它不属于 `5540f23` 列的那三个,所以没跟着一起清。同一文件里 500 行附近那段 `#if 0` 的分析注释引用它作为"live verdict",要删得连那段一起处理。

## 复现步骤

板上,按顺序:

```
bk7258_wifi_runtime connect <ssid> <psk>
ifup wlan0
renew wlan0
ifconfig
ping -c 4 <网关>
ping -c 4 www.baidu.com
```

`connect` 约有三成概率因扫描失败而超时,看不到 `CTRL-EVENT-CONNECTED` 就重敲一次。

`ifconfig` 要单独敲一次:`Got IP address` 那行是 ninfo,`DEBUG_NET_INFO` 删掉之后不再打印,现在判断 DHCP 成功只能看 `inet addr`。

先确认 AP 自己能上网。`Xiaomi_6C87` 那台路由器连续两轮 DHCP 失败,后来发现它本身就不通网,DHCP 服务大概一起坏着。三个 DISCOVER 都拿到了 802.11 ACK,失败在 3 次重试用尽。已经验证过的 AP 是两个手机热点,`Luv` 给 192.168.190.x,`FoggySpot` 给 192.168.1.x。

构建与烧录走 `.claude/skills/bk7258-worktree-build`:

```bash
scripts/full_flow.sh --workspace <workspace> --worktree <worktree> --jobs 12
scripts/full_flow.sh --workspace <workspace> --worktree <worktree> \
                     --flash --device /dev/ttyUSB0
```

烧写要交互终端,擦写 flash。日志留在 package 目录的 `bk_loader-*.log`,判据是 `Writing Flash OK`。
