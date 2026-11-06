#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# BK7258 功能验收（成功路径）:先把环境配成「能成功」,再逐项验证功能真的成功
#
#   ./func-test.sh                  # 全跑: net → start → config → text → voice
#                                   #       → keys → approval → notify
#   ./func-test.sh text keys        # 只跑指定项(按给出的顺序跑)
#   ./func-test.sh --list           # 看有哪些项
#   ./func-test.sh --lines          # 只看每项配置落在 cmd_scripts.sh 第几行
#
# 与 run-ui-flow.sh 的分工:
#   run-ui-flow.sh  验「流程顺序 + 每一步看什么」(照 ui-flow-and-test.md §7/§9)
#   func-test.sh    验「每个功能点是否真的成功」——不按流程走,只问:
#     联网真的拿到 IP 了吗? LLM 真的回了(不是 LLM unavailable)吗?
#     说一句话真的识别出来了吗? TTS 真的出声了吗?
#     S2/S3 真的换了界面(聊天屏真的被拉起)、真的换了模型、真的开了新会话吗?
#     S2/S3 真的能通过/拒绝审批吗? 通知页真的弹出、真的自己收起/被按键关掉吗?
#
# ★ 密钥/配置唯一来源 cmd_scripts.sh:按命令名查行号,整行原样下发;
#   本脚本从不打印这些行的内容,串口回显统一打码后才落盘。
# ★ 不发 reboot、不发 quit、不烧录。唯一一次复位:板子停在 vela> 又需要
#   联网时发 restart(AP 复位回 nsh-ap>,不动烧录)。
# ---------------------------------------------------------------------------
# ★★★ 正确功能验证(real path)——本脚本的判据口径 ★★★
#
# 「通过」只能由真实链路产出。凡是用注入/模拟把界面点亮、再拿一行日志判过的,
# 都算旁路(bypass),一律不算通过。每一项的 真依赖 / 可观测判据 / 不算数的旁路:
#
#   net      真依赖: nsh-ap> + 真路由器
#            判据:   wapi connect 后真的拿到 192.168.x.x
#            旁路:   看到 "connect" 字样就过,不看有没有 IP
#
#   start    真依赖: 无(纯本地)
#            判据:   宠物首屏上屏(flush rc=0)+ 提示符回到 vela>
#
#   config   真依赖: net
#            判据:   DNS 真解析 + LLM/语音凭据真的下发了
#
#   text     真依赖: net + LLM key
#            判据:   真 LLM 回复文本落到气泡(不是 LLM unavailable)
#            旁路:   关键词快路径(不经 LLM)也算"文字走通"
#
#   voice    真依赖: net + 豆包 key + 麦克风
#            判据:   麦克风真采到声音 → ASR 出文字 → LLM 回复 → 喇叭真出声
#            旁路:   文件式/回灌音频替代真说话
#
#   keys     真依赖: net + 一次真 ask(有聊天记录) + bridge 下发的模型表
#            判据:   上/下滚气泡真的动;长按换模型打印 Model -> <新模型> 且界面
#                    出现「已切换到…」;长按新会话打印 New session requested.;
#                    组合键真的取消
#            旁路:   只判 [key] ... -> hit 就过(按了屏幕没反应也算过)← 已踩过
#
#   approval 真依赖: net + PC 侧 bridge + coding agent(审批必须来自真 agent)
#            判据:   真 agent 发起工具调用 → 板上审批页 → 板键通过/拒绝 →
#                    回合继续/终止
#            旁路:   rpending 本地造假审批;只看 Approved (debug). 一行
#            ★ 本轮指定的「真改动目标」(用户 2026-10-30 定):
#              改  ~/project26/vela-miwear-bes-o62-25rc05-lc3/apps/examples/hello/hello_main.c
#                  (就是 vela/apps 这个项目里的文件;该目录是独立 git 仓库,
#                   push 远端 = ssh://<用户名>@gerrit.pt.mioffice.cn:29418/vela/apps)
#              推  https://gerrit.pt.mioffice.cn/c/vela/apps/+/10334000
#                  = 给这个**已有 change 更新 patchset**,不新推 change
#              (提醒:更新 patchset 不产生通知事件——notify 按 change 号去重)
#
#   notify   真依赖: net + Gerrit/Jira MCP 真事件
#            判据:   真事件到达 notify_service → 通知页上屏 → 5s 自退 / 按键退出
#            旁路:   notify_inject 注入(只能证明"页面画得出来")
#
#   remote   真依赖: net + PC broker + bridge + coding agent
#            判据:   rstat State≠Offline 且 Backend=up → rask → 板上批 →
#                    回合结束 → rreply 拿到完整回复
#
# 结论(必须记住): text / voice / keys / approval / notify / remote 的正确路径
# 全部经过 Wi-Fi。Wi-Fi 不通时这些项只能是「阻塞(BLOCKED)」,不得被弱判据判成
# 通过。想复现注入版(只验界面)请显式加环境变量 —— 结果会标注「旁路」:
#
#     ALLOW_BYPASS=1 ./func-test.sh notify approval keys
#
# ★ 依赖闸门用 `rstat` 读板子自报的远控状态(它与 LLM/ASR/通知走同一套 Wi-Fi
#   和 TCP 栈),不靠猜。Offline/unknown 一律按「网没通」处理。
# ---------------------------------------------------------------------------
set -uo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "$0")" && pwd)
cd "$SCRIPT_DIR" || exit 1

DEV=${DEV:-/dev/ttyUSB0}
BAUD=${BAUD:-115200}
LOG=${LOG:-/tmp/func-test-$(date +%Y%m%d-%H%M%S).log}

# 配置来源。只用它定位行号,不读值、不打印值。
KEYS_DEFAULT="$SCRIPT_DIR/cmd_scripts.sh"
KEYS_FILE=${KEYS_FILE:-$KEYS_DEFAULT}

KEY_WAIT=${KEY_WAIT:-300}    # 等人工按键的上限(按了立刻往下走)
REPLY_WAIT=${REPLY_WAIT:-90} # 等 LLM 回复
ASR_WAIT=${ASR_WAIT:-45}     # 等 ASR 收尾
NOTIFY_WAIT=${NOTIFY_WAIT:-300}   # 等一条真的 Gerrit/Jira 事件
APPROVAL_WAIT=${APPROVAL_WAIT:-300} # 等一次真的审批请求
# 注入/模拟版只能显式开:ALLOW_BYPASS=1。开了也只在结果里标「旁路」,不算真通过。
ALLOW_BYPASS=${ALLOW_BYPASS:-0}

