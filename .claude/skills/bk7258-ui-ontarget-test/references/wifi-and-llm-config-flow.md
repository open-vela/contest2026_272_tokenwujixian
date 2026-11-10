# 联网 + 模型配置流程(真机)

> 适用范围:整片烧录后的 BK7258 真机,AP 侧 NSH 控制台 `/dev/ttyUSB0`(**1000000**,即 1 Mbaud;按 115200 开是全静默,不是挂死)。
> 两步配置是 `ask` / PTT 的前置:先让 AP 出网,再告诉 agent 用哪个模型。
> 本文只写「怎么配、怎么确认、坑在哪」,界面顺序与观察点见
> [ui-flow-and-test.md](ui-flow-and-test.md),判据见 [main_loop.md](main_loop.md) §0。

---

## 0. 提示符与两个"命令行"

| 位置 | 提示符 | 能敲什么 |
|---|---|---|
| AP 的 NuttX shell | `nsh-ap>` | `wapi`、`nslookup`、`ping`、`ai_agent` |
| agent CLI(AP 内) | `vela>` | `set_llm`、`set_dns`、`ask`、`voice_test_*`、`config_show` … |

要点:

- `ai_agent` 必须在 `nsh-ap>` 下敲;敲完提示符变 `vela>`,此时**不能**再敲 `wapi`
  (`vela>` 不转发 NSH 命令)。
- 真机**不要用 `quit` 退 agent**(会卡死、回不到 NSH);要退出/重来就复位(板上 RESET/K1)。
- 板子**只在收到输入时才输出**:串口空闲时不会主动重打提示符,自动化脚本必须先发一个
  `\r\n` 再去等 `nsh-ap>` / `vela>`。

---

## 1. 联网(AP 侧 `wapi`)

在 `nsh-ap>` 下执行:

```sh
wapi connect "<SSID>" ****
```

- SSID 带参数建议加引号;密码照抄 `../scripts/cmd_scripts.sh`(本文不写明文)。
- `wapi psk` / `wapi essid` 是分步写法(`cmd.sh` 里保留为注释),`wapi connect`
  一条即可。
- 成功判据(串口日志二选一即可):

  ```
  BK STA got ip4 ...
  wlan0 ip ...
  ```

之后用 `nslookup www.baidu.com` 快速验证 DNS + 出网,再 `ai_agent` 进 `vela>`。

### 坑

| 现象 | 原因 | 处理 |
|---|---|---|
| `nslookup: arp_send: ERROR: Unreachable: 0100000a` | AP 的 resolver 还在用开机默认 DNS `10.0.0.1`(无路由),`0100000a` = 10.0.0.1 | 先 `wapi connect` 成功;进 agent 后再 `set_dns`(见 §2) |
| `ifconfig` 在 AP 上是空的 | AP 是纯 usrsock 客户端,网卡/ARP/DHCP 都在 CP | 属正常,不要据此判断"没联网";看 `wlan0 ip` / `net_status` |
| `set_wifi` 没反应 | AP 上 `set_wifi` 是空壳 | AP 用 `wapi`;`set_wifi` 只对 CP 侧有意义 |
| `minicom` 开着,脚本打不开串口 | `/dev/ttyUSB0` 被占用 | 先 `sudo kill <minicom pid>` 再跑自动化 |

---

## 2. 模型配置(agent 侧 `set_llm`)

进 `vela>` 后,顺序固定为 **DNS → set_llm → 校验 → ask**:

```sh
set_dns 10.237.25.8
set_llm http://model.mify.ai.srv deepseek/deepseek-flash sk-************************
config_show
ask 你好
```

### 2.1 `set_dns`

- 用公司内网 DNS `10.237.25.8` 才能解析 `model.mify.ai.srv`
  (实测解析到 10.27.109.245 / 10.108.154.89)。
- 只有 **1 个** nameserver 槽位:一次给两个地址时**后一个覆盖前一个**,
  `set_dns A B` 最终只剩 `B`。要两个就分两条、且知道会互相覆盖。
- 开机默认是 `0x0a000001`(10.0.0.1),就是 §1 里 `Unreachable: 0100000a` 的来源。

### 2.2 `set_llm` 三个参数

```
set_llm <base_url> <model> [api_key]
```

| 参数 | 规则 | 本次实测值 |
|---|---|---|
| base_url | 只给到版本段,客户端自动补 `/v1/chat/completions` | `http://model.mify.ai.srv` |
| model | 带供应商前缀 `provider/model`;前缀会转成 `X-Model-Provider-Id` 请求头,真正发给 API 的是斜杠后的名字 | `deepseek/deepseek-flash` |
| api_key | OpenAI 风格 `Authorization: Bearer <key>`;**省略则保留上次的 key** | `sk-…`(见 cmd_scripts.sh) |

