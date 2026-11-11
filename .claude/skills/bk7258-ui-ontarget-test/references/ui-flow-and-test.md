# UI ↔ Agent 流程与验收(真机 BK7258)

> 2026-10-30 由 `ui-agent-interaction-flow.md` + `ui-screen-transition-order.md` 合并而成,
> 两份原文已删。合并时去掉了重复的命令表、PTT 流程图、通知/审批覆盖规则各一份,
> 并更正了附录 A 里已过时的缺陷⑦。
>
> 依据代码整理至 2026-10-26 镜像(含待机首按门控 + 应用侧 SNTP + 远控后端活性 +
> 通知页取代 toast + 形象整机固定一只);2026-10-30 真机复验补充。
>
> 本文回答两件事:**它是怎么运转的**(机制)、**按什么顺序测、每步应该看到什么**(观察点)。
> **「什么算通过」的判据只有一份**,在 [main_loop.md](main_loop.md) 第 0 节「正确功能验证」+
> `func-test.sh`;本文的「观察点」是日志/画面长什么样,不另立判据。

---

## 0. 一句话概括

> 任何输入(键/语音/终端)→ message_bus → agent_loop(NL 快路径或 LLM ReAct)→
> outbound → 屏上气泡 + TTS 播报;60s 无活动淡入待机时钟,首按只唤醒,
> 第二下才按「当时有没有审批」分派到滚动或通过/拒绝。

---

## 1. 屏幕是分层的(同一个 LVGL 根屏)

```
        ┌──────────────────────────────────────────────┐
        │  待机时钟层  standby_view       (60s 无活动) │  ← 最上层,淡入/淡出
        ├──────────────────────────────────────────────┤
        │  审批页  approval  (WAITING 时)              │  ← 有挂起审批时覆盖
        ├──────────────────────────────────────────────┤
        │  通知页  notice     (5s,可按键退出)          │  ← 审批在场时排队,只留最新
        ├──────────────────────────────────────────────┤
        │  聊天屏 lvgl_ui_channel  (气泡/卡片)         │
        │  宠物主页 pet_status_view                    │  ← 聊天 8s 无消息后「让位」给它
        └──────────────────────────────────────────────┘
```

覆盖优先级:**待机时钟 > 审批页 > 通知页 > 聊天屏/宠物主页**。

- 每个覆盖页都是独立根屏(`lv_obj_create(NULL)`),用淡入切屏,不动底下的页面。
- 通知页在审批页在场时不抢屏,只记下最新一条,等审批收起再上屏;
  待机时钟在场时来的通知直接丢弃。

注意:**「息屏」不是关背光**,而是暗色时钟覆盖层。背光、LVGL 线程、LCD flush 一直在跑,
所以唤醒没有点屏延迟。

页面一共 5 个:宠物主页 / 聊天屏 / 审批页 / 通知页 / 待机时钟。
**粒子流页已彻底移除(2026-10-23)**:代码、Kconfig、构建入口一并删掉,
`set_page 1` 现在返回错误。宠物首屏是唯一的 idle 页面,常驻。

---

## 2. 一条消息的完整回路(文本与语音共用)

```
 S1 按住/松开            终端 ask 文本
      │                      │
      ▼                      ▼
 voice_channel        nsh_commands.cmd_ask
 (ASR 流式识别)        (channel="cli")
      │ 识别文本              │
      ├──► 用户气泡 send_user ◄┤
      │                      │
      ▼                      ▼
 message_bus_push_inbound  ──► agent_loop_task
                                   │
                     ┌─────────────┴──────────────┐
                     │ 1) NL 快路径命中关键词?     │
                     │    (几点了/天气/放首歌…)     │
                     │    同步执行工具              │
                     │    宠物: 3 执行 → 0 待机     │
                     ├─────────────────────────────┤
                     │ 2) 否则走 LLM(ReAct)        │
                     │    宠物: 2 思考               │
                     │      └► 有 tool_calls:        │
                     │          3 执行 → 2 思考(每轮) │
                     │    收到最终答案              │
                     └─────────────┬───────────────┘
                                   ▼
                        dispatch_response → outbound 队列
                                   │
              ┌────────────────────┴───────────────────┐
              ▼ channel="cli"                          ▼ channel="voice"
        终端打印 + 助手气泡                       voice_channel_speak(TTS)
        (lvgl_ui_channel_send)                   TTS 失败 → 宠物 5 红+抖
              │
              ▼ 8s 无新消息
        聊天屏让位 → 宠物主页
              │ 再 60s 无活动
              ▼
        待机时钟
```

要点:

- **`ask` 只出气泡不出声;PTT 只出声**。用户说的话在两种模式都会以右侧气泡上屏。
- NL 快路径没有 THINKING 阶段(工具同步执行完才返回),所以只看到 3 → 0。
- LLM 被门控(无网/未配模型)时不会静默:聊天屏直接出一张说明卡片(见 §9 E)。

---

## 3. 息屏、唤醒与按键分派

### 3.1 首按吞掉

```
 任意活动(bubble / PTT / 状态变化 / 翻页 / 审批 / 滚动)
      │  ui_activity_reset(): 重置 60s 倒计时,若时钟在显示则淡出
      ▼
 ┌──────────┐   60s 无活动,且宠物状态 == IDLE
 │ 正常界面 │ ───────────────────────────────────────► ┌────────────┐
 └──────────┘         (LISTENING/THINKING/EXECUTING/    │ 待机时钟屏 │
      ▲                ERROR 会持续顺延,不打断动画)      └────────────┘
      │                                                      │
      │                                          S1/S2/S3 按下(任意键)
      │                                                      ▼
      │                                        gesture_fire()
      │                                          └► s_wake() = wake_from_standby()
      │                                                └► 时钟淡出、倒计时重置、返回 1
      │                                                      │
      └────────────── 本次按键被吞掉,不派发 ─────────────────┘
                       (所以唤醒用的 S2 不会顺手答掉审批/滚动)
```