# --- 本轮审批用的「真代码改动」目标(2026-10-30 用户指定) --------------------
# 审批的真因只能是 PC 侧 agent 真的要改代码。这里把要改的文件和要推的地方固定下来,
# 免得每次临时想「让它改什么」,也免得改成别的文件后说不清验的是什么。
#
# 文件选的是别的项目里的 hello_main.c,不是本仓的:它属于 vela/apps 这个 Gerrit
# 项目,和下面的推送目标(vela/apps +10334000)对得上。
PROJ26=${PROJ26:-$(cd -- "$SCRIPT_DIR/../../.." 2>/dev/null && pwd)}
APPR_FILE=${APPR_FILE:-$PROJ26/vela-miwear-bes-o62-25rc05-lc3/apps/examples/hello/hello_main.c}
# 改完要落到的位置:这个**已有 change 的新 patchset**(不新推 change)
APPR_PUSH_URL=${APPR_PUSH_URL:-https://gerrit.pt.mioffice.cn/c/vela/apps/+/10334000}
APPR_CHANGE=${APPR_CHANGE:-10334000}
# 推送用的工作副本:vela/apps 在该项目下的独立 git 仓库(push 远端是 gerrit ssh)。
# 只有它存在、并且把该 change 的提交 fetch 下来,才谈得上「更新 patchset」。
APPR_GERRIT_DIR=${APPR_GERRIT_DIR:-$PROJ26/vela-miwear-bes-o62-25rc05-lc3/apps}

# --- 输出:去心跳帧 + 打码 + 同时落盘 ---------------------------------------
redact() {
  # 控制台丢字会把一行拆开,所以除了 sk- 前缀,任何 24 字符以上的
  # token 样字符串也整体打码,避免半截密钥漏出去。
  sed -E 's/sk-[A-Za-z0-9_-]{4,}/<REDACTED>/g; s/[A-Za-z0-9_-]{24,}/<REDACTED>/g'
}
emit() {
  local line
  line=$(printf '%s' "$1" | redact)
  printf '%s\n' "$line"
  printf '%s\n' "$line" >>"$LOG"
}
say() { printf '%s\n' "$*"; printf '%s\n' "$*" >>"$LOG"; }
hr() { say "────────────────────────────────────────────────────────────"; }

remind() {  # remind <标题> <要做的动作> —— 响铃 + 大字框(只在终端/日志里)
  printf '\a\a\a'
  printf '\n'
  printf '╔══════════════════════════════════════════════════════════════╗\n'
  printf '║  ★★★ %s\n' "$1"
  printf '║  %s\n' "$2"
  printf '╚══════════════════════════════════════════════════════════════╝\n\n'
  printf '★★★ %s — %s\n' "$1" "$2" >>"$LOG"
}

# --- 配置行:只认行号 -------------------------------------------------------
keys_disp() {
  if [ "$KEYS_FILE" = "$KEYS_DEFAULT" ]; then
    printf 'cmd_scripts.sh'
  else
    printf '%s' "$KEYS_FILE"
  fi
}

cfg_lines() {  # cfg_lines <ERE(不含行首空白)> —— 打印所有命中的行号
  [ -f "$KEYS_FILE" ] || return 0
  grep -n -E "^[[:space:]]*$1" "$KEYS_FILE" 2>/dev/null | cut -d: -f1
}

cfg_first_line() {  # cfg_first_line <ERE> —— 命中就打印第一个行号并返回 0
  local n
  n=$(cfg_lines "$1" | head -n1)
  [ -n "$n" ] || return 1
  printf '%s' "$n"
}

cfg_is_template() {  # cfg_is_template <行号> —— 行内还留着 <host:port> 占位?
  sed -n "${1}p" "$KEYS_FILE" 2>/dev/null | grep -qE '<[A-Za-z_][^>]*>'
}

send_cfg() {  # send_cfg <ERE> <名称> —— 整行原样下发;只打印行号
  local re=$1 name=$2 n
  if ! n=$(cfg_first_line "$re"); then
    say "  ✗ $name: $(keys_disp) 里没有对应行"
    return 1
  fi
  if cfg_is_template "$n"; then
    say "  ✗ $name: $(keys_disp):$n 参数还是 <…> 占位,先填真值"
    return 1
  fi
  say ">>> [$name] 见 $(keys_disp):$n(整行原样下发,内容不显示)"
  printf '%s\r\n' "$(sed -n "${n}p" "$KEYS_FILE")" >&3
  return 0
}

show_lines() {
  printf '配置项来源: %s\n' "$KEYS_FILE"
  [ -f "$KEYS_FILE" ] || { printf '  ✗ 文件不存在\n'; return 1; }
  local item re
  for item in \
    'Wi-Fi 联网(nsh-ap>)|wapi[[:space:]]+connect' \
    'DNS|set_dns' \
    'LLM key + 模型|set_llm' \
    '豆包语音 key|set_volc_key'; do
    re=${item#*|}
    n=$(cfg_first_line "$re") || { printf '  ✗ %-24s 未收录\n' "${item%%|*}"; continue; }
    if cfg_is_template "$n"; then
      printf '  ○ %-24s 行 %s(还是 <…> 占位)\n' "${item%%|*}" "$n"
    else
      printf '  ✓ %-24s 行 %s\n' "${item%%|*}" "$n"
    fi
  done
  printf '说明:只报行号,不读取也不打印这些行里的值。\n'
}

# --- 串口 -------------------------------------------------------------------
open_serial() {
  [ -c "$DEV" ] || { say "✗ 串口不存在: $DEV"; exit 2; }
  if command -v fuser >/dev/null 2>&1 && fuser "$DEV" >/dev/null 2>&1; then
    say "⚠ $DEV 已被其他进程打开(minicom?)。请先退出,否则两边抢字节。"
    exit 2
  fi
  stty -F "$DEV" "$BAUD" raw -echo -echoe -echok -echoctl -echoke -crtscts clocal 2>/dev/null \
    || { say "✗ stty 配置失败(权限? 需要在 dialout 组)"; exit 2; }
  exec 3<>"$DEV" || { say "✗ 打不开 $DEV"; exit 2; }
  say "串口 $DEV @$BAUD 已打开;日志: $LOG"
}

close_serial() { exec 3>&- 2>/dev/null; exec 3<&- 2>/dev/null; }
trap close_serial EXIT

send() {
  say ">>> $1"
  printf '%s\r\n' "$1" >&3
}

RLINE=""
read_line() {
  local rc=0
  RLINE=""
  IFS= read -r -t 0.6 -u 3 RLINE || rc=$?
  RLINE=${RLINE%$'\r'}
  case "$RLINE" in
    *"hb frames"*|*"wd frames"*) RLINE=""; return 1 ;;
  esac
  [ -n "$RLINE" ]
}

expect() {  # expect [--not RE] <RE> <秒> <描述>
  local notpat=""
  if [ "$1" = "--not" ]; then notpat=$2; shift 2; fi
  local pat=$1 secs=${2:-60} desc=${3:-$1}
  local deadline=$(( SECONDS + secs ))
  while (( SECONDS < deadline )); do
    read_line || continue
    emit "    $RLINE"
    if [ -n "$notpat" ] && [[ $RLINE =~ $notpat ]]; then
      say "  ✗ 命中失败判据: $RLINE"
      return 1
    fi
    if [[ $RLINE =~ $pat ]]; then
      say "  ✓ $desc"
      return 0
    fi
  done
  say "  ✗ 超时 ${secs}s 未命中: $desc"
  return 1
}

expect_awake() {  # expect_awake <RE> <秒> <描述> [保活间隔秒=40]
  # 待机(60s 无操作)一上来,通知会被**直接丢掉**:
  #   [lvgl_notice] dropped (standby clock up): Gerrit
  # 所以这里边等边做一次真页面切换保活 —— 只是让屏醒着,不注入事件、不改通知链路,
  # 通知本身仍然是 notify_service 真轮询真 Gerrit/Jira 出来的。
  # 页面切换不会关掉通知页(notice 只有 5s 超时 / 审批页顶掉 / 按键 三条关闭路径)。
  local pat=$1 secs=${2:-60} desc=${3:-$1} iv=${4:-40}
  local deadline=$(( SECONDS + secs )) next=$(( SECONDS + iv ))
  while (( SECONDS < deadline )); do
    if (( SECONDS >= next )); then
      say "  …保活:set_page 1 → 0(防待机把通知丢掉)"
      printf 'set_page 1\r\n' >&3
      printf 'set_page 0\r\n' >&3
      next=$(( SECONDS + iv ))
      continue
    fi
    read_line || continue
    emit "    $RLINE"
    if [[ $RLINE =~ $pat ]]; then
      say "  ✓ $desc"
      return 0
    fi
  done
  say "  ✗ 超时 ${secs}s 未命中: $desc"
  return 1
}

drain_for() {
  local deadline=$(( SECONDS + $1 ))
  while (( SECONDS < deadline )); do
    read_line && emit "    $RLINE"
  done
  return 0
}

probe_prompt() {  # 打印 nsh|vela|? ;先发 \r\n,板子只在收到输入时才有输出
  printf '\r\n' >&3
  # 20s:notify 轮询 TLS 阻塞命令循环时,回车也要排队好几秒
  local deadline=$(( SECONDS + 20 )) seen=""
  while (( SECONDS < deadline )); do
    read_line || continue
    case "$RLINE" in
      *"vela>"*)   seen=vela; break ;;
      *"nsh-ap>"*) seen=nsh;  break ;;
    esac
  done
  printf '%s' "${seen:-?}"
}