### 2.3 对应 Claude Code 里的设置

| Claude Code 设置 | 真机上对应 |
|---|---|
| `ANTHROPIC_BASE_URL=http://model.mify.ai.srv/anthropic` | ❌ 设备端只说 **OpenAI 兼容协议**,不支持 Anthropic Messages;用 `http://model.mify.ai.srv` |
| `ANTHROPIC_DEFAULT_SONNET_MODEL=deepseek/deepseek-flash` | ✅ 原样填 `deepseek/deepseek-flash` |
| `ANTHROPIC_DEFAULT_OPUS_MODEL=deepseek/deepseek-v4-pro` | 换模型:只发 `set_llm http://model.mify.ai.srv deepseek/deepseek-v4-pro`(key 保留) |
| `ANTHROPIC_API_KEY=sk-…` | ✅ 第三个参数;不要写进文档/日志 |

> cmd_llm.c 里的 `claude` 预设指向 `api.anthropic.com/v1/messages`,设备端用不了,
> 不要选它。

### 2.4 确认配置生效

- `config_show`:看 `LLM Base` / `Model` / key 是否已设置(屏幕截图外发前先打码)。
- `set_llm` 成功会打:

  ```
  [llm] LLM config updated atomically: model.mify.ai.srv/v1/chat/completions (model: deepseek/deepseek-flash)
  ```

- `ask` 走到 `[llm] OpenAI API with tools (...)` 才说明请求真的发出去了。

### 2.5 模型侧行为提示

`deepseek/deepseek-flash` 在本项目的 system prompt + tools 下,**可能只回一句
"让我查一下…"而不发 tool_calls**(本轮就只发起了 1 次 LLM 调用、没有
`Tool iter`)。这不是设备 bug;要验多轮工具链,换成明确会调工具的模型/提示词。

### 2.6 时间同步(联网后自动对时)

待机屏要显示正确的日期/时间,所以联网成功后必须对时。链路顺序:
`wapi connect`(nsh-ap>)→ `ai_agent`(vela>)→ 链路 up → 应用侧 SNTP 自动对时。

**为什么不用 NuttX 自带的 ntpc**:`apps/netutils/netinit` 里 `ntpc_start()`
在 `NETINIT_HAVE_NETDEV` 分支下,AP 没有 netdev(网络走 usrsock 到 CP),
开机根本执行不到,于是时钟停在 1970。现在由 `src/infra/time_sync.c`
自己发 48 字节 SNTP(UDP/123,模式 3),成功后 `settimeofday()`,
每 30 分钟再校一次;每步失败都带 errno 记日志,便于区分 DNS 与 udp/123。

**为什么显示要 +8 小时**:AP 构建没开 `CONFIG_LIBC_LOCALTIME`,
libc 的 `localtime_r()` 实际退化成 `gmtime_r()`,`TZ=CST-8` 不生效。
所以所有要显示本地时间的地方都显式 `now + AGENT_TZ_OFFSET_SEC`
(与 `tool_get_time` 一致),待机屏 `standby_view.c` 也按这个来。

**验证命令(vela>)**:

```sh
sync_time
```

正常输出(2026-10-23 真机):

```
clock  UTC 2026-10-23 06:33:46 | CST 2026-10-23 14:33:46 (epoch 1792737226)
link   up (192.168.110.233)
sntp   synced (re-syncs every 30 min)
```

未同步时第三行会给出最近一次失败原因(`resolve … failed` / `recv … errno`,
后者多半是 udp/123 被网络策略挡了)。

**屏上地面真值**:待机屏每分钟打一条它实际渲染的内容,可直接核对
(控制台会丢字,看分钟即可):

```
[standby_view] standby shows 2026-10-23 14:33 周五
```

> 注意:`wapi connect`/`set_llm` 的回显会把整行命令(含 key)打到控制台。
> 抓串口日志时按 `sk-[A-Za-z0-9]+` 整体打码;控制台丢字会让"半截密钥"
> 躲过按原值精确匹配的打码,这条正则必须兜底。

---

## 3. 自动化驱动方式(串口脚本)

- 用 pyserial 直接开 `/dev/ttyUSB0`(**1000000**,1 Mbaud),不要经 minicom。
- **先写 `\r\n` 再等提示符**,否则永远等不到(板子不主动输出)。
- 一次性从 `../scripts/cmd_scripts.sh` 里取 `set_llm` 整行原样下发,避免脚本文本里再存一份 key。
- 落到文件的日志**必须把 key 打码**再存/提交(见 `contest-log-redact` skill)。

---

