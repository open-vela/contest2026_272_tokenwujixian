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
| 10 | notify(通知页) | ✓ | 真 Gerrit 事件 `#11151328/#11151337/#11151338/#11151339/#11151343/#11151347/#11151349` → **唤醒待机时钟**(`standby clock hidden`)→ `[lvgl_notice] show` → 约 5s `hidden (auto-dismiss)` → `standby clock shown` 回到锁屏;全程无 `dropped (standby clock up)`(旧「待机丢弃」行为已按新需求改为「唤醒」)、无 assert/panic/flush rc!=0、心跳 stall=0。**按键提前关**在审批轮已验真路径(`[key] notice … dismissed (swallowed)` + `hidden (dismissed by key)`,关后无滚动/审批副作用) | 11-02 |

## 未通过

| 项 | 结果 | 依据(真机观察到的) | 时间 |
|---|---|---|---|
| `ask`(文字一轮) | ✗ | 只配 Wi-Fi + DNS + LLM + Volc + remote(**未配 MCP**),`ask 1+1等于几` 回显后 120s 内 `[agent] ask:`、`Sent to agent:`、`[pet_view] 思考中` **一条都没有**;再发 `help`/`config_show` 连回显都消失。`cmd_ask`(`nsh_commands.c:666`)顺序是 push 入站 → `lvgl_ui_channel_send_user()` → `printf`,前两步都不吐字 = CLI 线程卡在 `send_user` 的 LVGL 锁;同期有 `[lvgl_ui] handler 634ms/458ms` 慢回调。**与 MCP 无关** | 10-30 |

## 未验证的真实链路项(还没真走通或没走到)

| # | 项 | 现状 |
|---|---|---|
| 1 | `ask`(文字一轮) | 11-02 复验更进一步但仍挂:LLM 请求已发出(`[llm] OpenAI API with tools`)→ 回复进入派发(cli 通道 `Dispatching response → cli:console`)→ `[agent] reply N bytes`/正文一条不出、90s 超时;**随后 AP 整核静默**(看门狗 `[lvgl_ui] hb` 也不打)。**同一天 `ptt`(voice 通道)也在「LLM 请求发出→回复渲染前」挂**,证明挂点=LLM 回复路径、与通道无关。非 SMP(AP/CP 均 `CONFIG_SMP_NCPUS=1`),`# CONFIG_WATCHDOG is not set` 故卡死不自动复位 |
| 2 | ~~`notify` 的「按键关掉」腿~~ | 已验(见「已验证」#10):审批轮 S2 关掉通知页 #11151065,真路径 |
| 3 | `notify` 的 Jira 来源腿 | 仍阻塞:`jira initialize failed: HTTP -1`,0 工具;`notify_status` 里 jira `cursor=""`、从未上屏。Gerrit 15 工具正常,`Total remote tools: 15`(非 39) |
| 4 | `ptt` / 真麦克风语音 | 11-02 验到一半,**麦克风/PTT/ASR 腿真通**:按住 S1 → `PTT press` → 说话 → `PTT release` → `[volc_asr] recognized: 今天今天是几月几号`(真麦克风识别出字;前几次 `-61`=说话太轻 `no text recognized`,`media_recorder_open errno=11` 会回退 NuttX 采集非阻塞)。**但 LLM 回复腿挂**:`[llm] OpenAI API with tools` 发出 → `思考中` → 整机静默,与 `ask` 同一个挂点(LLM 回复渲染路径),非麦克风问题 |
| 5 | 审批后「推到 Gerrit」 | `func-test.sh approval` 里明确写「推送目标记下(本项不判)」;change 10334000 的新 patchset **没推过**,推送腿未验 |
| 6 | 换模型后的真实回合 | `keys` 只验到 `Model -> anthropic/claude-sonnet-4-6` 的切换回执;用它跑真回合时 `Billing: 0 tokens`、无工具调用(空回合),换模型后能不能真干活未验 |

## 阻塞(真依赖没满足)

| 项 | 卡在哪 |
|---|---|
| `notify` | Gerrit 腿已全通(上屏/唤醒待机/自动关/按键关);**Jira 腿阻塞**:`jira initialize failed: HTTP -1`(网关侧),0 工具 |
| `ptt` | 未跑:需要真麦克风说一句 + 豆包 ASR/TTS |

## 待完善(只记用户目测指出的)

| 项 | 现象 |
|---|---|
| 字体缺字 | 「聆听」的「聆」显示为方框;对话气泡里也有部分字是方框。CP 字体是 463 字符子集,缺码点就成 tofu |
| 状态环动画 | 宠物「思考 / 执行」时周围转圈的圆环**断断续续、不连贯**(掉帧/分段跳) |
| 语音失败反馈 | 未识别到语音、语音链路不可用时,**UI 回显和正常情况一样**,用户分不清是「没说话」还是「坏了」 |
| TTS 音质 | 播报音量小、音调偏高不好听;**断句不准**,一句完整的话常被拆成 3–4 段读 |
| 审批等待时进待机 | 有审批挂着(宠物状态「等待确认」)时,60s 无操作**仍会进待机**(`standby_timer_cb` 明确把 `PET_STATE_WAITING` 排除在「忙则顺延」之外,是有意设计);此后第一下按键只唤醒(**`[key] standby: keys=4 -> wake (swallowed)`** 实测),要按第二下才派发到审批 → 用户视角就是「审批页按了没反应」。**不是死机**:日志无 panic/assert,心跳连续 | 10-30 |
| mimo 界面不可见 | 链路里 mimo 是 `mimo acp --pure`(无界面),`deskmate-demo.sh` 顺带起的 `mimo attach` 输出被重定向进 `/tmp/deskmate-bridge.log` → **PC 上不弹窗**;要看代码改动得手动 `mimo attach http://127.0.0.1:4096 --session <id>` | 10-30 |
| 通知页文字与停留时长 | 通知弹窗里**文字被截短、显示不全**,长标题超出界面;而且**回到宠物屏太快**(实测 `[lvgl_notice] hidden (auto-dismiss)` 约 5s,`LVGL_UI_NOTICE_MS`),来不及看清 | 10-30 |