expect_key() {  # expect_key <RE> <秒> <描述> <rearm 命令|-> [失败判据 RE]
  local pat=$1 secs=${2:-$KEY_WAIT} desc=$3 rearm=${4:--} failpat=${5:-}
  local deadline=$(( SECONDS + secs )) next=$(( SECONDS + 25 ))
  while (( SECONDS < deadline )); do
    if [ "$rearm" != "-" ] && (( SECONDS >= next )); then
      # 趁人还没按,补一次「布景」:既防止 60s 无操作进待机,也保证
      # 场景仍在(通知页一直挂着 / 审批还在等)。
      printf '%s\r\n' "$rearm" >&3
      next=$(( SECONDS + 25 ))
    fi
    read_line || continue
    emit "    $RLINE"
    if [ -n "$failpat" ] && [[ $RLINE =~ $failpat ]]; then
      say "  ✗ 命中失败判据: $RLINE"
      return 1
    fi
    if [[ $RLINE =~ $pat ]]; then
      say "  ✓ $desc"
      return 0
    fi
  done
  say "  ✗ 超时 ${secs}s 未命中: $desc"
  say "    (若这期间没按,按一下就立刻继续;需要重来就重跑本项)"
  return 1
}

RESULTS=()
# --- 真依赖闸门:只认板子自己报的状态,不靠猜 ------------------------------
# 1) net_status 只问本机链路(不看远控有没有配),拿它判「网通不通」;
# 2) rstat 判「远控这一环(== bridge + coding agent)通不通」。
# 两项都不满足就是满足不了,一律记「阻塞」,不拿弱判据顶替成通过。
# ★ 必须留空,不能预置 "unknown":下面的读循环用 [ -n "$RD_BACKEND" ] 判断
#   「回包到齐了没」,预置成字符串会让条件第一轮就成立,一看到 IP 就 break,
#   rstat 的回包永远读不到(10-30 连踩 4 轮)。真要显示兜底用 ${RD_...:-unknown}。
RD_STATE=""
RD_BACKEND=""
NET_OK=0
NET_IP=""
RD_SEEN=0
real_deps() {
  [ "$RD_SEEN" = 1 ] && return 0
  RD_SEEN=1

  # 两条命令一起发,然后一个固定 90s 窗口读到底,两个字段都到齐才提前结束。
  # 10-30 17:09-17:15 实测(踩了三次):
  #   ① 25s 窗口太短:一条 net_status 的回复会被切碎、横跨 60s 才打完
  #      (中间夹着 [lvgl_ui] idle 60000ms → standby clock)。
  #   ② 提前看到 IP 就 break,剩下半条回包还挂在串口上;紧接着发的 rstat
  #      回声直接粘在它后面(实测粘成 "Proxy mode: urstat"),回包被分帧丢掉。
  #   ③ 板子单线程:上一条命令还在往外打时,下一条命令排在输出队列后面才回。
  # 所以不 break、不排空、不重试 —— 发完就老老实实读 90s。
  printf 'net_status\r\n' >&3
  sleep 1
  printf 'rstat\r\n' >&3
  local deadline=$(( SECONDS + 90 ))
  while (( SECONDS < deadline )); do
    read_line || continue
    emit "    $RLINE"
    case "$RLINE" in
      *"Network connected: yes"*) NET_OK=1 ;;
      *"Network connected: no"*)  NET_OK=0 ;;
      *"IP: "*) NET_IP=${RLINE#*IP: }; NET_IP=${NET_IP# } ;;
      *"State     :"*) RD_STATE=${RLINE##*: }; RD_STATE=${RD_STATE# } ;;
      *"Backend   :"*) RD_BACKEND=${RLINE##*: }; RD_BACKEND=${RD_BACKEND# } ;;
    esac
    [ -n "$NET_IP" ] && [ -n "$RD_BACKEND" ] && break
  done
  case "$NET_IP" in ""|0.0.0.0|'()'*) NET_OK=0 ;; esac

  if [ "$NET_OK" = 1 ]; then
    say "  依赖体检: 网络=通(IP=$NET_IP) 远控 State=${RD_STATE:-unknown} Backend=${RD_BACKEND:-unknown}"
  else
    say "  依赖体检: 网络=不通(IP=${NET_IP:-无}) 远控 State=${RD_STATE:-unknown} Backend=${RD_BACKEND:-unknown}"
  fi
}
net_up()    { [ "$NET_OK" = 1 ]; }
remote_up() {
  net_up || return 1
  [ "$RD_STATE" != unknown ] || return 1
  [ "$RD_STATE" != Offline ] || return 1
  [ "$RD_BACKEND" != down ] || return 1
  return 0
}
bypass_ok() { [ "${ALLOW_BYPASS:-0}" = 1 ]; }
# 推送前置检查:vela/apps 工作副本在不在、这个 change 的提交 fetch 过没有。
# 只回答「推得了吗」,不替人 fetch、不推。
aprr_worktree_ready() { [ -n "$APPR_GERRIT_DIR" ] && [ -d "$APPR_GERRIT_DIR/.git" ]; }
aprr_change_fetched() {
  aprr_worktree_ready || return 1
  git -C "$APPR_GERRIT_DIR" for-each-ref --format='%(refname)' 2>/dev/null \
    | grep -q "refs/changes/[0-9][0-9]/$APPR_CHANGE/"
}
blocked() {  # blocked <原因> —— 记阻塞(返回 2),不判旁路
  say "  ⛔ 阻塞: $1"
  say "     正确路径必须真走通这一环;本轮不给通过。"
  return 2
}
phase_name() {  # phase_name <项> —— 汇总里用中文功能名
  case "$1" in
    net)      printf '联网(拿到 IP)' ;;
    start)    printf '启动(宠物首屏 + vela>)' ;;
    config)   printf '配置(网络/DNS/LLM/语音 key)' ;;
    text)     printf '文字(真 LLM 回复)' ;;
    voice)    printf '语音(ASR 识别 + 回复 + TTS)' ;;
    keys)     printf '按键(滚动/换模型/新会话/取消)' ;;
    approval) printf '审批(通过/拒绝)' ;;
    notify)   printf '通知(弹出/自动收起/按键关掉)' ;;
    keys-s2)      printf '按键:S2 单击上滚 + 拉起聊天屏' ;;
    keys-s3)      printf '按键:S3 单击下滚' ;;
    keys-s2long)  printf '按键:S2 长按换模型' ;;
    keys-s3long)  printf '按键:S3 长按新会话' ;;
    keys-combo)   printf '按键:S2+S3 取消' ;;
    appr-pass)    printf '审批:S2 通过' ;;
    appr-deny)    printf '审批:S3 拒绝' ;;
    notify-auto)  printf '通知:5s 自动收起' ;;
    notify-key)   printf '通知:按键关掉' ;;
    *)        printf '%s' "$1" ;;
  esac
}
mark() {  # mark <0|1|2|3> —— 2=阻塞(真依赖没满足) 3=旁路通过(不算真通过)
  case "$1" in
    0) RESULTS+=("✓ $(phase_name "$2")") ;;
    2) RESULTS+=("⛔ $(phase_name "$2")(阻塞:真依赖没满足)") ;;
    3) RESULTS+=("✓(旁路) $(phase_name "$2")——只验了界面/键表,不算真通过") ;;
    *) RESULTS+=("✗ $(phase_name "$2")") ;;
  esac
}