# BUG-20261021-01:`ask` 回复后约 20s 整机静默

**状态**:已排除 CPU 满、音频/beep/TTS 腿、LLM/TLS 腿、LVGL 断言、线程栈溢出;
**当前主嫌 = `lv_obj_update_layout()` 的布局重跑不收敛(死循环)**,见 §3.7.2。
**已加两道兜底**(布局 32 轮上限 + 看门狗/探针),镜像 `a01a0e16…cf789f03`
(2026-10-21 12:12 构建)**待烧录验证**。
**环境**:Wi-Fi 已连(`<SSID>`)、DNS `10.237.25.8`、LLM `deepseek/deepseek-flash`、
**未配置 volc TTS key**。

## 3.1 复现步骤

1. `nsh-ap>`:`wapi connect "<SSID>" ****` → `wlan0 ip` 有地址;
2. `ai_agent` → `vela>`;
3. `set_dns 10.237.25.8`、`set_llm http://model.mify.ai.srv deepseek/deepseek-flash sk-…`;
4. `ask 你好`。

复现率:两次独立会话均复现(每次 `ask` 都会)。

## 3.2 观测到的日志(尾部)

```
[agent] ask: 你好
[pet_view] state → 思考中
[llm] OpenAI API with tools (model: deepseek/deepseek-flash, 11476 bytes)
[agent] Dispatching response → cli:console
[Agent]: 让我查一下...
vela> [agent] reply 18 bytes
[voice] speak: "让我查一下..." (18 bytes)
[audio_pb] media_player_open(TTS) failed
[audio_pb] TTS lane unavailable (-5), trying Music
[audio_pb] media_player_open(Music) failed
[audio_pb] opened NuttX playback (/dev/audio/pcm0p, 16000Hz, 1ch, 16bit, 4 x 4096B)
[audio_pb] （同样的失败序列再来一遍）
[volc_tts] stream: API key not configured
[voice] TTS network done: 5614ms
[voice] stream failed: -2
[lvgl_ui] voice_channel_speak failed (rc=-2)
[lv_nuttx_lcd] rate: …（继续 7 个区间，约 17s）
[lv_nuttx_lcd] rate: 2300 flushes in 2349ms  ← 最后一行
```

之后:串口 **0 字节**(连发 `\r\n` 也不回显)、LVGL flush 日志同时停止、
`dmesg` 无 USB 断开、无 `/dev/ttyACM*`(CP 控制台没枚举,拿不到 CP 侧日志)。

## 3.3 已排除:CPU 没有满