### 3.2 两道门 + 两套键表

派发前有两道「吞按键」的门,顺序固定:

```
 按键 → ①通知页在屏上? → 是:关通知页,本次按键结束(不翻页、不答审批)
          └ 否 ↓
        ②待机时钟在屏上? → 是:点亮,本次按键结束
          └ 否 ↓
        ③按当时有没有审批,走 idle 键表或 approval 键表
```

```
 无审批(idle 键表)                     有审批(approval 键表,自动压栈)
 ────────────────────                  ──────────────────────────────
 S2 短按 → 聊天上滚                     S2 短按 → 通过一次 (approve once)
 S3 短按 → 聊天下滚                     S3 短按 → 拒绝     (deny)
 S2 长按 → 切下一个模型                 S2+S3 同按 → action_cancel(目前只打日志)
 S3 长按 → 开新会话
 S1 按住 → 录音(独立线程,见 §4)
```

键表在 `pending()` 为真时每 20ms 轮询自动压栈/出栈,不依赖界面事件通知。

> 板上键位:**K1 = RESET(接 CEN,别当功能键,按下去整板重启)**;S1 = PTT;S2/S3 = 用户键。

**滚动只在有对话时拉聊天屏**(2026-10-20 修复):此前 S3 唤醒会无条件切到聊天屏,
空对话时就是一块近黑/灰的空屏;同时让位判据用的是宠物屏缓存的 `visible` 标志,
而聊天屏接管前台时没人清它,导致 8s 让位永不触发、空屏一直挂着。现改为:
滚动仅在 `history.count > 0` 时拉聊天屏;让位改用 `pet_status_view_is_active()`
(实时比较 `lv_screen_active()`);`set_page` 同步清 `screen_visible`。
代码见 `lvgl_ui_channel_scroll()`、`pet_idle_timer_cb()`、`pet_status_view_is_active()`。

聊天屏空闲 8s 无新消息自动让位回宠物首屏(`[lvgl_ui] idle → pet home screen`)。

---

## 4. 录音 → 识别 → 回复(PTT 一轮)

**界面顺序(观察点)**:宠物 LISTENING(青环旋转、眼朝下,PTT 按钮红脉冲 +「录音中...」)
→ 松开 → 宠物 THINKING(蓝环旋转、眼朝上,「识别中...」)→ 聊天屏右侧蓝色用户气泡
→ 左侧 Agent 回复卡片(色条分类)→ 宠物回 IDLE → 8s 让位回宠物首屏。

色条分类:绿=结论 / 蓝=结构化 / 橙=Jira / 灰=普通。

```
  S1 按下                          S1 松开
    │                                │
    ▼                                ▼
 voice_channel_start()          set_ptt(PROCESSING)
 打开采集/ASR 流                  宠物 2 思考
    │                                │
 set_ptt(RECORDING)             voice_channel_stop_with_text()
  宠物 1 听音(青环、眼睛向下)    阻塞在这个专用线程里,等 ASR 收尾
    │                                │
    │                           ┌────┴─────────────────────┐
    │                       识别为空                    识别失败
    │                           │                          │
    │                       直接结束               红色错误卡
    │                                                    "语音识别不可用:
    │                                                     请检查网络和配置"
    ▼                                                     │
  (录音期间 TTS 会被拒绝,                                 ▼
   避免录到自己的播报)                                结束
                                 │ 识别成功
                                 ▼
                        用户气泡 + voice_channel_dispatch()
                                 │
                         (回到 §2,channel="voice")
                                 │ 回复
                                 ▼
                        TTS 播报(16k)→ set_ptt(IDLE) 宠物 0
```

`g_ptt_busy` 保证「上一轮还没处理完时再按 S1 被忽略」,不会并发开会话。

> **UI 的 PTT 只能靠物理 S1**。CLI 的 `voice_start` / `voice_stop` 只驱动音频/ASR 通道,
> 不驱动 UI(`lvgl_ui_channel_set_ptt` 只在 `ptt_button.c` 的物理按键回调里发);
> 用 CLI 测只能在串口看日志,屏上全程 IDLE。

---

## 5. 审批与通知

### 5.1 审批的流转

审批来自**远控通道**:PC 上的 coding agent 要跑一个需要许可的工具
(编译 / commit / push)时,经 bridge 发来 `approval required: <tool> <hint>`。
板上不执行这些工具,只把人的决定回传 —— 完整分工见 §8。

```
 远端发来 "approval required: <tool> <hint>"
              │
              ▼
 remote_ctrl_channel.on_session_event(REMOTE_EVENT_STATE)
   ├─ 控制台打 [需要审批] <tool>  → 按 S2 通过 / S3 取消
   ├─ lvgl_ui_channel_set_approval()
   │     ├─ 宠物进入 4 WAITING(半闭眼琥珀),并存下原状态
   │     ├─ 审批页浮现
   │     └─ 按键盘压入 approval 层
   └─ notify_observer 同步给通知服务
              │
     ┌────────┴─────────┐
     ▼                  ▼
 S2 短按通过         S3 短按拒绝
 remote_ctrl_channel_decide(ONCE/DENY)
     │                  │
     └───────┬──────────┘
             ▼
   远端收到决定,工具继续/中止
             │
   has_prompt=false(本地决定 / 超时 / 面板端已处理)
             ▼
   clear_approval(): 审批页退出,宠物恢复之前状态
   (若此前是在执行工具 → 回 3;若只是 rpending 调试 → 回 0)
```

