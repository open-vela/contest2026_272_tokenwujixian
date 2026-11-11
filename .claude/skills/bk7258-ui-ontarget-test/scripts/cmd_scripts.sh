# ===========================================================================
# BK7258 真机 ai_agent 验证 —— 密钥与配置清单（按执行顺序）
#
# ★ 本文件是密钥/配置的唯一来源：
#     references/ui-flow-and-test.md   只引用这里的行号
#     scripts/func-test.sh --lines     只查行号，从不读取值
#   新增 key 请加在对应小节，不要再散落到文档、脚本或聊天记录里。
#
#   这是「粘贴用的命令清单」，不是能在宿主机直接执行的脚本：
#     标 [host] 的在宿主机跑；[nsh-ap>] / [vela>] 是串口提示符。
#   所有 <...> 都是待填占位符（密码/密钥/token/IP/账号），真值由使用者自己填，
#   只留在本机这份文件里，别写进 skill 文档或提交。
# ===========================================================================

# ── 0. 烧录 [host] ────────────────────────────────────────────────────────
# 整片烧录会清空 NVS：烧完必须从 §2 重新联网、§4/§5 重新配 key。
# <full_flow.sh> 路径与 <all-app.bin>、<contest-repo> 换成你自己的：
#   <skill>/scripts/full_flow.sh --image <all-app.bin 绝对路径> \
#     --worktree <contest repo 绝对路径> --flash --device /dev/ttyUSB0

# ── 1. 串口 [host] ────────────────────────────────────────────────────────
# 进入串口
# AP 控制台物理线速率是 1 Mbaud(CONFIG_BK7258_UART0_BAUD=1000000);
# 开 115200 会全静默,那是波特率错,不是板子挂死。
sudo minicom -o -D /dev/ttyUSB0 -b 1000000

# ── 2. 联网 [nsh-ap>] ─────────────────────────────────────────────────────
# 旧的分步写法（#ifconfig / #wapi psk / #wapi essid）默认注释；
# 末尾那条 wapi connect 是一步到位的等价写法，两种执行一种即可。
# 配置wifi
#ifconfig
#wapi psk wlan0 <WiFi密码>
#wapi essid wlan0 <SSID> 1
# 下面两行是原文件里的「命令 ← 预期结果」笔记，不能直接执行，故注释掉：
#wapi            ← 应打印 Usage:
#wapi scan       ← 应列出热点
# 一步到位写法：
wapi connect <SSID> <WiFi密码>
nslookup www.baidu.com
#ping -c 3 <内网IP>

# ── 3. 进 agent CLI [nsh-ap> → vela>] ─────────────────────────────────────
# 进入ai_agent后，可通过k1进行复位（或者发送复位指令）退出
wapi connect "<SSID>" <WiFi密码>
# 进 agent 前再确认一次联网（与 §2 的一步到位写法是同一条命令，可跳过）
ai_agent

# ── 4. 文字链路：LLM [vela>] ──────────────────────────────────────────────
# 先 set_dns，再自检联网/对时，最后配模型。
# 模型名必须带供应商前缀（deepseek/… 或 xiaomi/…）——网关靠它加
# X-Model-Provider-Id 头；省略 key 参数则沿用上次配的 key。
set_dns 10.237.25.8
net_status
sync_time
set_llm http://model.mify.ai.srv deepseek/deepseek-flash <LLM-API-KEY>
ask 你好

# ── 5. 语音链路：PTT / 文件式 TTS+ASR [vela>] ─────────────────────────────
# 只需一个豆包 v3 key（ASR 与 TTS 共用）。资源 id 是编译期常量：
# ASR=volc.seedasr.sauc.duration，TTS=seed-tts-2.0，账号两者都要开通。
set_volc_key <VOLC-API-KEY>
config_show                                   # Volc API Key 应显示前 4 位，不是 (not set)
voice_test_beep 1500 300                      # 先听喇叭
voice_test_tts 你好，这是语音链路验证 /data/tts_test.pcm    # 应打 TTS OK: N bytes
voice_test_asr /data/tts_test.pcm              # 回显「你好这是语音链路验证」
# 真正的 PTT 闭环：按住板上 S1 说 2~3 秒再松手。
# voice_start/voice_stop 只驱动音频通道，不驱动 UI（屏上不会出聆听态/气泡）。