# ===========================================================================
# 功能点
# ===========================================================================

phase_net() {  # 功能:联网真的拿到 IP(必须先在 nsh-ap>)
  hr; say "【功能 1/7】联网:wapi connect 真的拿到 IP"
  local p
  p=$(probe_prompt)
  case "$p" in
    nsh) say "  当前在 nsh-ap>(可以直接联网)" ;;
    vela)
      say "  当前在 vela>;wapi connect 只在 nsh-ap> 生效 → 发 restart 回 nsh-ap>"
      say "  (AP 复位,约 10s;不动烧录、不动 CP 的 Wi-Fi 凭据)"
      send 'restart'
      expect 'nsh-ap>' 60 '回到 nsh-ap>' || return 1
      ;;
    *)
      say "  ✗ 既不是 nsh-ap> 也不是 vela>(串口 TX 掉线?拔插 USB 再试)"
      return 1
      ;;
  esac

  local n ok=0
  for n in $(cfg_lines 'wapi[[:space:]]+connect'); do
    if cfg_is_template "$n"; then
      say "  ○ $(keys_disp):$n 还是 <…> 占位,跳过"
      continue
    fi
    say ">>> [Wi-Fi] 见 $(keys_disp):$n(整行原样下发,内容不显示)"
    printf '%s\r\n' "$(sed -n "${n}p" "$KEYS_FILE")" >&3
    if expect 'BK STA got ip4|wlan0 ip' 30 'Wi-Fi 拿到 IP'; then
      say "  (第 $n 行这组凭据能连上)"
      ok=1
      break
    fi
    say "  …第 $n 行这组没连上,换下一组"
  done
  [ "$ok" = 1 ] || { say "  ✗ 没有一组 Wi-Fi 凭据连上(BK STA got ip4 / wlan0 ip 都没出现)"; return 1; }
  drain_for 2
}

phase_start() {  # 功能:agent 起来 + 宠物首屏上屏 + 提示符是 vela>
  hr; say "【功能 2/7】启动:ai_agent → 宠物首屏 → vela>"
  local p
  p=$(probe_prompt)
  if [ "$p" = vela ]; then
    say "  已经在 vela>(不重复启动)"
  else
    send 'ai_agent'
    expect 'pet screen shown' 90 '宠物首屏上屏' || return 1
    expect 'vela>' 30 'agent CLI 提示符' || return 1
  fi
  send 'set_pet 0'
  expect 'state . 待机' 10 '宠物回 IDLE' || true
  drain_for 1
}