三条容易踩的规则:

1. **审批等待时不再进待机**(2026-11-02 修复):WAITING 是等人做决定的活问题,
   和 LISTENING/THINKING/EXECUTING/ERROR 一样持续顺延待机倒计时;待机只在
   IDLE 累计。真机 `rpending` 后静置 65s 无 `standby clock`。旧行为把审批页
   暗掉会吞掉「按 S2/S3 答复」的第一下(只唤醒),故改为不再暗掉。
2. **`rpending` 造的是假审批**(`g_debug_pending`):S2/S3 只在本地关掉并弹 toast;
   `ronce`/`rdeny` **关不掉**它(它们走 `remote_ctrl_channel_decide()`,不认 debug 标志),
   会打印 `No approvable request is pending` 且审批页继续挂着。
   **唯一清掉方式 = 物理 S2/S3 单击**(走 `remote_ctrl_decide_from_input()`)。
   `set_page 0` 只把画面切回宠物屏,`approval_visible`/`g_debug_pending` 仍为真:
   通知照旧排队(`deferred (approval page up)`)、待机照旧被顺延 —— 它**不是**真正的 escape。
   真审批反过来 —— S2/S3 与 `ronce`/`rdeny` 都行。
3. `rpending` 只用于调试,**不能拿它证明「审批能批」**;真审批必须由 PC 侧 agent 发起。

### 5.2 通知页的流转

后台轮询(Gerrit / Jira)发现新的变更后,`push_notify()` 调
`lvgl_ui_channel_show_notice(title, body, color)`;屏上走的是一个独立页,
而不是屏顶小 toast(圆表盘太窄,body 会被截断)。

```
 notify 轮询发现新条目 / notify_inject 本地注入
              │
              ▼ show_notice() —— 异步投到 LVGL 线程
    ┌─────────┴──────────┬───────────────────────┐
    ▼                    ▼                       ▼
 审批页在屏上?        待机时钟在屏上?        都不在
    │是                  │是                     │
    ▼                    ▼                       ▼
 排队(只留最新一条)    丢弃(不打扰待机)      通知页淡入
 等审批收起再上屏                                │
                                       ┌───────┴────────┐
                                       ▼                ▼
                                  5s 自动收起       任意键退出
                                  (回原页面)       (按键被吞掉)
```

### 5.3 覆盖规则汇总

| 场景 | 结果 |
|---|---|
| 通知页 vs 审批页 | 审批优先:通知先排队,只留最新一条,等审批收起再上屏 |
| 通知页 vs 通知页 | 后到覆盖:换新内容并**重新计时**(按长度缩放),不排队 |
| 通知页 vs 待机时钟 | 时钟在屏时通知直接丢弃(不打扰待机) |
| 审批页 vs 待机时钟 | 审批等待时待机被顺延,时钟**不盖**审批(见 §5.1 规则 1) |

要点:

1. **通知页算用户活动**:上屏时 `ui_activity_reset()`,看一眼通知不会立刻被时钟盖住。
2. **按键退出是「吞掉」**:这一下既不会滚聊天,也不会答审批;下一次按键才回到键表。
3. 底层接口:`lvgl_ui_channel_show_notice()` / `_notice_dismiss()` / `_notice_visible()`;
   停留时长由 `notice_duration_ms()` 按 title+body 字符数缩放,`LVGL_UI_NOTICE_MS`(10000)
   为下限、30s 为上限(不再是固定 5s)。
4. `notify_inject` 是**注入**,只能证明「页面画得出来、能退出」;真事件走 MCP 轮询
   (需先 `mcp_add gerrit/jira`)。判据口径见 [main_loop.md](main_loop.md) §0。

---

## 6. 宠物状态与形象

| 值 | 状态 | 谁触发 |
|---|---|---|
| 0 | IDLE 待机 | 一轮处理结束、PTT 收尾 |
| 1 | LISTENING 听音 | PTT 录音中(`set_pet 1` 可单独点灯) |
| 2 | THINKING 思考 | 进入 LLM、工具结果回来后 |
| 3 | EXECUTING 执行 | ReAct 每轮工具执行前 / NL 快路径工具执行前 |
| 4 | WAITING 等待 | 审批页出现(覆盖层,退出时恢复原态) |
| 5 | ERROR 错误 | TTS 失败、ASR 不可用等 |

审批期间 `lvgl_ui_channel_approval_pending()` 为真,快路径收尾**不会**把 WAITING 冲成 IDLE。

### 6.1 形象:整机固定一只

- 形象 4 种:**0 白猫 / 1 青蛙 / 2 棕狗 / 3 粉猪**(黑猫已删)。
- **每次烧录后的首次启动随机定一只**,写进配置键 `pet_type`,之后每次重启都用同一只;
  运行中切页面、切状态都**不换形象**,只换表情(6 态各自的表情帧)。
- 配置在整片烧录时被清空,所以「随机」只发生在烧录之后 ——
  这也意味着**一次验收只看得到一只**。
- 调 `set_ptype <0-3>` 会改并写盘(调试用);不带参数只回显当前编号,验收时用它
  确认「切页面后形象没变」。

---

## 7. 界面切换总顺序(主线)

> 标准测试流程的骨架:**每个箭头(一次界面切换)就是一个测试点**。
> 本节只排顺序与界面长什么样;「什么算过」见 [main_loop.md](main_loop.md) §0。

