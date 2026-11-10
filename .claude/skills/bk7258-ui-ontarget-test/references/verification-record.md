# 验证记录(真机 BK7258)

> 「什么算过」的口径见 [testLoop/main_loop.md](testLoop/main_loop.md) §0:只有真实链路产出可观测效果才算过,
> 注入 / 模拟 / 只看一行日志的算旁路,不计通过。真依赖没满足的记「阻塞」,不算失败也不算通过。
> 本文件只记跑了什么、结果如何,一两句话说清。

## 已验证

| # | 项 | 结果 | 依据(真机观察到的) | 时间 |
|---|---|---|---|---|
| 1 | net(联网) | ✓ | `wlan0 ip 192.168.112.233`,SSID `<SSID>`,4WAY→COMPLETED,`me dhcp done vif:0`;10-28 的扫描回归(psram_zalloc 失败)**未复现** | 10-30 |
| 2 | start(启动) | ✓ | `ai_agent` 后回 `vela>`,宠物首屏上屏,无 `flush rc!=0` | 10-30 |
| 3 | states(状态切换) | ✓ | `pet type: 1`(青蛙),切页面来回后仍是同一只,不会每次换形象 | 10-30 |
| 4 | standby(待机) | ✓ | 63s 无操作打进 `standby shows 2026-10-30 14:41 周五`,时间与 PC 一致(SNTP 生效) | 10-30 |
| 5 | wake(首按唤醒) | ✓ | `[key] standby: keys=2 gesture=0 -> wake (swallowed)` + `standby clock hidden`;这一下没派发到聊天/滚动 | 10-30 |
| 6 | config(配置) | ✓ | `Network: connected / 192.168.112.233`、`Volc API Key 4f38****`、`Net Proxy: usrsock (CPU: ap)`;MCP 下发后 `mcp_status` = 2 服务器 39 工具 | 10-30 |
| 7 | remote(链路 + 一轮到底) | ✓ | `rstat` = State Idle / Backend up;`rask pwd` → PC 侧 mimo 真执行 `bash pwd` → 板 `[Agent]: /home/…/tools/acp-bridge` + 气泡 + TTS;MQTT 上完整 `prompt_submit → prompt_ack → tool_result → agent_output → turn completed` | 10-30 |
| 8 | keys(按键 5/5) | ✓ | S2 单击 → `[key] idle keys=2 hit` + `loading chat screen`;S3 单击 → `keys=4 hit`;S2 长按 → `Model -> anthropic/claude-sonnet-4-6`;S3 长按 → `New session requested.` + PC 侧 `New session started`;S2+S3 → `keys=6 gesture=3` + `[key] cancel`。`func-test.sh keys` exit=0。**备注:聊天历史是用远控一轮 `rask pwd` 造的,不是真 `ask`**——`ask` 本身卡死(见「未通过」),`main_loop.md` 里 keys 的「一次真 ask」这一前置本轮没满足,滚动/换模型/新会话本身是真机真实效果。**另:S2+S3 组合键只证明「被识别并派发到 global 键表」——`action_cancel()` 目前实现为空,只 `printf("[key] cancel")`,不停 TTS、不取消回合、不返回上一页**(`board/bk7258-devkit/src/bk7258_buttons_input.c:179` + `:305`) | 10-30 17:20 |
| 9 | approval(审批 S2 通过 / S3 拒绝) | ✓ | 两条腿都是板键真实生效:**通过** S2 → `Approved once.` → mimo 真改盘上文件 `hello_main.c` **1650→1681 字节**,末尾 `/* deskmate approval probe */`;**拒绝** S3 → `Denied.` → 该文件**保持 1681 字节、无 `denial probe`**,mimo 自己回「编辑被拒绝」。两腿之后都收到 `Permission decision accepted by ACP`,链路没断。`func-test.sh approval` exit=0 | 10-30 20:07 |
| 11 | `ask`(文字一轮) | ✓ | 11-09 复验**完整通过**:`[agent] ask: 1+1等于几` → `[llm] OpenAI API with tools (deepseek/deepseek-flash)` → `[agent] Dispatching response → cli:console` → `[Agent]: 让我查一下...` → 第二轮 `[Agent]: 2`(答对);用户泡 `[lvgl_ui] async: add msg (12 bytes)` + `loading chat screen` 上屏;TTS 真播两段(`speak done: total 4061ms/3383ms`,`BK7258 PA: enabled/disabled`);心跳连续到 `frames 30382`,`assert/panic/flush rc!=0` = 0。**同机隔离对照**:单跑 `voice_test_speak` 也完成,证明音频栈不挂。遗留:TTS 首包延迟抖动极大(本轮 3.2s,同次隔离测试 58.7s),10-30/11-02 的"整核静默"未再复现,根因仍未定死 | 11-09 |
| 12 | notify 的 Jira 来源腿 | ✓ | vivo 热点下 jira 真发现 **24 工具**(与 gerrit 15 合计 39),Jira 真事件上屏:`FEEDBACK-158662`/`LCFEAT-476737`/`CODECHECK-183541` 等;此前 `HTTP -1` 是网络侧(guest 网络不通内网),非代码 | 11-09 |
| 13 | 语音/远控指令 → PC 侧 mimo 自动弹窗展示 | ✓ | 板端发 `rask 只回答两个字:窗口` → 桥日志 `[deskmate-bridge] opened MiMo attach window for session ses_…`(窗口由**设备 prompt 触发**,非人工开)→ 约 7s 后该轮正常完成(`[remote_ctrl] reply 6 bytes` + 吐司「任务完成: 窗口」);自动弹出的 MiMo 窗口截图里能看到同一轮问答(「只回答两个字:窗口」→「窗口」,Build · deepseek/deepseek-flash · 1.4s)以及上一轮内容。**踩坑**:attach 客户端与桥共用同一 ACP 会话,若在 bridge 的 turn 执行中接入会让该轮永远拿不到回复(实测挂死);须先连上再提交,故首次 prompt 前预热点 `DESKMATE_ATTACH_WARMUP_MS`(默认 7000),后续轮次零延迟 | 11-09 |
| 14 | 页面滞留与切换(60s 统一) | ✓ | 真机:`ask` 造出气泡后聊天屏**不再 8s 让位**(旧 `PET_IDLE_YIELD_MS` 已删),`set_pet 0` 置 IDLE 后 **60.7s** 才 `[lvgl_ui] idle 60000ms → standby clock`;宠物页同样 **60.5s** 进待机。宠物页点 S2(历史 2 条)→ `[lvgl_ui] scroll: pet → chat (2 msgs)` + `loading chat screen`;重启清空历史后同一点按 → `scroll: empty transcript, staying on pet` 且不切页。待机唤醒第一下仍被吞(`[key] standby: keys=2 → wake (swallowed)`)。**注意**:在聊天屏上 S2/S3 是上下翻页,不打 scroll 换页日志,别误判成"没生效" | 11-09 |
| 10 | notify(通知页) | ✓ | 真 Gerrit 事件 `#11151328/#11151337/#11151338/#11151339/#11151343/#11151347/#11151349` → **唤醒待机时钟**(`standby clock hidden`)→ `[lvgl_notice] show` → 约 5s `hidden (auto-dismiss)` → `standby clock shown` 回到锁屏;全程无 `dropped (standby clock up)`(旧「待机丢弃」行为已按新需求改为「唤醒」)、无 assert/panic/flush rc!=0、心跳 stall=0。**按键提前关**在审批轮已验真路径(`[key] notice … dismissed (swallowed)` + `hidden (dismissed by key)`,关后无滚动/审批副作用) | 11-02 |