phase_config() {  # 功能:联网状态 + DNS + LLM 凭据 + 语音凭据全部就位
  hr; say "【功能 3/7】配置:net_status / set_dns / set_llm / set_volc_key"
  send 'net_status'
  if ! expect 'Network connected: yes' 15 'AP 认为网络已连接'; then
    say "    ↳ 先跑 net 项:$(keys_disp) 里的 wapi connect"
    return 1
  fi
  # net_status 还有 IP/State/Proxy 几行要吐;串口回显会排队,命令发得太快
  # 会等十几秒才被 CLI 处理(实测),所以先把它排空再发配置命令。
  drain_for 3

  send_cfg 'set_dns' 'DNS' || return 1
  expect 'DNS configured' 45 'DNS 已设置' || true
  drain_for 2

  send_cfg 'set_llm' 'LLM' || return 1
  expect 'LLM backend: |LLM config updated atomically|API key saved' 45 \
         'LLM 凭据/模型已写入' || return 1
  drain_for 2

  send_cfg 'set_volc_key' '豆包语音' || return 1
  expect 'Doubao voice API key saved' 30 '语音 key 已写入' || return 1
  drain_for 2

  send 'config_show'
  local llm_ok=0 volc_ok=0 net_ok=0
  local deadline=$(( SECONDS + 25 ))
  while (( SECONDS < deadline )); do
    read_line || continue
    emit "    $RLINE"
    case "$RLINE" in
      *"Volc API Key"*"****"*) volc_ok=1 ;;
      *"  API Key"*"****"*)    llm_ok=1 ;;
      *"Network: connected"*)  net_ok=1 ;;
      *"============================="*) [ "$net_ok" = 1 ] && break ;;
    esac
  done
  [ "$llm_ok" = 1 ] || { say "  ✗ config_show 里 API Key 不是 ****(没配 LLM key)"; return 1; }
  [ "$volc_ok" = 1 ] || { say "  ✗ config_show 里 Volc API Key 不是 ****(没配语音 key)"; return 1; }
  [ "$net_ok" = 1 ] || { say "  ✗ config_show 里 Network 不是 connected"; return 1; }
  say "  ✓ LLM key / 语音 key / 网络 三项都就位"
  drain_for 1
}

phase_text() {  # 功能:文字一轮真的走通 LLM(不是 LLM unavailable)
  hr; say "【功能 4/7】文字:ask → 真 LLM 回复 → 气泡上屏 → 8s 让位"
  real_deps
  if ! net_up; then blocked "网没通(net_status IP=${NET_IP:-无}),LLM 请求发不出去"; return 2; fi
  send 'ask 你好，请用一句话介绍你自己'
  expect 'Processing message from cli' 20 'agent 收到消息' || return 1
  expect --not 'LLM unavailable' 'OpenAI API with tools' "$REPLY_WAIT" '真的发起了 LLM 请求' || return 1
  expect --not 'LLM unavailable' '\[agent\] reply [0-9]+ bytes' "$REPLY_WAIT" '回复到达(真 LLM,不是降级话术)' || return 1
  expect 'idle . pet home screen|pet home screen' $(( 8 + 25 )) '8s 后聊天屏让位回宠物(功能)' || \
    say "  ⚠ 没看到让位日志(界面可能还停在聊天屏),不影响 LLM 判定"
  drain_for 1
}

phase_voice() {  # 功能:S1 录音 → ASR 真识别 → 回复 → TTS 真出声
  hr; say "【功能 5/7】语音:按住 S1 说话 → 识别 → 回复 → TTS 出声"
  real_deps
  if ! net_up; then blocked "网没通(net_status IP=${NET_IP:-无}),ASR/TTS 都在云上"; return 2; fi
  remind "语音:按住 S1 说 2~3 秒再松开" \
         "说完就松手。判据: PTT press → PTT release → recognized: 有文字 → TTS first chunk"
  expect_key 'PTT press' "$KEY_WAIT" '按下 S1 → 开始录音' 'set_page 0' || return 1
  expect 'PTT release' 60 '松开 S1 → 开始识别' || return 1
  expect_key 'recognized: .+' "$ASR_WAIT" 'ASR 真的识别出文字' '-' 'no speech recognized|ASR failed' || return 1
  expect --not 'LLM unavailable' '\[agent\] LLM resp: text=[0-9]+' "$REPLY_WAIT" 'Agent 回复(真 LLM)' || return 1
  expect 'TTS first chunk' 30 'TTS 真的开始出声' || \
    say "  ⚠ 没看到 TTS first chunk(回复可能有,但没播出来?)"
  drain_for 1
}

phase_keys() {  # 功能:S2/S3 真的换界面/换模型/新会话/取消
  hr; say "【功能 6/7】按键:S2/S3 单击上/下滚 + 长按换模型/新会话 + 组合取消"
  say "  前置:前两项已产生对话记录(聊天屏才会被拉起来)"
  real_deps
  # 按键的真实效果全在别处:滚动要有聊天记录(真 ask),换模型要有 bridge 下发的
  # 模型表,新会话要有远控会话。网不通时它们只能「什么都不做」——那种情况只判
  # [key] ... -> hit 就是旁路,所以这里直接记阻塞。
  if ! net_up; then blocked "网没通(net_status IP=${NET_IP:-无}):滚动没聊天记录、换模型没模型表、新会话没会话"; return 2; fi
  if ! remote_up; then blocked "远控没连上(Backend=${RD_BACKEND:-?}):模型表/新会话都由 bridge 提供"; return 2; fi
  # 回宠物页,让「第一次单击」能明确证明「按键把聊天屏拉起来了」
  send 'set_page 0'
  drain_for 2

  remind "按键 1/5:单击 S2(聊天上滚)" \
         "按一下就松。判据:[key] idle keys=2 hit + loading chat screen(聊天屏被拉起)+ 眼睛看气泡上移"
  expect_key '\[key\] idle: keys=2 gesture=0 -> hit' "$KEY_WAIT" \
             'S2 单击命中 idle 键表' 'set_page 0' || return 1
  expect 'loading chat screen' 10 'S2 单击真的把聊天屏拉起来' || \
    say "  ⚠ 没看到 loading chat screen(可能聊天屏本来就在前台)"
  say "  (请看屏幕:气泡应向上滚一格)"

  remind "按键 2/5:单击 S3(聊天下滚)" \
         "按一下就松。判据:[key] idle keys=4 hit + 气泡下移"
  expect_key '\[key\] idle: keys=4 gesture=0 -> hit' "$KEY_WAIT" \
             'S3 单击命中 idle 键表' 'set_page 0' || return 1
  say "  (请看屏幕:气泡应向下滚一格)"

  remind "按键 3/5:长按 S2 约 1 秒(≥0.8s)再松开 —— 切换模型" \
         "判据:[key] idle keys=2 gesture=1 hit + Model -> <模型名>"
  expect_key '\[key\] idle: keys=2 gesture=1 -> hit' "$KEY_WAIT" \
             'S2 长按命中 idle 键表' 'set_page 0' || return 1
  expect_key 'Model -> ' 10 '模型真的被切换(打印新模型名)' '-' \
             'No model list to cycle|Model switch request failed' || return 1

  remind "按键 4/5:长按 S3 约 1 秒(≥0.8s)再松开 —— 新会话" \
         "判据:[key] idle keys=4 gesture=1 hit + New session requested."
  expect_key '\[key\] idle: keys=4 gesture=1 -> hit' "$KEY_WAIT" \
             'S3 长按命中 idle 键表' 'set_page 0' || return 1
  expect_key 'New session requested\.' 10 '新会话请求真的发出' '-' \
             'New-session request failed' || return 1

  remind "按键 5/5:同时按住 S2+S3,再一起松开 —— 取消" \
         "判据:[key] global keys=6 gesture=3 命中 + [key] cancel"
  expect_key '\[key\] global: keys=6 gesture=3 -> hit' "$KEY_WAIT" \
             'S2+S3 组合命中 global 键表' 'set_page 0' || return 1
  expect_key '\[key\] cancel' 10 '取消动作真的执行' '-' || return 1
  drain_for 1
}