# ── 6. MCP（选填）[vela>] —— 查 Gerrit/Jira、notify 真实轮询 ──────────────
# 设备端没有 mcp_query：查询是让 LLM 调工具（gerrit.get_change_details /
# jira.jira_get_issue 这类点分名）。填好真值、去掉行首 #，再跑 mcp_discover，
# 工具才会注册进 LLM 的工具表。
# Gerrit：URL 必须含 onedev.pt.miui.com，token 原样进 x-user-token 头
mcp_add gerrit https://onedev.pt.miui.com/mcp-gw/gerrit_mcp <GERRIT-MCP-TOKEN>
mcp_add jira https://acc.pt.miui.com/jira-mcp/mcp https://jira-phone.mioffice.cn <用户名> <JIRA-PAT>
#mcp_discover
#ask 用 jira_get_issue 工具查询 Jira issue <ISSUE-ID> 的内容,做个简短总结
#ask 用 gerrit.get_change_details 工具查询 change_id 为 <CHANGE-ID> 的提交,总结它改了什么 <gerrit-url>#mcp_discover
#mcp_status
#mcp_tools
#notify_status

# ── 7. 远控（选填）[vela>] —— IDE 编码 + 提交 + 审批 ──────────────────────
# 前置：PC 端 deskmate/mimo bridge 已起来（DESKMATE_PERMISSION_TIMEOUT_MS=300）。
#   两端不必同一 Wi-Fi，只需都能连到同一个 MQTT broker（broker 跑在 PC 上时才等于同网段）；
#   设备端明文 MQTT 1883 不支持 TLS，主题前缀固定 deskmate，device_id 两端必须一致。

# # broker 侧 /tmp/mosq_local.conf
# listener 1883 0.0.0.0
# allow_anonymous false
# password_file /tmp/deskmate.passwd
# # mosquitto_passwd -c /tmp/deskmate.passwd deskmate

# # PC 侧 bridge 也要带同一组凭证
# export DESKMATE_MQTT_USERNAME=deskmate
# export DESKMATE_MQTT_PASSWORD=<该账号的密码>

# cd <contest repo>/tools/acp-bridge
# export DESKMATE_MIMO_BIN=$HOME/.mimocode/bin/mimo     # 关键：mimo 不在 PATH
# export DESKMATE_PERMISSION_TIMEOUT_MS=300             # 单位是秒；默认 30s 会把通道 wedge
# node bin/deskmate-bridge.mjs --backend mimo --transport mqtt \
#   --broker mqtt://127.0.0.1:1883 --topic-prefix deskmate --device-id bk-1

# net_status                      # 必须 connected
# set_remote <MQTT-broker:1883> bk-1
# rstat                           # State: Offline → Idle（bridge 起来之后）
# rask 用一句话介绍你自己          # 触发一轮，板上会进 THINKING / EXECUTING


set_remote <MQTT-broker:1883> bk-1
set_remote_auth <用户名> <MQTT密码>
#rstat
#rask 读取 /tmp/apps_hello_wt/AGENT_TASK.txt，按任务完成修订、验证并提交
# 审批：ronce 通过 / rdeny 拒绝；rpending 造的是假审批，只能用板上按键答。
# rstat 三条判据：State≠Offline（broker/bridge 通）、Backend=up（PC 上 coding
#   agent 在跑）、出现 “Approval needed for:” 才是真审批。板上只批，不执行
#   读代码/改代码/编译/commit/push——那些都在 PC 侧完成（见 §7 上方注释）。

# ── 8. 退出与清理 [host] ──────────────────────────────────────────────────
# 退出 agent 用 restart（quit 会卡死）；复位按板上 K1 或 reboot bootloader。
sudo fuser -v /dev/ttyUSB0
pgrep -af 'minicom|picocom|screen|cu |bk_loader|python3'
sudo pkill -x minicom