`[lv_nuttx_lcd] rate:` 打印的 `flush/s` 是**累计计数 / 本区间时间**,不是瞬时速率
([lv_nuttx_lcd.c:182-191](../../apps/graphics/lvgl/lvgl/src/drivers/nuttx/lv_nuttx_lcd.c#L182-L191)):

```c
syslog(LOG_INFO, "... (%.1f flush/s)\n", s_flush_count,
       s_flush_count * 1000.0f / ms);   /* 分子是累计值,分母是区间 */
```

所以数字会从 291 一路"涨"到 979。按相邻两次的增量算,死亡前一直是
**100 次 flush / ~2.4s ≈ 42fps**,前后完全一致,没有越来越卡。
`ask` 期间 CPU 大头是阻塞等待(LLM HTTP ~10s、TTS 5.6s),不是自旋。

## 3.4 已确认的事实

- LLM 腿成功:HTTP 返回、`reply 18 bytes` 已写回 CLI;
- 死亡点不在 LLM 请求、不在 CLI 回显,而在**回复之后**;
- 挂死时 **LVGL 渲染线程和 NSH 控制台一起静默**(flush 日志与回显同时断)
  ⇒ 不是单个业务线程阻塞,是 AP 级挂死/死锁(或控制台写路径整体卡死);
- 进程侧没有任何 assert / UsageFault / panic 文本输出;
- 两次抓包都**没有** `[pet_view] state → 待机`(只到 `思考中`),说明 LVGL 线程
  没来得及处理 `set_pet_state(0)` 排进队列的回调。

## 3.5 隔离实验结果(2026-10-21 10:40,同镜像真机)

复位后按 `beep → speak → ask` 顺序跑,每步之间发空行探测 `vela>` 是否还回显:

| # | 命令 | 覆盖路径 | 结果 |
|---|---|---|---|
| 1 | `voice_test_beep` | 仅音频 open→写 200ms→close | **ALIVE**(45s) |
| 2 | `voice_test_speak 你好` | 与 `ask` 回复后**完全相同**的腿(beep+TTS 失败+播放/关闭) | **ALIVE**(60s) |
| 3 | `ask 你好` | 全链路 | **DEAD**——`[agent] reply 18 bytes` 之后 0 输出 |

第 3 步细节:这次连 `[voice] speak` 都没打出来,flush 停在
`rate: 1200 flushes`,之后控制台和 LVGL 同时静默。
(第 1 次抓包同一步是 TTS 跑完、约 20s 后才死——**死亡时机不稳定**,指向竞态而非确定性崩溃。)

**结论:音频开关/beep/TTS 失败腿本身不会挂死板子**,3.6 里原来的"音频驱动是元凶"
降级;`voice_channel_speak()` 那两处缺陷仍值得修(去 5.6s 卡顿),但不是本 bug 的充分条件。

对照已有证据(同一镜像,详见 [ui-flow-and-test.md](ui-flow-and-test.md) 附录 A 第 6 条):

| 组合 | 是否挂死 |
|---|---|
| 离线 `ask`(只有气泡,`network_is_connected()` 为假 → 不 TTS) | 9/9 正常 |
| `voice_test_speak`(只有 TTS,LVGL 空闲) | 正常 |
| 在线 `ask`(LLM/TLS + 气泡 + TTS 同时进行) | **必挂** |
| 在线 `ask` + LLM 指向连不上的地址(见 3.5.1) | **必挂** |

### 3.5.1 跳过 TLS 实验(2026-10-21,`set_llm http://127.0.0.1`)

已联网、key 保留,只把 LLM 的 base_url 指到本机不存在的服务,强制 LLM 腿
**立即失败**(不走 TLS 握手、不走 usrsock 长连接):

```
[agent] LLM call failed (iter 0) ...
[agent] reply ...            ← 错误/兜底文案仍然上屏
[voice] speak: ...
[voice] TTS network done: 20778ms
[voice] TTS stream failed: -2
[agent] Dispa…               ← 截断,之后 DEAD
```

即 **LLM/TLS 腿被排除**:LLM 请求瞬时就失败了,板子仍然后挂死;而死前
`[voice] speak` 与 TTS 依然完整跑完(≈20s),说明此时 UI(LVGL 线程建气泡)
与 TTS(outbound 线程播报)确实在并发,且与网络请求本身无关。

### 3.5.2 TTS 门控镜像实测(2026-10-21 11:02)

烧的是带 TTS 门控的镜像(`bk_loader-20261021-110109.log`:
10:59:47 `connect success` → 11:01:09 `Writing Flash OK`),在线 `ask` 仍然挂死,
但**整段日志里一条 `[voice]` 都没有**(既没有 `speak: TTS not configured, skipped`,
也没有 `[voice] speak:`)——`voice_channel_speak()` **根本没被调用到**。

⇒ 死亡点在 `lvgl_ui_channel_send()` **内部**、TTS 调用**之前**;
TTS 门控能消掉 5.6~20s 白等,但不是本 bug 的解。

### 3.5.3 仪表化镜像实测(2026-10-21 11:32)

镜像特征日志可得: `[lvgl_ui] LVGL loop prio -> 45 (rc=0)`
(= 新增的优先级设置生效),`[lvgl_ui] hb frames=100/200` 心跳正常。

在线 `ask` 的输出序列:

```
[agent] reply 18 bytes
[lvgl_ui] handler 442ms idle=0     ← 单帧异常慢(阈值 300ms)
之后:hb 停、flush 停、控制台停
```

- 没有 `ui lock wait …`(阈值 500ms)——即 dispatch 线程不是"等锁等过 500ms
  才写日志"那种形态,更像是**日志通道本身也没了**;
- LVGL 心跳与 flush 同时停 ⇒ LVGL 线程在 `lv_timer_handler()` 里不再前进;
- 全程**没有任何 assert / Panic 文本**。

### 3.5.4 根因假设:LVGL 断言 = 静默死循环

查构建配置发现两件事凑成了"静默假死":

| 配置 | 值 | 后果 |
|---|---|---|
| `CONFIG_LV_LOG_PRINTF` | **未开** | `LV_LOG_ERROR("Asserted at expression: … (Out of memory)")` 直接被丢弃(没注册 print cb) |
| `CONFIG_LV_ASSERT_HANDLER` | **未定义** | `lv_assert_handler()` 展开成 `while(1);`([lv_assert.c](../../apps/graphics/lvgl/lvgl/src/misc/lv_assert.c)) |
| `CONFIG_LV_USE_ASSERT_MALLOC / NULL` | **都开着** | 一次 `lv_malloc()` 失败或空指针就会触发 |

于是:**LVGL 线程在持着 `lvgl_ui_lock()` 的情况下进死循环** →
建气泡的 dispatch 线程(= CLI 线程)永远拿不到锁 → 控制台不回显、
LVGL 不再刷帧、没有任何报错。这解释了此前所有"整机静默"的观测,
也解释了为什么必须"在线"(联网后 TLS/LLM 占用堆,回复时再建卡片更容易 OOM)。

### 3.5.5 断言诊断镜像实测(2026-10-21 11:43)

镜像 SHA-256 `3047873…f024e7c5`(含 `lv_assert_handler → syslog + abort()`)。
在线 `ask` 仍然挂死,关键计数:**`[lvgl] ` 0 条、`assertion failed` 0 条、
`Panic` 0 条** ⇒ **不是 LVGL 断言**(断言路径会先打日志再 abort,一次都没进)。

日志尾部(截断处即死亡点):

```
[agent] reply 18 bytes
[lvgl_ui] handler 1044ms idle=0        ← 回复前那一帧异常慢(>1s)
[lvgl_ui] async: add msg (18 bytes)    ← LVGL 线程进入建卡片
                                       ← 没有 "async: add msg done",然后整机静默
```

同一轮里,用户气泡(`6 bytes`)是**完整走完**的:

```
[lvgl_ui] async: add msg (6 bytes)
[lvgl_ui] async: add msg done
```

⇒ 死亡点收窄到 **`chat_view_add_message()` 里构造 Agent 回复卡片**
(`chat_view_build_agent_card`,标题栏+分隔线+色条+正文 CJK 标签)。

**为什么"整机静默"而不是只有 UI 停**:`vela>` 提示符的读行/回显由 CLI 线程
负责,而 CLI 线程在 `lvgl_ui_channel_send()` 里要拿 `lvgl_ui_lock()`
(日志里确有 `ui lock wait 500ms (owner tid=0xb)`)。LVGL 线程卡在建卡片并
**一直持着这把锁**,CLI 线程就永远回不到读行状态 → 串口只剩 RX、没有回显。
所以"0 字节"**不能**解释成 UART 死了或 AP 崩了,必须先确认线程级状态。

## 3.6 剩余可疑点

1. ~~LVGL 断言静默死循环~~ 已被 3.5.5 排除(断言改 abort 后一次都没触发)。
2. **`lv_obj_update_layout()` 无界重跑(当前主嫌,见 3.7.2)**:布局过程自己
   会把屏幕重新标脏,尺寸互相反馈的部件组合会让
   `while(scr->scr_layout_inv)` 永不退出。对外表现就是"卡在某个 LVGL 调用
   里、没有 assert、没有 panic、帧计数停住"。已加 32 轮上限 + WARN 兜底。
3. `chat_view_scroll_to_bottom()` 的滚动/动画路径(次嫌,见 3.7.3 的 tr36-38)。
4. ~~LVGL 线程栈溢出(64KB)~~ 已被实测排除:高水位 4KB(3.7.1)。
   本构建 LVGL 用 **clib malloc**(`CONFIG_LV_USE_CLIB_MALLOC=y`),不存在
   LVGL 私有堆池,OOM 只会表现成 `lv_malloc` 返回 NULL → 断言,已被排除。

~~在线 LLM/TLS 腿~~ 已被 3.5.1 排除;~~气泡+TTS 并发~~ 已被 3.5.2 排除
(TTS 压根没被调用)。

## 3.7 下一步实验(每次挂死需物理复位一次)

1. ~~跳过 TLS~~ 已完成,见 3.5.1 → 指向并发。
2. 加 TTS 就绪门控后重烧复测(见 3.8):无 key 的板上 `ask` 完全不碰音频子系统,
   等价于把"气泡+TTS 并发"从现场移除。
   - 不挂 ⇒ 确认为并发/音频腿;
   - 照挂 ⇒ 问题在"回复气泡"本身(chat 视图异步建气泡),再往 LVGL 侧查
     (`chat_view_add_message_async_cb` 的堆分配/长文本折行)。
3. 每次挂死时**看屏幕是否还在动画**:动 ⇒ 只有输出通道死;定格/黑屏 ⇒ AP 挂死。

### 3.7.1 第一步:`trN` 步进探针 + UI 看门狗(2026-10-21 12:05 镜像,已实测)

在 `packages/ai_agent/src/ui/lvgl_ui_channel.c` 里加了三条独立线索,**同一次
烧录即可判定**:

| 线索 | 日志形态 | 说明 |
|---|---|---|
| 步进探针 | `[lvgl_ui] tr<N>` | `chat_view_add_message` / `chat_view_build_agent_card` 每个 LVGL 调用前后各一条。**最后一条 trN 之后没有 tr(N+1),那条调用就是卡住的调用** |
| UI 看门狗 | `[lvgl_ui] wd: LVGL stalled Ns` | 优先级 20(高于 LVGL 的 45),每 5s 比一次帧计数,连续 10s 不前进就报。**它还在打 ⇒ AP 与串口都活着,只是 LVGL 线程卡住**;它也静默 ⇒ AP/控制台本身没了 |
| 栈水位 | `[lvgl_ui] hb frames=N stack U/S` | 心跳里带 LVGL 线程栈高水位(`up_check_tcbstack`,需 `CONFIG_STACK_COLORATION=y`,本构建已开) |

判读表:

| 现象 | 结论 |
|---|---|
| 停在某个 `trN`,看门狗一直在报 | 该 LVGL 调用内部死循环 → 按 trN 对应到具体 API |
| 停在 `trN`,看门狗也静默 | 硬故障/整机死(栈溢出、内存踩坏)→ 查 `stack U/S` 是否逼近上限 |
| 卡前 `stack` 已接近 `S` | 64KB 栈不够,优先加栈(`AGENT_LVGL_UI_STACK`) |

#### 实测结果(镜像 `17f8ed24…f8ad596`,12:05:59 烧录,12:06 起跑)

探针改走**裸 `write(1,…)`**(避开 syslog 全局锁),一次在线 `ask` 的结果:

```
tr 序列:  10 12 13 14 15 16 17 34        ← 到 34 停,没有 35
hb frames stack 20 3904
hb frames stack 60 4008
hb frames stack 80 4008                  ← 64KB 栈只用了 4KB
async: add msg  1 次(6 字节 = 用户气泡)
add msg done    0 次
wd up / wd frames stall                  0 次  ← 看门狗一个字节都没打
```

两个新事实:

1. **这次死的是用户气泡**,不是回复卡片:`tr 34` 是
   `slot->bubble = wrap`,下一条 `tr 35` 在 `chat_view_scroll_to_bottom()`
   之后 —— **卡在滚动到底部那一步**。回复卡片(`tr 11 / 18 / 20…`)一条都没到,
   因为 CLI 线程还堵在 `lvgl_ui_lock()` 上,回复的 async 回调根本没排进去。
   (上一轮 syslog 版停在 `tr 23`,那一轮的结论不可靠:syslog 自己可能已经死。)
2. **栈溢出假设基本排除**:4KB / 64KB。
3. **看门狗静默**:`pthread_create` 的失败日志没打出来,`wd up` 也没打出来。
   在 `CONFIG_PTHREAD_STACK_DEFAULT=2048` 的配置下,看门狗线程只有 2KB 栈
   却要跑一次 `snprintf` —— 本轮已改为显式 8KB 栈 + 同时打 syslog 和裸
   write + 回读实际优先级,**下一次一定能区分"没起来 / 起来了但输出丢了"**。

### 3.7.2 根因候选(2026-10-21):`lv_obj_update_layout()` 无界重跑

代码位置 `apps/graphics/lvgl/lvgl/src/core/lv_obj_pos.c:289`:

```c
lv_obj_t * scr = lv_obj_get_screen(obj);
/*Repeat until there are no more layout invalidations*/
while(scr->scr_layout_inv) {          /* ← 没有次数上限 */
    scr->scr_layout_inv = 0;
    layout_update_core(scr);
}
```

`scr_layout_inv` 由 `lv_obj_mark_layout_as_dirty()` 置位,而**布局过程本身**
会再次调用它:`LV_EVENT_SIZE_CHANGED` / `LV_EVENT_CHILD_CHANGED` 处理函数
(`src/core/lv_obj.c:700-726`)、flex 的 `lv_obj_set_flex_grow`
(`src/layouts/flex/lv_flex.c:119`)、以及任何带 `LAYOUT_UPDATE` 属性的
style 变更(`src/core/lv_obj_style.c:297-302`)都会把父/子重新标脏。

于是只要出现一对**尺寸互相喂自己**的部件,这个 while 就永不退出:

- `LV_SIZE_CONTENT` 容器里放一个按 `lv_pct()` 取尺寸的子部件;
- flex 里 `flex_grow` 的宽度每轮从行宽重新算,和 `lv_pct(85)` 的初值打架。

本工程的气泡树正好是这种组合:

| 位置 | 参与反馈的尺寸 |
|---|---|
| 用户气泡 | `wrap` = `lv_pct(100)` / `height=SIZE_CONTENT`;`bubble` = `SIZE_CONTENT`;`lbl` = `LONG_WRAP` + `max_width`(由 `chat_list` 内容宽算) |
| Agent 卡片 | `bubble` = `lv_pct(85)` + COLUMN flex;`color_bar` = `height=SIZE_CONTENT` 在 ROW flex 里;`body_lbl` = `lv_pct(85)` **且** `flex_grow=1` |

**这条假设能解释所有观测**:LVGL 线程在持锁状态下死循环 ⇒ 帧计数停、flush 停;
CLI 线程下一个 `lvgl_ui_lock()` 永远拿不到 ⇒ 串口只有 RX 没有回显;没有
assert、没有 panic、没有踩栈(只是原地空转);死点随文本/字体测量结果变化,
所以两次分别停在建卡片和滚动(非确定性)。也解释了必须"在线":联网后
可用堆变少/尺寸测量不同,更容易落进不收敛的那一档。

### 3.7.3 本轮(12:12 镜像)的改动与判定

镜像 SHA-256 `a01a0e16…cf789f03`(build 12:12,含下列四处):

1. `chat_view_scroll_to_bottom()` 拆成三段,新增 `tr36`(强制布局前)、
   `tr37`(布局后)、`tr38`(滚动后)—— 下次直接看死点在布局还是滚动;
2. **LVGL 布局重跑加上限**:`lv_obj_update_layout()` 最多 32 轮,超限打
   `LV_LOG_WARN("layout did not converge after %u passes")` 并跳出。
   WARN 级日志本构建是开的(`CONFIG_LV_LOG_LEVEL_WARN=y`),经 log cb 落到
   syslog,形态 `[lvgl] layout did not converge after 32 passes …`。
   补丁单独存了一份(已移入选手仓):
   见选手仓 `docs/zhangyi101/patches/lvgl-layout-loop-guard.patch`
   (`apps/graphics/lvgl/.gitignore` 忽略 `/lvgl`,源码不在 git 里,重解压后
   必须重新打);
3. 看门狗线程显式 8KB 栈,`watchdog create rc=N` 必打,`wd up setrc/prio/getrc`
   同时走 syslog + 裸 write,每 5s 打 `wd frames <帧数> stall <秒> tr <最后探针>`
   —— 即使 UI 线程自己打不出字,看门狗也能报出它死在哪一步;
4. 心跳加看门狗计数:`hb frames stack <帧> <栈> wd <ticks>`,用来判断看门狗
   线程到底有没有被调度。

判定表(下一次实测):

| 日志 | 结论 |
|---|---|
| 出现 `layout did not converge`,且**不再挂死** | 根因确认 = 布局不收敛;接着按 tr 定位是哪对部件,改部件树 |
| 没有该 WARN,仍挂在 `tr36` 后 | 不是这个 while,但确实是 `lv_obj_update_layout` 内部 → 继续二分(下一层是 `layout_update_core` / flex) |
| 挂在 `tr37` 后 | 问题在 `lv_obj_scroll_to_y` 的动画路径 |
| 只有 `wd frames … tr 34` 继续涨、UI 探针不动 | AP 活着、UI 线程卡住(与上面同义,但证明串口通道没死) |
| `watchdog create rc≠0` | 线程建不出来,看门狗路线作废,换方案 |

## 3.8 代码缺陷与修复

1. ~~`voice_channel_speak()` 应先判断 TTS 是否已配置~~ **已修**:
   - `voice_tts.h` 增可选钩子 `int (*is_ready)(void)` + `voice_tts_is_ready()`;
   - `voice_tts.c` 实现 `voice_tts_is_ready()`(无 active 后端 → 0,
     否则委托后端 `is_ready`,未实现该钩子的后端视为就绪);
   - `volc_tts.c` 实现 `volc_tts_is_ready()`(重新 `volc_tts_init()` 后看
     `s_api_key` 非空)并注册 `.is_ready`;
   - `voice_channel.c` 的 `voice_channel_speak()` 在拿到 `speak_lock`、做完
     PTT 检查后立刻 `voice_tts_is_ready()`,不就绪则打
     `speak: TTS not configured, skipped` 并返回 `-ENOENT`,
     **不再 beep、不再开关音频设备**。
2. `packages/ai_agent/src/ui/lvgl_ui_channel.c` — `lvgl_ui_channel_send()` 只按
   `network_is_connected()` 决定是否 TTS,没判断 TTS 是否可用;无 key 的板上
   每条回复都会白走音频腿并卡 ~5.6s。现由 1 的早退兜住,该处可作为后续优化。

修完后的预期:未配 TTS 的板上 `ask` 不再触碰音频子系统,既不卡 5.6s,
也不再有"气泡 + TTS"并发。

## 3.9 顺带观察(非本 bug)

- 复位后 CP 侧 Wi-Fi 仍保持连接(`config_show` 直接显示
  `Network: connected / 192.168.110.233`),本次 `wapi connect` 没有打印重连日志;
  但仍建议按 §1 显式连一次再测。
- 无 key 时 TTS 会先起 beep 再失败,界面上表现为"响一声然后没下文"。

## 4 最终结论(推翻 §3.7 的"LVGL 布局不收敛"假说)

§3.7.2 的"布局循环"假说被本轮实测数据**推翻**。真根因是 **RPMSG 控制台写路径
无限阻塞**,与 LVGL 布局无关。

### 4.1 判定铁证:看门狗静默 ⇒ 不是锁/死转,是控制台写阻塞

看门狗线程(prio=20,最高实时优先)每次 tick 的**第一动作就是裸 `write(1)`**
(不取 `lvgl_ui_lock`、不走 syslog、不走 log cb)。它静默只有一种解释:连
`write(1)` 本身都返回不了。而健康线程(裸 `putreg32` 心跳寄存器)继续跑,所以
"[AP] stalled" 从没触发 —— 调度器活着,是**控制台写系统调用卡死**,不是某个
线程空转、不是拿不到锁。

这直接排除了 §3.7 的三条候选:

| §3.7 假说 | 为何不成立 |
|---|---|
| LVGL 线程持锁死循环 | 看门狗不取锁,它也会卡,只能怪它自己最后那句 `write(1)` |
| `lv_obj_update_layout` 不收敛 | 若只是 UI 线程空转,看门狗(独立线程)照常打 `wd frames` |
| 串口通道死(只 RX 不回显) | 看门狗裸写都不回,方向一致,但源头在 AP 侧 `dmasend`,不在 CP 侧 |

### 4.2 卡死链路

1. AP `/dev/console` = `uart_rpmsg`(`CONFIG_RPMSG_UART_CONSOLE`),与 usrsock 的
   LLM 网络流量共用**同一条 AP→CP RPTUN 链路**的 TX 缓冲池
   (`BK7258_RPTUN_VRING_NUM=4` × 2048B = 4 个 TX buffer/方向)。
2. `uart_rpmsg_dmasend()` 调 `rpmsg_get_tx_payload_buffer(&ept,&space,true)`。
3. `wait=true` 在 BK7258 上是**无限**等,不是文档说的 ~15s:
   - OpenAMP `rpmsg_virtio_get_tx_payload_buffer` 的循环只在
     `notify_wait` 返回 `-EOPNOTSUPP` 时才递减 tick_count;
   - NuttX 侧 `rpmsg_virtio_notify_wait()` 把超时 `-ETIMEDOUT` **映射回 0(成功)**:
     ```c
     ret = rpmsg_tickwait(...);
     return ret == -ETIMEDOUT ? 0 : ret;
     ```
   - 于是循环以为"刚被唤醒、再等等",tick_count 永不递减、永不返回 NULL —— 池子
     一旦被 usrsock 大流量占满,控制台写就**永久卡死**。
4. 级联:LVGL 线程持 `lvgl_ui_lock()` 在 `lv_timer_handler()` 里打印 → 阻塞 ⇒
   `agent_loop` 的 `set_pet_state(0)`(待机)拿不到锁 ⇒ 看门狗自己的 `write(1)`
   也阻塞。日志停在"思考中","待机"永不出现 —— 正是每次 `ask` 的表现。

"必须在线才复现"也顺理成章:离线时 usrsock 没流量,池子不空,控制台从不卡;
一联网,LLM 请求/响应的 RPMSG 碎片与海量 syslog 争抢这 4 个 buffer,耗尽即死。

### 4.3 修复(BUG-20261021-01,已编译)

`nuttx/drivers/serial/uart_rpmsg.c` 的 `uart_rpmsg_dmasend()`:
`rpmsg_get_tx_payload_buffer(..., true)` → **`false`**。池空时 `dmatx.length=0`
直接丢弃本次 DMA 尝试(tail 不动,`uart_xmitchars_dma()` 稍后自动重试),
控制台变成**可丢字的无损阻塞**——宁可偶尔丢几行日志,也不让 AP 因写控制台
而死机。

改动只影响控制台写路径,CP 侧 bridge 的 `dmasend` 基本不走、无害。

### 4.4 若验证后仍挂的下一嫌疑(备查)

控制台这条路如果还有卡点,是 usrsock 客户端与 CP 服务端的**其它 `wait=true`**
(同样是无限等):`usrsock_rpmsg.c` 的 `usrsock_request`、CP 侧
`usrsock_rpmsg_server.c` 的 `recvfrom_handler` 等。根修可让 NuttX
`rpmsg_virtio_notify_wait()` 超时时返回 `-ETIMEDOUT` 而非 0(这会让所有
`wait=true` 调用变回真正有界)。