```
① 开机 / 冷启动
   └─ 宠物首屏:本次烧录固定的那只宠物 + 状态环(IDLE 绿环满、呼吸、眼睛居中)+ 三行按键说明
      │  累计空闲 60s(且宠物在 IDLE 态)
      ▼
② 待机时钟:暗色大号 HH:MM + 日期 + "待机中",内容层淡入
      │  任意交互(按键 / 命令 / 消息)
      ▼
③ 唤醒 → 切回 ① 宠物首屏
```

| 阶段 | 界面/画面 | 关键视觉 |
|---|---|---|
| ① 宠物首屏 | 本次烧录固定的宠物(像素画)+ 状态环 + 三行按键说明 | IDLE=绿环满/呼吸/眼居中;说明=S1 按住说话 / S2 长按切换模型 / S3 长按新建会话 |
| ② 待机时钟 | 大号 HH:MM + 月日周 + 「待机中」 | 暗色、仅内容层淡入 |
| ③ 唤醒 | 回到进入待机前的页(宠物) | 瞬时切回 |

### 7.1 主线观察点(逐条)

> 冷启动后串口停在 `nsh-ap>`。除第①步在 `nsh-ap>` 下敲 `ai_agent` 外,
> 其余命令都在随后出现的 `vela>` 提示符下敲。

| # | 主线步骤 | 指令 / 动作 | 预期日志(观察点) | 预期界面 |
|---|---|---|---|---|
| ① | 开机 → 宠物首屏 | `nsh-ap>` 下敲 `ai_agent` | `[pet_view] init OK` → `[pet_view] pet screen shown` | 固定形象 + IDLE 绿环满/呼吸/眼居中 + 三行按键说明;提示符变 `vela>` |
| ② | 空闲 → 待机时钟 | 不按键、不发命令,累计空闲 60s | `idle 60000ms → standby clock`、`[standby_view] standby shows …` | 暗色大号 HH:MM + 日期 +「待机中」 |
| ③ | 待机 → 唤醒 | `set_page 0`,或按 S2/S3 单击 | `[standby_view] standby clock hidden`、`[key] standby: keys=N gesture=0 -> wake (swallowed)` | 切回宠物首屏(IDLE);有对话时 S3 才打开聊天屏 |
| ④ | 唤醒 → 回空闲 | (无需指令,宠物首屏常驻) | (无) | 宠物首屏常驻 IDLE,不再自动轮换页面 |

---

## 8. 远控目标模式:板上只批三步

> 设计文档:`contest2026_272_tokenwujixian/docs/zhangyi101/remote-goal-mode.md`
>
> **分工**:板上不读代码、不改代码、不编译;PC 上的 coding agent 完成
> 「读 → 改 → 编译 → commit → push」,板上只做三件事:**发目标、看状态、
> 在编译/commit/push 前批一次**。

### 8.1 一次目标的回路

```
 rask <目标>  ──►  PC: coding agent 读/改/编/commit/push
                        │ 需要许可时发 ACP session/request_permission
                        ▼
 板上 Attention + 审批页 ◄── bridge 把 prompt 随 snapshot 下发
     │ S2 通过 / S3 拒绝(或 ronce / rdeny)
     ▼
 bridge 回传决定 → agent 继续/中止 → turn_result
     ▼
 板上 Result → rreply 看完整回复
```

### 8.2 命令与观察点

| 阶段 | 命令 / 动作 | 观察点 |
|---|---|---|
| 看链路 | `rstat` | 有 `=== Remote agent ===`;`State` **不是** `Offline` |
| 看后端 | 同上 | `Backend : up`(第三条健康线) |
| 发目标 | `rask <目标>` | 打印 `Prompt sent.`;若 `Error: …` 按其内容处理 |
| 等审批 | 屏上审批页 + 终端 `[需要审批]` | `rstat` 出现 `Approval needed for: <tool>` |
| 批 | **S2** 通过 / **S3** 拒绝(或 `ronce` / `rdeny`) | `Approved once.` / `Denied.` |
| 看结果 | 等 `[任务] completed` / `[任务] failed` | `rreply` 打印本轮回复 |
| 一键跑 | `./run-ui-flow.sh remote` | `REMOTE_GOAL` 换目标,`REMOTE_WAIT` 调等审批上限 |

### 8.3 三条健康线(故障表现完全不同,分开看)

1. **broker ↔ bridge**:bridge 每 10s 发一次 snapshot;设备 30s 收不到就回
   `Offline`。判据 = `rstat` 的 `State`。
2. **bridge ↔ coding agent**:bridge 进程活着不代表 agent 活着。bridge 在
   agent 退出时把 `backend=down` 随 snapshot 下发;此时 `rask` **本地拒绝**
   并提示「PC coding agent is not running」。判据 = `rstat` 的 `Backend`
   (`up` / `down` / `unknown`;`unknown` 是旧 bridge 没上报,不阻塞发送)。
3. **一轮本身**:`rstat` 的 `turn_active` / `Attention`。审批超时由 PC 侧
   `DESKMATE_PERMISSION_TIMEOUT_MS` 决定,**单位是秒**(默认 30,演示建议 300)。

### 8.4 四个坑

- **可能根本不弹审批**:审批只在 PC agent 真的发出 ACP
  `session/request_permission` 时出现;MiMoCode 有「工具跑完不上报」的已知
  问题。验收前先预检:让 agent 明确编译或 `git commit`,确认出现
  `Approval needed for:`;没出现就别指望板上能批。