phase_approval() {  # 功能:S2 真通过 / S3 真拒绝
  hr; say "【功能 7/7a】审批:S2 通过一次 → S3 拒绝"
  real_deps
  if ! bypass_ok; then
    # 真审批只能由 PC 侧 coding agent 发起一次工具调用产生。
    if ! remote_up; then
      blocked "审批必须来自 PC 侧真 agent(net_status IP=${NET_IP:-无} 远控 State=${RD_STATE:-?} Backend=${RD_BACKEND:-?});rpending 是假审批"; return 2
    fi
    say "  本轮真改动目标: ${APPR_FILE}"
    say "  改完推送目标: $APPR_PUSH_URL  (change $APPR_CHANGE 的新 patchset,不新推 change)"
    if aprr_change_fetched; then
      say "  推送前置: 工作副本 $APPR_GERRIT_DIR 里已有该 change 的提交 → 批准后可推 patchset"
    elif aprr_worktree_ready; then
      say "  推送前置: 有工作副本($APPR_GERRIT_DIR),但该 change 的提交没 fetch 下来"
      say "            → 推之前要先 fetch refs/changes/xx/$APPR_CHANGE/*(脚本不代做)"
    else
      say "  推送前置: 缺 vela/apps 工作副本($APPR_GERRIT_DIR 不存在)→ 本轮只验到「改代码+审批」,不推"
    fi
    if [ ! -f "$APPR_FILE" ]; then
      blocked "目标文件不存在($APPR_FILE):先确认路径再验审批"; return 2
    fi
    # 真审批的「因」:真让 agent 去改这个文件。只改文件、不 commit、不 push ——
    # 推送是审批通过之后另一步,而且要有工作副本才能做。
    say "  触发真审批:rask → agent 改 $APPR_FILE(只改文件,不 commit 不 push)"
    send "rask edit $APPR_FILE : append /* deskmate approval probe */; no commit, no push"
    if ! expect 'approval:|\[需要审批\]|Approval needed|approval pending' "$APPROVAL_WAIT" '真审批请求到达'; then
      blocked "窗口内 PC 侧 agent 没有发起工具调用(它没在跑,或这一轮没走到要批的步骤)"; return 2
    fi
    drain_for 1
    remind "审批 1/2:单击 S2 —— 通过一次" \
           "判据:[key] approval keys=2 hit + Approved";
    expect_key '\[key\] approval: keys=2 gesture=0 -> hit' "$KEY_WAIT" \
               '按键落到审批键表(通过)' '-' || return 1
    expect 'Approved' 15 '审批真的被通过' || return 1
    drain_for 1

    say "  …等第一轮收尾(reply … bytes / [Agent]:),再发第二条"
    expect 'reply [0-9]+ bytes|\[Agent\]:' 120 '第一轮收尾' \
      || say "  (没等到收尾标记,仍尝试发第二条;若报 A turn is running 说明还没结束)"
    # 拒绝分支同样要有真因:再让 agent 改一次,而不是干等它自己发起第二次。
    say "  触发第二次真审批:rask → agent 再改一次(只改文件,不 commit 不 push)"
    send "rask edit $APPR_FILE : append another /* deskmate denial probe */; no commit, no push"
    if ! expect 'approval:|\[需要审批\]|Approval needed|approval pending' "$APPROVAL_WAIT" '第二次真审批到达'; then
      blocked "第二次真审批没到(agent 没再发起工具调用),拒绝分支未验"; return 2
    fi
    drain_for 1
    remind "审批 2/2:单击 S3 —— 拒绝" \
           "判据:[key] approval keys=4 hit + Denied"
    expect_key '\[key\] approval: keys=4 gesture=0 -> hit' "$KEY_WAIT" \
               '按键落到审批键表(拒绝)' '-' || return 1
    expect 'Denied' 15 '审批真的被拒绝' || return 1
    drain_for 1
    # 推送不属于本项判据:审批验的是「板键能决定 agent 的工具调用」。
    # 推 patchset 另算一步,而且必须有 vela/apps 的工作副本。
    say "  推送目标记下(本项不判): $APPR_PUSH_URL"
    if aprr_change_fetched; then
      say "    → 工作副本里已有该 change 的提交;下一步可让 agent commit --amend 推 patchset"
    elif aprr_worktree_ready; then
      say "    → 有工作副本 $APPR_GERRIT_DIR,但该 change 未 fetch:先 fetch 再推"
    else
      say "    → 缺 vela/apps 工作副本:先给 APPR_GERRIT_DIR,否则推不了(不许假推)"
    fi
    return 0
  fi

  # ---- 以下为旁路:rpending 本地造审批,只验「按键能落到审批键表」----
  BYPASS_USED=1
  say "  ⚠ 旁路模式(ALLOW_BYPASS=1):用 rpending 本地造审批,只证明按键键表有效"
  send 'rpending get_current_time 现在几点了'
  expect 'Simulated approval pending' 10 '审批页出现' || return 1
  drain_for 1

  remind "审批 1/2:单击 S2 —— 通过一次" \
         "判据:[key] approval keys=2 hit + Approved once (debug)."
  expect_key '\[key\] approval: keys=2 gesture=0 -> hit' "$KEY_WAIT" \
             '按键落到审批键表(通过)' 'rpending get_current_time 现在几点了' || return 1
  expect 'Approved once \(debug\)\.' 15 '审批真的被通过' || return 1
  drain_for 1

  send 'rpending compile audio.c 要编译吗'
  expect 'Simulated approval pending' 10 '第二个审批页出现' || return 1
  drain_for 1

  remind "审批 2/2:单击 S3 —— 拒绝" \
         "判据:[key] approval keys=4 hit + Denied (debug)."
  expect_key '\[key\] approval: keys=4 gesture=0 -> hit' "$KEY_WAIT" \
             '按键落到审批键表(拒绝)' 'rpending compile audio.c 要编译吗' || return 1
  expect 'Denied \(debug\)\.' 15 '审批真的被拒绝' || return 1
  drain_for 1
}