## 未通过

| 项 | 结果 | 依据(真机观察到的) | 时间 |
|---|---|---|---|
| `ask`(文字一轮) | ✗ → ✓ | **11-09 已转为通过**(见「已验证」#11);下面保留 10-30 首次失败的现场作为历史记录。10-30 现场:只配 Wi-Fi + DNS + LLM + Volc + remote(**未配 MCP**),`ask 1+1等于几` 回显后 120s 内 `[agent] ask:`、`Sent to agent:`、`[pet_view] 思考中` **一条都没有**;再发 `help`/`config_show` 连回显都消失。`cmd_ask`(`nsh_commands.c:666`)顺序是 push 入站 → `lvgl_ui_channel_send_user()` → `printf`,前两步都不吐字 = CLI 线程卡在 `send_user` 的 LVGL 锁;同期有 `[lvgl_ui] handler 634ms/458ms` 慢回调。**与 MCP 无关** | 10-30 |

## 未验证的真实链路项(还没真走通或没走到)

| # | 项 | 现状 |
|---|---|---|
| 1 | ~~`ask`(文字一轮)~~ | 11-09 已验(见「已验证」#11) |
| 3 | ~~`notify` 的 Jira 来源腿~~ | 11-09 已验(见「已验证」#12) |
| 4 | ~~`ptt` / 真麦克风语音~~ | **11-09 已验通过**:物理 S1 按住说话 → 真麦克风 `peak=27923` → `[volc_asr] stream: recognized: …` → 远控回合(PC 侧 `glob`/`bash`/`read` 真执行,列出 `app/hello_app/hello_app_main.c`)→ 回显镜像 + TTS 播报 |
| 5 | 审批后「推到 Gerrit」 | **11-09 语音链路已验,审批腿另算**。语音指令「给 hello 点 c 文件加一行注释然后推送」→ `[volc_asr] recognized` → 桥 cwd = `apps` 仓库(此前误为 contest 仓,已改)→ mimo 真 `edit` `examples/hello/hello_main.c`(+1 行注释)→ `git commit`(自动带 Change-Id `I1ebfb25…`)→ `git push vela` → **Gerrit 新 change 11220036 PS1 = `4e02ca47e`**(`vela/apps` / `trunk-25rc05` / NEW,`ssh gerrit query` + `ls-remote refs/changes/36/11220036/1` 双向核实)。**两处偏差**:(a) 这是**新建 change**,不是 in-place 的 PS14——commit 是在 PS13 之上新提交而非 `--amend` 到 `c47425c58`,故 10334000 仍停在 PS13;(b) 该轮 mimo **没发 `session/request_permission`**,板端审批页没出现,「审批→推送」不是同一条连续链路;审批本身(通过/拒绝两条腿)见「已验证」#9(10-30 真改盘上文件) | 11-09 |
| 6 | 换模型后的真实回合 | **已证为真失败**(非网络):板端 `rmodel` 切模型后,该轮桥侧 `Context: 0/1048576`、既无文本也无工具调用,板端空回。11-09 复现稳定 |

## 阻塞(真依赖没满足)

| 项 | 卡在哪 |
|---|---|
| `notify` | ~~Jira 腿~~ 11-09 已通(见「已验证」#12);Gerrit + Jira 两条来源腿现均通 |
| `ptt` | **11-09 已解除阻塞**:两次人工按住 S1 的端到端复跑均通过(第一次 `peak=27350` → recognized「今天是几月几号」→ 远控回合 → TTS 8105ms;第二次 `peak=28019` → recognized「给 hello 点 c 文件加一行注释然后推送…」→ 远控真改文件并推 Gerrit,见「未验证」#5)。**待完善项**:静音/没说话时 `[voice] chunk*: peak≈40~50` → `stream: no text recognized` → `voice_channel_stop_with_text rc=-61` → 宠物进「出错了」,UI 回显与正常一轮**看不出区别**(对应「待完善」表的语音失败反馈条) |
| 远控桥 | ~~网络拓扑~~ **11-09 已打通**:mosquitto 改绑 `0.0.0.0` + iptables 只放行板子出口 IP(手机热点会 NAT,板流量到 PC 记作 `10.220.33.136`,不是板自身 IP);`rstat`/`rask` 真跑通。遗留:临时改绑与防火墙规则未持久化 |
| 一切联网链路(`ask`/`rask`/TTS 云端) | **11-09 烧录后复现扫描回归**:`wapi connect` 后 `scanu_confirm … upload_cnt=0` + 每 3.7s `Failed to allocate memory`,只能 `wapi disconnect` 停下;`wget`/`wlan` 无 IP。与「已验证 #1(10-30 未复现)」不矛盾——该回归是间歇性的,根因仍是 CP 侧 PSRAM 堆初始化(10-28 引入)。**不是本次 UI 改动引入** |

## 待完善(只记用户目测指出的)

| 项 | 现象 |
|---|---|
| 字体缺字 | 「聆听」的「聆」显示为方框;对话气泡里也有部分字是方框。CP 字体是 463 字符子集,缺码点就成 tofu |
| 状态环动画 | 宠物「思考 / 执行」时周围转圈的圆环**断断续续、不连贯**(掉帧/分段跳) |
| 语音失败反馈 | 未识别到语音、语音链路不可用时,**UI 回显和正常情况一样**,用户分不清是「没说话」还是「坏了」 |
| TTS 音质 | 播报音量小、音调偏高不好听;**断句不准**,一句完整的话常被拆成 3–4 段读 |
| 审批等待时进待机 | 有审批挂着(宠物状态「等待确认」)时,60s 无操作**仍会进待机**(`standby_timer_cb` 明确把 `PET_STATE_WAITING` 排除在「忙则顺延」之外,是有意设计);此后第一下按键只唤醒(**`[key] standby: keys=4 -> wake (swallowed)`** 实测),要按第二下才派发到审批 → 用户视角就是「审批页按了没反应」。**不是死机**:日志无 panic/assert,心跳连续 | 10-30 |
| ~~mimo 界面不可见~~ | **11-09 已解决**:`--mimo-attach-on-prompt` 在设备首个 `prompt_submit` 到达时自动弹 GNOME Terminal 并 `mimo attach` 到同一会话,窗口实时显示设备发起的问答与改动(见「已验证」#13)。注意:窗口必须在 turn 开始前连上,故首次 prompt 有 ~7s 预热点 | 10-30 → 11-09 |
| 通知页文字与停留时长 | 通知弹窗里**文字被截短、显示不全**,长标题超出界面;而且**回到宠物屏太快**(实测 `[lvgl_notice] hidden (auto-dismiss)` 约 5s,`LVGL_UI_NOTICE_MS`),来不及看清 | 10-30 |