- **`rmode` / `rmodel` / `rnew` / `ruse` 对当前 bridge 无效**:bridge 只实现
  `hello` / `permission` / `usage_query` / `prompt_submit`,其余返回
  `unsupported command`,设备端不分派该错误 → 静默无效。尤其 `rstat` 的
  `Mode` 只是**本机最后一次请求值**,不代表已生效。
- **`rpending` 是假审批**(§5.1):只有 S2/S3 能答,`ronce`/`rdeny` 答不了;
  远控的**真**审批反过来——S2/S3 与 `ronce`/`rdeny` 都行。
- **`rask` 是非阻塞的**:它只表示「目标已下发」,不是「已完成」;完成后要看
  `[任务] completed` 或 `rstat` 回 `Idle`,再用 `rreply` 取回复。

---

## 9. 交互支线观察点(逐条)

> 前置:串口在 `vela>`;`net_status` 显示联网(语音/文字两条腿都要出网);
> `config_show` 里 volc key / ASR 凭据 / LLM 已配置。
>
> **整片烧录(offset 0x0)会清空 NVS 配置** —— 烧完必须重新联网并配 key,
> 否则 A/B 支线全部跑不了。另:AP 的 `/data` 是 tmpfs,**重启(不只是烧录)也会丢配置**。

### A. 文字(ask)——最短的端到端链路

| # | 动作 | 指令 / 操作 | 预期日志(观察点) | 预期界面 |
|---|---|---|---|---|
| A1 | 发一句话 | `ask 你好,用一句话介绍你自己` | `Sent to agent: ...`、`[agent] ask: ...` | 宠物 THINKING(蓝环旋转、眼朝上),随即切聊天屏 |
| A2 | 用户回显 | (自动,`cmd_ask` 已补 `lvgl_ui_channel_send_user`) | (无) | 右侧蓝色「用户气泡」= 刚发的话 |
| A3 | Agent 回复 | (等 3~20s) | 回复相关 `[agent]` 日志 | 左侧「Agent 回复卡片」(色条分类),宠物回 IDLE 绿环 |
| A4 | 让位 | 静等 8s 不碰按键 | `[lvgl_ui] idle → pet home screen` | 回宠物首屏 |

### B. 语音(PTT)——**必须用板载 S1**

| # | 动作 | 指令 / 操作 | 预期日志(观察点) | 预期界面 |
|---|---|---|---|---|
| B1 | 按住说话 | 物理按住 S1,说 2~4s | `[ptt_btn] PTT press → recording` | 宠物 LISTENING(青环旋转、眼朝下) |
| B2 | 松开 | 物理松开 S1 | `[ptt_btn] PTT release → ASR`、`[voice] ... ASR finish: <ms>ms` | 宠物 THINKING(蓝环),随后见 B3 |
| B3 | 识别成功 | (自动) | `[ptt_btn] recognized: <文字>` | 聊天屏右侧用户气泡;之后同 A3/A4 |
| B4 | 识别为空 | 按住但不出声 | `[ptt_btn] no speech recognized` | **回 IDLE,不报错** |
| B5 | ASR 故障 | 未配 key / 无网时按住说话 | `[ptt_btn] voice_channel_stop_with_text failed rc=-2`(失败码现已上抛) | 红条卡片「语音识别不可用…」+ 宠物 ERROR |

失败时:`[volc_asr] ASR API key not configured` → `set_volc_key <api_key>`、
`set_volc_asr <app_id> <token> <cluster>`,必要时 `set_voice_asr` 选通道。

### C. 审批(rpending,假审批)

> 只用于调试键表;**真审批必须由 PC 侧 agent 发起**(§8),`rpending` 不能当验收证据。

| # | 动作 | 指令 / 操作 | 预期日志(观察点) | 预期界面 |
|---|---|---|---|---|
| C1 | 造一个待审批 | `rpending fetch_weather 要查天气吗` | `Simulated approval pending: fetch_weather` | 审批页:tool 名 + hint |
| C2 | 通过一次 | 物理 **S2 单击** | `[key] approval: keys=2 gesture=0 -> hit`、`Approved once (debug).` | toast「已通过」,淡入回 C1 之前的界面 |
| C3 | 取消 | 再 `rpending ...` 后 **S3 单击** | `Denied (debug).` | toast「已取消」,回原界面 |

### D. 按键(滚动 / 模型 / 新会话)

| # | 动作 | 指令 / 操作 | 预期日志(观察点) | 预期界面 |
|---|---|---|---|---|
| D1 | 上滚 | 有对话后 S2 单击 | `[key] idle: keys=2 gesture=0 -> hit` | 聊天屏上滚;无对话时只唤醒、不切屏 |
| D2 | 下滚 | S3 单击 | `[key] idle: keys=4 gesture=0 -> hit` | 同上 |
| D3 | 换模型 | S2 长按(≥0.8s) | `[key] idle: keys=2 gesture=1 -> hit` + `Model -> <新模型>` | 屏上 toast「已切换到 …」 |
| D4 | 新会话 | S3 长按 | `[key] idle: keys=4 gesture=1 -> hit` + `New session requested.` | 屏上 toast「已开启新会话」 |
| D5 | 取消 | S2+S3 同时按 | `[key] global: keys=6 gesture=3 -> hit`、`[key] cancel` | (目前只打日志) |

> `keys=2`=S2(KEY_BIT(1))、`keys=4`=S3(KEY_BIT(2));gesture 0=单击 1=长按 3=组合。
>
> **`-> hit` 只说明按键被收下了,不说明功能生效**。D3/D4 的真实效果依赖 bridge 下发的
> 模型表和远控会话,网不通时会打 `No model list to cycle` /
> `New-session request failed`;D1/D2 要有聊天记录才拉得起聊天屏。