phase_notify() {  # 功能:通知页真的弹出、自己收起、按键能关掉且被吞
  hr; say "【功能 7/7b】通知:真事件 → 通知页 → 5s 自动收起 → 按键关掉且不误触"
  real_deps
  if ! bypass_ok; then
    if ! net_up; then blocked "网没通(net_status IP=${NET_IP:-无}):notify_service 轮询不到 Gerrit/Jira"; return 2; fi
    send 'set_page 0'
    drain_for 2
    say "  前置:通知只在「屏醒着」时上屏;待机时钟在屏上时事件会被丢弃(设计如此)"
    say "  真事件:等 notify_service 轮询到一条 Gerrit/Jira 事件(最多 ${NOTIFY_WAIT}s,其间自动保活)"
    if ! expect_awake '\[lvgl_notice\] show: (Gerrit|Jira)' "$NOTIFY_WAIT" '真事件 → 通知页上屏'; then
      blocked "窗口内没有真事件到达(notify_service 没轮询到,或还没有新事件)"; return 2
    fi
    drain_for 1
    expect '\[lvgl_notice\] hidden \(auto-dismiss\)' 20 '停留 5s 后自己收起' || return 1
    drain_for 1
    say "  …按键退出那一半需要再一条真事件;5s 窗口很紧,先准备好手指"
    remind "下一条通知页一出现就立刻单击 S2(只有 5s)" \
           "判据:[key] notice ... dismissed + hidden (dismissed by key);这一按不该滚聊天"
    if ! expect_awake '\[lvgl_notice\] show: (Gerrit|Jira)' "$NOTIFY_WAIT" '第二条真事件到达'; then
      blocked "没有第二条真事件,「按键退出」这一半未验"; return 2
    fi
    expect_key '\[key\] notice: .*dismissed' 10 '按键被通知页吞掉' '-' || return 1
    expect '\[lvgl_notice\] hidden \(dismissed by key\)' 10 '通知页真的被按键关掉' || return 1
    drain_for 1
    return 0
  fi

  # ---- 以下为旁路:notify_inject 注入,只验「页面画得出来、能退出」----
  BYPASS_USED=1
  say "  ⚠ 旁路模式(ALLOW_BYPASS=1):用 notify_inject 注入,只证明通知页能画/能退"
  send 'set_page 0'
  drain_for 2

  send 'notify_inject gerrit 9688768 "CI failed: audio.c build error"'
  expect '\[lvgl_notice\] show: Gerrit' 15 '通知页真的弹出(Gerrit)' || return 1
  expect '\[lvgl_notice\] hidden \(auto-dismiss\)' 20 '停留 5s 后自己收起' || return 1
  drain_for 1

  send 'notify_inject jira DEMO-1 "demo: check notification page"'
  expect '\[lvgl_notice\] show: Jira' 15 '通知页真的弹出(Jira)' || return 1
  remind "通知:通知页弹出后单击 S2" \
         "判据:[key] notice ... dismissed + hidden (dismissed by key);这一按不该滚聊天"

  # 5s 窗口对手按太紧:每 3s 补一条 Jira(后到覆盖,停留窗口重算),页面一直挂着
  local deadline=$(( SECONDS + KEY_WAIT )) got=0 n=2 sub
  while (( SECONDS < deadline )); do
    printf '%s\r\n' "notify_inject jira DEMO-$n \"demo: keep page up\"" >&3
    n=$(( n + 1 ))
    sub=$(( SECONDS + 3 ))
    while (( SECONDS < sub )); do
      read_line || continue
      emit "    $RLINE"
      case "$RLINE" in
        *"[key] notice:"*dismissed*) got=1; break 2 ;;
      esac
    done
  done
  if [ "$got" = 1 ]; then
    say "  ✓ 按键被通知页吞掉(没有落到滚动/审批)"
    expect '\[lvgl_notice\] hidden \(dismissed by key\)' 10 '通知页真的被按键关掉' || return 1
  else
    say "  ✗ ${KEY_WAIT}s 内没看到按键退出通知页"
    return 1
  fi
  drain_for 1
}

# --- 单步:一次只等一次按键 -------------------------------------------------
# 配合「在会话里一步一步问」的用法:每次调用只布景 + 等一个动作,
# 上一步结果出来后再决定下一步,不会把后面要做的事提前打给人看。

step_keys_s2() {
  say "  布景:回宠物页(这样「第一次单击」能证明聊天屏被拉起)"
  send 'set_page 0'; drain_for 2
  expect_key '\[key\] idle: keys=2 gesture=0 -> hit' "$KEY_WAIT" \
             'S2 单击命中 idle 键表' 'set_page 0' || return 1
  expect 'loading chat screen' 10 '聊天屏被拉起' || \
    say "  ⚠ 没看到 loading chat screen(可能聊天屏本来就在前台)"
}

step_keys_s3() {
  expect_key '\[key\] idle: keys=4 gesture=0 -> hit' "$KEY_WAIT" \
             'S3 单击命中 idle 键表' 'set_page 0'
}

step_keys_s2long() {
  expect_key '\[key\] idle: keys=2 gesture=1 -> hit' "$KEY_WAIT" \
             'S2 长按命中 idle 键表' 'set_page 0' || return 1
  expect_key 'Model -> ' 10 '模型真的被切换' '-' \
             'No model list to cycle|Model switch request failed'
}

step_keys_s3long() {
  expect_key '\[key\] idle: keys=4 gesture=1 -> hit' "$KEY_WAIT" \
             'S3 长按命中 idle 键表' 'set_page 0' || return 1
  expect_key 'New session requested\.' 10 '新会话请求真的发出' '-' \
             'New-session request failed'
}

step_keys_combo() {
  expect_key '\[key\] global: keys=6 gesture=3 -> hit' "$KEY_WAIT" \
             'S2+S3 组合命中 global 键表' 'set_page 0' || return 1
  expect_key '\[key\] cancel' 10 '取消动作真的执行' '-'
}

