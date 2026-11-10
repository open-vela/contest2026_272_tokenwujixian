---
name: bk7258-ui-ontarget-test
description: "Use when 需要在 BK7258 真机(串口 /dev/ttyUSB0)驱动并判定 ai_agent UI(AP 侧):按最新流程回归宠物/聊天/审批/按键/语音界面、逐条核对预期日志与预期界面、驱动 ai_agent/set_pet/set_page/show_chat/ask 命令,或排查 flush 率异常、串口无回显/TX 掉线、wapi 联网失败、set_llm 模型未配置、在线 ask 后整机静默等显示/联网/模型/控制台写卡死问题。先读 verification-record.md 分清已验/未验,不重复验证。"
---

# BK7258 真机 UI 测试

本 skill 固化「稳定不变的驱动脚手架 + 判定 + 排障」;「具体流程长什么样、判据、已验状态」由本 skill 自带的 `references/` 文档与 `scripts/cmd_scripts.sh` 承担。每次测试前先读文档最新版,再回来用本 skill 执行。

## 单一事实源(先读,持续更新)

| 文档 | 管什么 | 怎么读 |
|---|---|---|
| `references/main_loop.md` | **成功路径 10 段 + §0 判据(唯一「过」的口径)** | 先读 §0;顺序看「## 顺序」;人工按键协作看 §2 |
| `scripts/func-test.sh` | 可执行的测试脚本(依赖闸门 + 判据 + 备注) | 跑测试以它为准;判据与 main_loop §0 一致 |
| `references/ui-flow-and-test.md` | 状态流转 + 界面切换顺序 + 每条支线(A–F)触发→预期日志→预期界面 + 驱动命令速查 | >600 行:先 `grep -n '^#'` 看目录;主线 §7、支线 §9、命令 §10 |
| `references/wifi-and-llm-config-flow.md` | `wapi` 联网 + `set_llm` 配模型的步骤/判据/坑;BUG-20261021-01 定位与结论 | 配网/配模型读 §0–§2;控制台写卡死读 §4 |
| `references/verification-record.md` | **已验证 / 未通过 / 未验证 / 阻塞 的记录** | 开测前先读:只验「未验证」项,已验证的不重复验 |
| `scripts/cmd_scripts.sh` | 烧录/串口/wapi/set_llm/ask 的实际命令(所有密码/密钥/token 用 `<…>` 占位) | 抄命令或取凭据时读;真值由使用者填进本文件,**别写进本 skill 或其它文档** |

以文档为准,不凭记忆;发现文档与真机不符时先改文档再测。**密钥/密码一律留占位符,真值由使用者填进 `scripts/cmd_scripts.sh`(SSID 密码、api_key、volc_key、MCP token、MQTT 密码),不得把真值写进本 skill。**

## 硬性约定(不随流程变,违反必踩坑)

- **两个提示符不互通**:`nsh-ap>`(NuttX shell)只认 `wapi`/`nslookup`/`ping`/`ai_agent`;`vela>`(agent CLI,敲 `ai_agent` 后进入)只认 `set_*`/`ask`/`show_chat` 等,不转发 NSH 命令。
- **永不 `quit` 退 agent**:会卡死、回不到 NSH;退出/重来 = 板上 RESET(K1)或重烧。
- **板子只在收到输入时才输出**:交互/自动化先发 `\r\n` 再等提示符,否则永远等不到。
- **整片烧录清空 NVS**:每次烧完都要重做联网 + 配模型。

## 最小链路(烧录 → 串口 → 联网 → 配模型 → 启动)

每条命令后附**标志性节点**,串口出现它才说明该步成功;`<…>` 占位符真值填进 `scripts/cmd_scripts.sh`。

| # | 步骤 | 命令(提示符) | 标志性节点 |
|---|---|---|---|
| 0 | 烧录 | `full_flow.sh --image <all-app.bin> --worktree <repo> --flash --device /dev/ttyUSB0` | `connect success` → `Gotten Bus...` → `Writing Flash OK`,烧完停在 `nsh-ap>` |
| 1 | 开串口 | `sudo minicom -o -D /dev/ttyUSB0 -b 1000000`(**AP 控制台物理线速率是 1 Mbaud**,`CONFIG_BK7258_UART0_BAUD=1000000`;开 115200 会全静默,那不是挂死) | `nsh-ap>` |
| 2 | 联网 | `wapi connect "<SSID>" <密码>`(`nsh-ap>`) | `BK STA got ip4` / `wlan0 ip` |
| 3 | 启动 | `ai_agent`(`nsh-ap>`) | `vela>` + `[pet_view] pet screen shown` |
| 4 | 配模型 | `set_dns 10.237.25.8` → `set_llm http://model.mify.ai.srv deepseek/deepseek-flash <api_key>` → `config_show`(`vela>`) | `[llm] LLM config updated atomically: ...` |
| 5 | 验证 | `ask 你好`(`vela>`) | `[llm] OpenAI API with tools (...)` |

## 驱动命令速览(以 flow doc「命令速查」为准)

| 命令 | 作用 |
|---|---|
| `set_pet 0..5` | 设宠物状态(0 待机/1 听音/2 思考/3 执行/4 等待/5 错误) |
| `set_page 0` / `show_chat` | 回宠物主页(**只有 page 0**,粒子页已删;传 1 会打印 `set_page: only page 0 (pet home) exists`) / 切聊天屏 |
| `ask <文本>` | 真实 agent 生命周期(驱动 chat + 状态机) |
| `rpending <tool> [hint]` | 造一个假审批(S2/S3 决定;CLI `ronce`/`rdeny` 关不掉) |
| `voice_start`/`voice_stop` | 只驱动音频/ASR,**不驱动 UI**;语音 UI 支线必须物理按 S1 |
| `config_show` / `net_status` | 查凭据配置 / 联网状态 |
| `set_ptype [0..3]` | 换宠物形象(**0 猫 / 1 青蛙 / 2 狗 / 3 猪**,黑猫已删)。每次烧录后首启随机定一只并持久化,运行中切页面不换;无参只回显当前编号 |
| `sync_time` | 查时钟与 SNTP 状态;待机屏日期时间靠它对齐(联网后自动对时) |

## 判定与脱敏

- **判定**:每条步骤同时核「预期日志 + 预期界面」(判据在 flow doc / main_loop §0)。日志级硬判据:无 `flush rc!=0`、无 `assert`/`UsageFault`/`Panic by remote core`、无 `LLM unavailable`(除非故意测出错支线)。
- **自动化**(可选):pyserial 直接开 `/dev/ttyUSB0`(别经 minicom);先写 `\r\n` 再等提示符;`set_llm` 整行从 `scripts/cmd_scripts.sh` 原样下发,避免脚本再存一份 key。
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

1. 流程/判据/已验状态变了 → 先改对应文档(它们是事实源),本 skill 只加引用不抄内容。
2. 架构约定没变 → 本 skill 不动。
3. 只有「脚手架」变了(串口节点、烧录方式、脱敏、排障表)→ 改本 skill 对应节。
4. 别把最新测试状态 / 镜像 SHA / 密钥写进本 skill(那是 flow doc 与 `scripts/cmd_scripts.sh` 的职责)。