### E. 无网 / 未配置模型(开机即启动的降级路径)

> 整片烧录后 NVS 被清空,开机就是「无网 + 无模型」状态,正好是本节的天然前置。
> agent 循环是**开机即启动**(串口启动日志多一行
> `[boot +…ms] P5: agent loop started` / `[agent] Agent loop started, free heap: …`),
> 不再等 `Network connected`。

| # | 动作 | 指令 / 操作 | 预期日志(观察点) | 预期界面 |
|---|---|---|---|---|
| E1 | 无网发文字 | `ask 你好` | `Sent to agent: 你好` → `[agent] Processing message from cli:console` → `[agent] llm unavailable for cli:console: 未联网：请先连接 Wi-Fi，设置好网络后再试。` → `[Agent]: 未联网：…` | 右侧用户气泡「你好」+ 左侧 Agent 卡片「未联网：请先连接 Wi-Fi，设置好网络后再试。」 |
| E1' | 同上(真机已见) | `ask 你好` | `[agent] Processing message from cli:console` → `[agent] reply 60 bytes` → 离线时**不再尝试 TTS**(`network_is_connected()` 门控) | 卡片先排入 LVGL 异步队列再播 TTS,故先上屏 |
| E2 | 看宠物状态 | 静等 8s 不按键 | `[lvgl_ui] idle → pet home screen` | 宠物 **ERROR**(红环/抖动/眼睛 X);状态文字**不是**通用「出错了」,而是 `未联网 / 请先连接 Wi-Fi` |
| E3 | 联网但未配模型 | `nsh-ap>` 下 `wapi connect <ssid> <pw>`,等 `ifconfig` 有 wlan0 IP、`ai_agent` 后 `net_status` connected;**不**执行 `set_llm`;再 `ask 你好` | `[agent] llm unavailable for cli:console: 模型未配置：请先用 set_llm 配置模型。` | 卡片变为「模型未配置：请先用 set_llm 配置模型。」,宠物 ERROR 文字变为 `模型未配置 / 请用 set_llm 配置` |
| E4 | 无网/无凭据按 PTT | 按住 S1 说 2s 再松开 | `[ptt_btn] voice_channel_stop_with_text failed rc=-2` | 红条卡片「语音识别不可用：请检查网络和语音识别配置。」+ 宠物 ERROR,文字 `语音识别不可用 / 请检查网络和凭据` |
| E5 | 恢复配置 | `wapi connect`(nsh-ap>)+ `set_llm` + `set_volc_key` | 之后 `ask` / S1 恢复正常链路 | 正常回复卡片,宠物 THINKING→IDLE |

> 卡片出现即算「有反馈」;E1/E3 的卡片是 Agent 卡(左侧色条),
> E4 是强制红条(`0xff5252`)。若只看到 `Sent to agent:` 而没有
> `[agent] Processing message`,说明 agent 循环没起来,先查启动日志里
> `agent loop started`。
>
> **宠物屏必须写清原因**:聊天屏 8s 让位回宠物屏后,ERROR 态的状态文字是具体原因:
> `未联网 / 请先连接 Wi-Fi`、`模型未配置 / 请用 set_llm 配置`、
> `语音识别不可用 / 请检查网络和凭据`。实现:`llm_blocked_reason()` 同时给出
> 长文案(聊天卡)与短文案(宠物屏),经 `lvgl_ui_channel_set_pet_error()` →
> `pet_status_view_set_error_text()` 异步写入;离开 ERROR 自动清空,避免旧原因串台。
>
> `lvgl_ui_channel_send()` 把 TTS 放在气泡入队**之后**,且仅在
> `network_is_connected()` 时调用。无网时不再尝试 TTS(不会出现
> `[voice] speak` / ~20s `TTS network done` 阻塞),气泡仍立即入 LVGL 异步队列。
>
> **ERROR 态的两个坑**(E2 之后接着做待机段时一定会撞上):
> 1. **ERROR 没有自动回落定时器** —— 它保持红环/抖动,直到下一次状态设置
>    (再按 S1、Agent 回复、`set_pet 0`)。老文档写的「稍后回 IDLE」不成立。
> 2. **ERROR 会永久阻止进入待机** —— 待机计时只在 IDLE 累计
>    (`standby_timer_cb` 在 `s_last_pet_state != 0` 时每秒重置活动时间)。真机复现:
>    无网 `ask` 后停在 ERROR,静置远超 60s 也不出时钟;`set_pet 0` 后 60s 即
>    `idle 60000ms → standby clock`。所以要验待机,先清掉 ERROR。
>
> 已联网、模型已配但**请求本身**失败(密钥错 / 超时)回的是
> 「请求失败:请检查网络或模型配置。」(2026-10-20 由英文
> `Sorry, I encountered an error.` 改为中文),与上面的前置拦截文案区分开。

### F. 状态点灯(6 态 + 固定形象)

| # | 动作 | 指令 | 预期日志(观察点) | 预期界面 |
|---|---|---|---|---|
| F1 | 逐个点灯 | `set_pet 0` … `set_pet 5` | `state → 待机/聆听中/思考中/执行中/等待确认/出错了` | 依次换 6 个表情 |
| F2 | 读固定形象 | `set_ptype` | `pet type: <0-3>` | (终端) |
| F3 | 页面来回切 | `set_page 0` → `show_chat` → `set_page 0` | `pet screen shown` | 宠物页 ↔ 聊天屏 |
| F4 | 再读形象 | `set_ptype` | 编号与 F2 相同 | **形象不随页面变** |