step_appr_pass() {
  bypass_ok || { blocked "审批必须来自 PC 侧真 agent;rpending 是假审批(要跑注入版请加 ALLOW_BYPASS=1,结果会标旁路)"; return 2; }
  BYPASS_USED=1
  send 'rpending get_current_time 现在几点了'
  expect 'Simulated approval pending' 10 '审批页出现' || return 1
  drain_for 1
  expect_key '\[key\] approval: keys=2 gesture=0 -> hit' "$KEY_WAIT" \
             '按键落到审批键表(通过)' 'rpending get_current_time 现在几点了' || return 1
  expect 'Approved once \(debug\)\.' 15 '审批真的被通过'
}

step_appr_deny() {
  bypass_ok || { blocked "审批必须来自 PC 侧真 agent;rpending 是假审批(要跑注入版请加 ALLOW_BYPASS=1,结果会标旁路)"; return 2; }
  BYPASS_USED=1
  send 'rpending compile audio.c 要编译吗'
  expect 'Simulated approval pending' 10 '审批页出现' || return 1
  drain_for 1
  expect_key '\[key\] approval: keys=4 gesture=0 -> hit' "$KEY_WAIT" \
             '按键落到审批键表(拒绝)' 'rpending compile audio.c 要编译吗' || return 1
  expect 'Denied \(debug\)\.' 15 '审批真的被拒绝'
}

step_notify_auto() {
  bypass_ok || { blocked "通知必须来自 Gerrit/Jira 真事件;notify_inject 是注入(要跑注入版请加 ALLOW_BYPASS=1,结果会标旁路)"; return 2; }
  BYPASS_USED=1
  send 'set_page 0'; drain_for 2
  send 'notify_inject gerrit 9688768 "CI failed: audio.c build error"'
  expect '\[lvgl_notice\] show: Gerrit' 15 '通知页真的弹出(Gerrit)' || return 1
  expect '\[lvgl_notice\] hidden \(auto-dismiss\)' 20 '停留 5s 后自己收起'
}

step_notify_key() {
  bypass_ok || { blocked "通知必须来自 Gerrit/Jira 真事件;notify_inject 是注入(要跑注入版请加 ALLOW_BYPASS=1,结果会标旁路)"; return 2; }
  BYPASS_USED=1
  send 'set_page 0'; drain_for 2
  send 'notify_inject jira DEMO-1 "demo: check notification page"'
  expect '\[lvgl_notice\] show: Jira' 15 '通知页真的弹出(Jira)' || return 1
  local deadline=$(( SECONDS + KEY_WAIT )) got=0 n=2 sub
  while (( SECONDS < deadline )); do
    printf '%s\r\n' "notify_inject jira DEMO-$n \"demo: keep page up\"" >&3
    n=$(( n + 1 ))
    sub=$(( SECONDS + 3 ))
    while (( SECONDS < sub )); do
      read_line || continue
      emit "    $RLINE"
      case "$RLINE" in
        *"[key] notice:"*dismissed*) got=1; break 2 ;;
      esac
    done
  done
  [ "$got" = 1 ] || { say "  ✗ ${KEY_WAIT}s 内没看到按键退出通知页"; return 1; }
  say "  ✓ 按键被通知页吞掉(没有落到滚动/审批)"
  expect '\[lvgl_notice\] hidden \(dismissed by key\)' 10 '通知页真的被按键关掉'
}

# --- 入口 -------------------------------------------------------------------
ALL="net start config text voice keys approval notify"

usage() {
  cat <<EOF
用法: ./func-test.sh [项…]
  整项: $ALL
  单步(一次只等一次按键): keys-s2 keys-s3 keys-s2long keys-s3long keys-combo
                            appr-pass appr-deny notify-auto notify-key
  --list    列出各项
  --lines   列出配置在 cmd_scripts.sh 的行号(不显示内容)
全跑(默认): net → start → config → text → voice → keys → approval → notify
审批本轮目标: ${APPR_FILE#$PWD/}
             → 推 $APPR_PUSH_URL (change $APPR_CHANGE 的新 patchset,不新推 change)
             改了要推:该 change 得先 fetch 进 APPR_GERRIT_DIR(默认 vela-miwear-bes-o62-25rc05-lc3/apps)
说明:只验「功能成功」;流程顺序 + 失败分支请用 run-ui-flow.sh / flow.sh。
EOF
}

main() {
  local -a want=()
  local a
  for a in "$@"; do
    case "$a" in
      --list|-l) usage; exit 0 ;;
      --lines)   show_lines; exit $? ;;
      -h|--help) usage; exit 0 ;;
      net|start|config|text|voice|keys|approval|notify) want+=("$a") ;;
      keys-s2|keys-s3|keys-s2long|keys-s3long|keys-combo) want+=("$a") ;;
      appr-pass|appr-deny|notify-auto|notify-key) want+=("$a") ;;
      *) say "未知参数: $a"; usage; exit 2 ;;
    esac
  done
  if [ ${#want[@]} -eq 0 ]; then
    for a in $ALL; do want+=("$a"); done
  fi

  open_serial || exit 2
  say "功能验收开始(成功路径);每项独立判定,失败不会中断后面的项。"
  say "需要人工按键的项会响铃 + 弹桌面通知。"

  local p fn rc
  BYPASS_USED=0
  for p in "${want[@]}"; do
    case "$p" in
      keys-s2|keys-s3|keys-s2long|keys-s3long|keys-combo|\
      appr-pass|appr-deny|notify-auto|notify-key)
        fn="step_${p//-/_}" ;;
      *) fn="phase_$p" ;;
    esac
    rc=0
    BYPASS_USED=0
    "$fn" || rc=$?
    if   [ "$rc" = 0 ] && [ "$BYPASS_USED" = 1 ]; then mark 3 "$p"
    elif [ "$rc" = 0 ]; then mark 0 "$p"
    elif [ "$rc" = 2 ]; then mark 2 "$p"
    else mark 1 "$p"
    fi
  done

  hr
  say "汇总:"
  local r
  for r in "${RESULTS[@]}"; do say "  $r"; done
  hr
  local fail=0 blk=0 byp=0
  for r in "${RESULTS[@]}"; do
    case "$r" in
      ✗*) fail=1 ;;
      ⛔*) fail=1; blk=$(( blk + 1 )) ;;
      "✓(旁路)"*) byp=$(( byp + 1 )) ;;
    esac
  done
  [ "$blk" = 0 ] || say "⛔ 有 $blk 项是「阻塞」:真依赖没满足,这些项没被验证过(也没被判成通过)。"
  [ "$byp" = 0 ] || say "⚠ 有 $byp 项是「旁路通过」:只证明了界面/键表,不算功能走通。"
  if [ "$fail" = 0 ]; then
    say "✅ 功能全部通过(真路径)。日志: $LOG"
  else
    say "❌ 有功能项未通过 / 未验证(见上)。日志: $LOG"
  fi
  return "$fail"
}

main "$@"
