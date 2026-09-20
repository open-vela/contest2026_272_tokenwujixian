# BK7258 DevKit AI 硬件适配 —— openvela 大赛参赛作品（队伍 272）

## 一、作品简介

将 Beken BK7258 DevKit（Cortex-M33 三核 AMP：CP=CPU0，AP=CPU1/2）完整适配到 openvela/NuttX，并在其上跑通 AI Agent。本作品交付从裸板到 AI 应用的全链路：

- **Wi-Fi 通信核（CP）**：闭源 `libwifi.a` + Armino 权威源码导入的 STA 链路，扫描 / WPA2-CCMP 关联 / 四次握手 / DHCP / DNS / 公网可达全部真机验证
- **应用核（AP）跨核上网**：NuttX/Vela 原生 `usrsock over RPMsg`——AP 上的普通 socket 经共享内存与 mailbox 转移到 CP 执行，经 CP 的 wlan0 出网；AP 侧应用零改动
- **PSRAM 双核分割**：16MB PSRAM 软分区（CP 下半 / AP 上半各 8MB），AP 堆从 244KB SRAM 扩到 8.4MB
- **AI Agent on AP**：minimal ai_agent 在 AP 完整自举（tmpfs 存储 / 20 工具 / CLI），配置 OpenAI 兼容端点后完成首个真实 LLM 对话——**消息从 AP 发出，经跨核 usrsock 与 CP 的 Wi-Fi 抵达云端模型并返回回复**

## 二、选题方向

**新硬件适配**（BK7258 DevKit BSP 移植）+ 在适配成果上直接落地 **AI 硬件产品**能力（跨核 AI Agent）。

## 三、目录结构

| 目录 | 内容 |
| --- | --- |
| `chips/bk7258/` | 芯片层：约 550 个 .c。`armino/` 为 Armino 权威源码导入（保留上游目录结构与 provenance，`cmp` 逐字节对齐），`hal_port/` 为团队适配层（OSAL shim、vnd_cal、诊断探针、usrsock 客户端接入） |
| `board/bk7258-devkit/` | 板级：9 个 defconfig（`cp`/`ap`/`ap-net`/`psram-*` 等）、RPTUN/mailbox/PSRAM 接线、L2 打包脚本（线性 CRC + 独立解码校验）、烧录工具与回归测试 |
| `app/` | `bk7258_platform_test`（平台验证）、`bk7258_gdma_test`（DMA）、`deskmate`（DeskMate 客户端骨架） |
| `evidence-2026*/` | 真机证据日志（关联、DHCP、DNS、公网 ping、AP 跨核上网，PSK 均已脱敏为 `<PSK-REDACTED>`） |
| `docs/` | bring-up 复盘（20 个根因）、`compose/spec/`（native WAPI spec、OTP/mb_ipc spec） |
| `logs/` | AI Coding 日志（格式见 logs/README.md） |

## 四、运行方式

```bash
# 1. 拉取工程（工作区根目录）
repo init -u https://github.com/open-vela/contest2026_272_tokenwujixian \
  -b dev-ai-contest-2026 -m contest2026_272_tokenwujixian.xml
repo sync -c -j8

# 2. 构建（openvela 工作区根目录；以 CP 为例）
./build.sh vendor/beken/boards/bk7258/bk7258-devkit/configs/cp/ --cmake -j8

# 3. 打包 L2 镜像（CP app.bin + vendor bootloader + OpenVela AP app1.bin）
#    板级打包脚本含独立解码校验，必须输出 decode pass：
contest2026_272_tokenwujixian/board/bk7258-devkit/tools/bk7258-package.sh \
  --cp-app-bin <cp输出>/bk7258/app.bin --cp-validation-report <...>/l1-validation.json \
  --openvela-ap --ap-app-bin <ap输出>/bk7258_ap/app1.bin --ap-validation-report <...>/l1-validation.json

# 4. 烧录（BK7258 DevKit，/dev/ttyUSB0）
contest2026_272_tokenwujixian/board/bk7258-devkit/tools/bk7258-flash.sh \
  --image <package>/all-app.bin

# 5. 真机验收（CP console = UART0）
wapi mode wlan0 2
wapi psk wlan0 <PSK> 3          # CCMP
wapi essid wlan0 <SSID> 1       # 置 ON 即关联
renew wlan0                     # DHCP
ping <网关>                     # 链路验证
# AP console：CP NSH 中 cu -l /dev/ttyAP（RPMsg UART）
ai_agent                        # ap-net 配置；tmpfs 自举 + CLI
ask 你好                        # LLM 端到端（需先 set_llm 配置端点与 key）
```

配置说明：`cp` = Wi-Fi 通信核（usrsock server + TCP/UDP 栈）；`ap-net` = 应用核（usrsock client + 最小 ai_agent + 8MB PSRAM 上半区）；其余 `psram-*` 为调试配置。构建、打包、烧录的完整契约与已知边界见 `board/bk7258-devkit/README.md`。

## 五、关键设计与取舍

- **跨核上网采用 NuttX/Vela 原生 usrsock，而非自研转发**：AP 应用使用普通 socket 零改动；期间试验的 `NET_RPMSG_DRV` netdev 桥因对称端点创建死锁而废弃（过程与证据在提交历史），最终仅用上游既有机制，`nuttx/`/`apps/` 零改动
- **PSRAM 无 MPU 下的软件分半**：CP 先启动跑器件 bring-up（探测只写器件头 10KB，属 CP 半区），AP 不重跑初始化、仅添加上半区，两个内核分配器互不越界
- **Wi-Fi 闭源库适配以"权威对齐"为纪律**：Armino 源码逐字节导入（`cmp` 验证），bring-up 期手写 shim 全部清除，偏离权威的每处差异都被证明是 bug（复盘记录了 20 个根因）
- **RPMsg 载荷几何按 usrsock 需求定容**：4×2048B buffer + NIOVEC=16，单请求重组上限 ~30KB（LLM POST 实测 10.6KB）

## 六、AI Coding 使用说明

本作品的 bring-up 与调试高度依赖 AI 辅助开发：

- **调试攻坚**：Wi-Fi RX 半死、四次握手、usrsock 死锁定位等难题中，AI 承担了证据收集（符号级二进制审计、哈希定位断言现场）、根因链推导（如 RPMsg `gfeatures` 协商缺陷的六环证据链）与修复方案生成；人类负责板上取证、方案裁决与"哪些改动值得上游"的取舍
- **过程纪律**：每轮修复以 `--fresh` 构建哈希不变/符号表等值为验收；死代码以"未编译且未被 include"为判据；成果以真机证据日志归档
- **效率**：两周内从裸板 NSH 到 AP 上 LLM 对话，其中约半数提交由 AI 主导完成初版
- 完整对话日志见 `logs/`

