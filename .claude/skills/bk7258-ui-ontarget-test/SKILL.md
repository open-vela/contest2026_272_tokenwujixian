---
name: bk7258-ui-ontarget-test
description: "Use when 需要在 BK7258 真机(串口 /dev/ttyUSB0)驱动并判定 ai_agent UI(AP 侧):按最新流程回归宠物/聊天/审批/按键/语音界面、逐条核对预期日志与预期界面、驱动 ai_agent/set_pet/set_page/show_chat/ask 命令,或排查 flush 率异常、串口无回显/TX 掉线、wapi 联网失败、set_llm 模型未配置、在线 ask 后整机静默等显示/联网/模型/控制台写卡死问题。"
---

# BK7258 真机 UI 测试

本 skill 固化「稳定不变的驱动脚手架 + 判定 + 排障」;「具体流程长什么样」由两份持续更新的文档承担。每次测试前先读文档最新版,再回来用本 skill 执行。

## 单一事实源(先读,持续更新)

| 文档 | 管什么 | 怎么读 |
|---|---|---|
| `../../../../yzhang/testLoop/ui-screen-transition-order.md` | 界面切换顺序、每条支线(A–F)的触发→预期日志→预期界面、驱动命令速查 | 文档 >100 行:先 `grep -n '^#'` 看目录;判据看「交互支线测试指令」,命令看「驱动命令速查」 |
| `../../../../yzhang/testLoop/wifi-and-llm-config-flow.md` | `wapi` 联网 + `set_llm` 配模型的步骤/判据/坑;BUG-20261021-01 定位与最终结论 | 配网/配模型读 §0–§2;控制台写卡死读 §4 |
| `../../../../yzhang/cmd.sh` | 烧录/串口/wapi/set_llm/ask 的实际命令(含密码/密钥) | 抄命令或取凭据时读;只原文引用,**密钥不写进本 skill** |

以文档为准,不凭记忆;发现文档与真机不符时先改文档再测。

## 硬性约定(不随流程变,违反必踩坑)

- **两个提示符不互通**:`nsh-ap>`(NuttX shell)只认 `wapi`/`nslookup`/`ping`/`ai_agent`;`vela>`(agent CLI,敲 `ai_agent` 后进入)只认 `set_*`/`ask`/`show_chat` 等,不转发 NSH 命令。
- **永不 `quit` 退 agent**:会卡死、回不到 NSH;退出/重来 = 板上 RESET(K1)或重烧。
- **板子只在收到输入时才输出**:交互/自动化先发 `\r\n` 再等提示符,否则永远等不到。
- **整片烧录清空 NVS**:每次烧完都要重做联网 + 配模型。

## 最小链路(烧录 → 串口 → 联网 → 配模型 → 启动)

每条命令后附**标志性节点**,串口出现它才说明该步成功;密码/密钥照抄 `cmd.sh`。

| # | 步骤 | 命令(提示符) | 标志性节点 |
|---|---|---|---|
| 0 | 烧录 | `full_flow.sh --image <all-app.bin> --worktree <repo> --flash --device /dev/ttyUSB0` | `connect success` → `Gotten Bus...` → `Writing Flash OK`,烧完停在 `nsh-ap>` |
| 1 | 开串口 | `sudo minicom -o -D /dev/ttyUSB0 -b 115200` | `nsh-ap>` |
| 2 | 联网 | `wapi connect "<SSID>" <pw>`(`nsh-ap>`) | `BK STA got ip4` / `wlan0 ip` |
| 3 | 启动 | `ai_agent`(`nsh-ap>`) | `vela>` + `[pet_view] pet screen shown` |
| 4 | 配模型 | `set_dns 10.237.25.8` → `set_llm http://model.mify.ai.srv deepseek/deepseek-flash <api_key>` → `config_show`(`vela>`) | `[llm] LLM config updated atomically: ...` |
| 5 | 验证 | `ask 你好`(`vela>`) | `[llm] OpenAI API with tools (...)` |

## 驱动命令速览(以 flow doc「命令速查」为准)

| 命令 | 作用 |
|---|---|
| `set_pet 0..5` | 设宠物状态(0 待机/1 听音/2 思考/3 执行/4 等待/5 错误) |
| `set_page 0\|1` / `show_chat` | 切宠物页/粒子页 / 切聊天屏 |
| `ask <文本>` | 真实 agent 生命周期(驱动 chat + 状态机) |
| `rpending <tool> [hint]` | 造一个假审批(S2/S3 决定;CLI `ronce`/`rdeny` 关不掉) |
| `voice_start`/`voice_stop` | 只驱动音频/ASR,**不驱动 UI**;语音 UI 支线必须物理按 S1 |
| `config_show` / `net_status` | 查凭据配置 / 联网状态 |
| `set_ptype 0..4` | 换宠物形象(0 白猫/1 绿青蛙/2 黑猫/3 棕狗/4 粉猪) |
| `sync_time` | 查时钟与 SNTP 状态;待机屏日期时间靠它对齐(联网后自动对时) |

## 判定与脱敏

- **判定**:每条步骤同时核「预期日志 + 预期界面」(判据在 flow doc)。日志级硬判据:无 `flush rc!=0`、无 `assert`/`UsageFault`/`Panic by remote core`、无 `LLM unavailable`(除非故意测出错支线)。
- **自动化**(可选):pyserial 直接开 `/dev/ttyUSB0`(别经 minicom);先写 `\r\n` 再等提示符;`set_llm` 整行从 `cmd.sh` 原样下发,避免脚本再存一份 key。
- **脱敏**:落到文件/日志的密钥先打码再存/提交(见 `contest-log-redact`,必须 `--verify` 输出 `CLEAN` 且退出码 0)。

## 排障速查(架构级,不随流程变)

| 现象 | 结论/处理 |
|---|---|
| 发命令无回显,只有板子→host 日志在滚 | host→board **TX 掉线**;物理拔插 USB,别盲刷 |
| `set_wifi` 没反应 | AP 上 `set_wifi` 是空壳;用 `wapi connect` |
| `nslookup: ... Unreachable: 0100000a` | resolver 还用默认 DNS 10.0.0.1;先 `wapi connect`,进 `vela>` 后 `set_dns` |
| `ifconfig` 空 | AP 是纯 usrsock 客户端,网卡在 CP;属正常,看 `net_status` |
| flush 率异常慢(~2–10/s) | defconfig 误切 `SPI_BITBANG`;应 HW SPI + `CONFIG_SPI=y` |
| 在线 `ask` 后 ~20s 整机静默 | BUG-20261021-01,真根因=RPMSG 控制台写 `wait=true` 无限阻塞,已修(`uart_rpmsg_dmasend` 改 `wait=false`);仍复现见 wifi-and-llm-config-flow.md §4.4 |

## 保持不过时

1. 流程/判据变了 → 先改两份 flow doc(它们是事实源)。
2. 架构约定没变 → 本 skill 不动。
3. 只有「脚手架」变了(串口节点、烧录方式、脱敏、排障表)→ 改本 skill 对应节。
4. 别把最新测试状态 / 镜像 SHA 写进本 skill(那是 flow doc「现状待定」的职责)。