> IDLE→IDLE 不重置活动时间,`set_pet 0` **唤不醒**待机;唤醒用 `set_page 0` 或按键。

---

## 10. 命令速查(唯一一份)

> **密钥/配置的唯一来源:`../scripts/cmd_scripts.sh`。**
> 要联网、配模型、配语音,按命令名去那个文件里取**整行原样下发**;
> 不要在本文、脚本或聊天里另存一份 key(控制台回显会把整行命令打出来)。
> 查行号:`../scripts/func-test.sh --lines`,或 `grep -n '^set_llm' ../scripts/cmd_scripts.sh`。

| 命令 | 作用 |
|---|---|
| `ai_agent` | 在 `nsh-ap>` 下启动 UI(自起 LVGL,出 `vela>` 提示符) |
| `set_pet <0..5>` | 设宠物**状态**(0 待机/1 听音/2 思考/3 执行/4 等待/5 错误)。IDLE→IDLE 不重置活动,**唤不醒待机** |
| `set_page <0>` | 回宠物首屏(粒子页已移除,其他值报错);也能唤醒待机 |
| `set_ptype [<0..3>]` | 宠物**形象**:0 白猫 / 1 绿青蛙 / 2 棕狗 / 3 粉猪。不带参数只回显;**每次烧录随机固定一只**,运行中不换(带参数=调试改并写盘) |
| `show_chat` | 切聊天屏 |
| `ask <文本>` | 真实 agent 一轮(气泡 + 状态机,无 TTS) |
| `voice_start` / `voice_stop` | 只驱动音频/ASR 通道,**不驱动 UI**;UI 语音支线用物理 S1 |
| `voice_test_asr <pcm>` / `voice_test_tts <text>` | 单独验 ASR/TTS 通道(不用按 S1) |
| `rpending <tool> [hint]` | **假审批**测试入口(用 S2/S3 决定,CLI 关不掉) |
| `rask <目标>` / `rstat` / `rreply` | 远控:发目标 / 看状态 / 取完整回复 |
| `ronce` / `rdeny` | 答**真实**远程审批;对 `rpending` 的假审批无效 |
| `notify_status` / `notify_start` / `notify_stop` | 查/开关后台通知轮询(Gerrit/Jira → **通知页**);真实轮询需先 `mcp_add` |
| `notify_inject <g\|j> <key> <text>` | **注入**一条通知:上屏为独立**通知页**,停 5s 自动退,任意键提前退,这一按被吞掉(不依赖 MCP) |
| `sync_time` | 打印 UTC/CST 时钟、联网状态、SNTP 是否已对齐(联网后自动对时) |
| `config_show` / `net_status` | 查凭据配置 / 联网状态(支线前置) |
| `restart` | 从 `vela>` 干净复位回 `nsh-ap>`(烧录前用;**`quit` 会卡死,别用**) |

自动行为:待机 60s 空闲;chat 空闲 8s 让位回宠物。

进入方式:`nsh-ap>` 下敲 `ai_agent`;联网用 `wapi connect`(`set_wifi` 在 AP 是空壳,
且 `wapi` **只能在 `nsh-ap>` 执行**,`vela>` 不转发)。

---

## 11. 每项验证要配什么(配置项 ↔ `cmd_scripts.sh`)

`cmd_scripts.sh` 已按**执行顺序**分成 §0–§8:烧录 → 串口 → 联网 → 进 agent →
LLM → 语音 → MCP(选填)→ 远控(选填)→ 退出清理。

| 验证点 | 需要的配置 | 该文件里的命令 | 位置 |
|---|---|---|---|
| `net_status` 联网 / 对时 | Wi-Fi SSID + 密码 | `wapi connect …`(`nsh-ap>` 下) | §2 行 33 |
| 域名解析 | 内网 DNS | `set_dns 10.237.25.8` | §4 行 47 |
| `ask`、LLM 工具调用、`cron_add` 建任务 | LLM key + 模型名 | `set_llm http://model.mify.ai.srv <vendor/model> sk-…` | §4 行 50 |
| PTT、文件式 TTS/ASR | 豆包 v3 key(ASR 与 TTS 共用一个) | `set_volc_key <api_key>` | §5 行 56 |
| 语音通道自检(不用按 S1) | 同上 | `voice_test_tts …` → `voice_test_asr /data/tts_test.pcm` | §5 行 59–60 |
| MCP 查 Gerrit、notify 真轮询 | 网关 token | `mcp_add gerrit …` | §6 行 69 |
| MCP 查 Jira | 用户名 + PAT | `mcp_add jira …` | §6 行 70 |
| 远控编码 + 审批 | 同一 MQTT broker + 账号 | `set_remote` / `set_remote_auth` | §7 行 105–106(前置见 §7 行 79–81 注释) |
| 远控一轮自检 | 同上 + PC 侧 bridge + coding agent | `./run-ui-flow.sh remote`(先看 `rstat` 的 `State`/`Backend`) | — |

- 远控**不要求 PC 与真机同网段**,只要求两者都能连到**同一个 MQTT broker**;
  broker 跑在 PC 上时(最常见)才等于「同一局域网」。设备端是明文 MQTT、
  不支持 TLS,主题前缀固定 `deskmate`,`device_id` 两端必须一致。
- 行号是 2026-10-26 的快照,会随文件增删漂移——**以 `../scripts/func-test.sh --lines`
  或 `grep -n` 为准**。
- `--keys` 的图例:`✓` 已填、`○` 待填(行首 `#`,或行内参数还是 `<host:port>` 这类尖括号原样)、`✗` 文件里没有。
- **整片烧录会清空 NVS;重启会丢 `/data`(tmpfs)**:两种都会让配置归零,
  `config_show` 里显示 `(not set)` 就是缺哪一项。

---

## 附录 A. 历史缺陷与修复记录

> 保留原因:里面是真实踩过的坑,复现特征比结论更有用。

1. **EXECUTING(3)/WAITING(4) 曾在真实流程里不触发** —— **已修**:
   NL 快路径(`handle_nl_fast_path`)在同步执行工具前置 3、结束后回 0;
   ReAct 路径在 `add_tool_result_messages()` 进 3、回 2。实测:
   - 快路径 `ask 现在几点了` → `state → 执行中` → `待机`;
   - LLM 工具路径 `ask 请调用时间工具告诉我现在时刻` → 思考中 → 执行中 →
     思考中 → 回复 → 待机。
   - WAITING 仍由审批页拥有:`lvgl_ui_channel_approval_pending()` 保证快路径
     结束时不会把待审批的 WAITING 冲成 IDLE。
2. **宠物类型没接 CLI** —— **已修,后统一为「整机固定一只」**:`set_ptype` 不带参数
   回显当前固定形象编号,带参数才改并写盘。
3. **`ask` 回复后约 20s 整机静默** —— **复验不再出现**:`ask 你好` 39 字节回复,
   `latency=1521ms elapsed=2s`,状态回待机、看门狗帧继续。
4. **待机首按门控** —— 已按预期工作:待机第一次按键只唤醒、不会答掉挂起审批。
5. **待机屏时间来源** —— 联网后由应用侧 SNTP 自动对时,屏上显示 UTC+8;
   取证日志 `[standby_view] standby shows …` 每分钟一条。详见
   [wifi-and-llm-config-flow.md](wifi-and-llm-config-flow.md)。
6. **AP UsageFault 崩溃 → CP panic(2026-10-20 定位并修复)**:
   - 现象:`ask 你好` 后只看到 `[agent] Processing message from cli:console`,随即 AP 死:
     `arm_usagefault: PANIC!!! Usage Fault: CFSR: 01000000 …(Unaligned access)` →
     `[CP] FATAL: Panic by remote core: ap` → `Rptun driver reset remote ap reason: 2`。
   - 回溯:`lv_loop_thread → lv_timer_handler → lv_timer_exec → lv_async_timer_cb → lv_async.c:103`,
     该行是 `ldmia.w r2, {r0,r1}`(r2 = `timer->user_data`)——ARMv8-M 上只有
     LDM/STM 这类多寄存器访问会因未对齐触发 UsageFault,即 **user_data 是垃圾值**。
   - 根因:AP 构建 `CONFIG_LV_USE_OS=0`,LVGL 自身无锁。`lv_timer_create()` 先把新 timer
     链进全局链表、之后才写 `user_data`,而 LVGL 循环线程正遍历同一链表;agent 线程
     `lv_async_call()` 撞上这个窗口。
   - 修复:新增 `lvgl_ui_lock()/lvgl_ui_unlock()`(owner+depth 模拟可重入,本构建未开
     `CONFIG_PTHREAD_MUTEX_TYPES`)与加锁版 `lvgl_ui_async_call()`;`lv_loop_thread()`
     在 `lv_timer_handler()` 外围持锁;所有跨线程 `lv_async_call()` 统一改用它;
     自起 LVGL 时把 loop 线程的创建挪到 init 末尾。
   - 复测(2026-10-20 真机,镜像 SHA-256 `a9006cae…9382aeb5`):连续 9 次无网 `ask`
     全部走完,串口 **0 次** `arm_usagefault`。
7. **~~未联网时 agent 循环线程不存在~~(已过时,2026-10-30 更正)**:这条当时写的是
   `agent_loop_start()` 只在网络连上时才被调用。现在(以及 2026-10-20 起)agent 是
   **开机即启动**,无网 `ai_agent` 会在屏上报原因。2026-10-30 真机实测(复位后无网):
   `[netmgr] Timed out waiting for network` → `[agent] Network timeout — net services
   not started.`,UI 正常渲染,宠物首屏可见。**按 E 节的降级路径读,不要按缺陷⑦读。**
8. **LVGL 布局重跑死循环(2026-10-21)**:`lv_obj_update_layout()` 里的 while 无界,
   某类「尺寸反馈回自身布局输入」的控件会每轮重新 invalidate、永不收敛,在持锁状态下
   冻住整个 LVGL 线程。补丁存于
   见选手仓 `docs/zhangyi101/patches/lvgl-layout-loop-guard.patch`,
   **32 轮上限 + 超限打日志**;打在 `apps/graphics/lvgl/lvgl/src/core/lv_obj_pos.c`。
   `apps/graphics/lvgl/lvgl` 是独立 git 仓库,重 sync / 重 clone 会覆盖,要重打;
   真机 `ask` 挂死时先确认这个补丁在不在(`grep -n 'passes >= 32' …/lv_obj_pos.c`)。

---

## 附录 B. 观察点 ≠ 判据

本文三类表格(`预期日志`、`预期界面`)是**观察点**:它们说明「正常跑起来时应该看到什么」,
用来定位问题。但**一行日志出现不等于功能成功**(典型反例:`[key] … -> hit` 只代表按键被
收下;`notify_inject` 只代表通知页画得出来;`rpending` 只是假审批)。

「什么算通过」只有一份,见 [main_loop.md](main_loop.md) 第 0 节 + `func-test.sh` 的依赖闸门。
