---
name: qemu-voice-loop-recovery
description: >-
  恢复或重建 goldfish qemu 语音闭环演示环境（HFP 蓝牙耳麦→ASR→LLM→TTS→LVGL）
  及配套宿主服务。覆盖：宿主重启或 /tmp 被清后重建闭环、固件/vela_data.bin
  缺失时按可复现步骤从源码重编 qemu 镜像、`/data` FAT 损坏（TTS -EIO）、
  mosquitto / mimo bridge / PC MCP server / Gerrit worktree 掉线、qemu 网络
  未就绪或 SLIRP NAT 竞态、演示前排障。Use when: "恢复环境"、"重建闭环"、
  重启后 qemu 语音失灵、qemu 起不来或没有 /dev/fb0、MCP 掉线、agent 无法联网。
---

# qemu 语音闭环重启恢复

先探活，只重建死掉的部件。全量重建严格按步骤顺序：**固件 → HFP → qemu**，
HFP 必须在 qemu 启动前就位（`-allow-host-audio` 绑定启动时的默认录音源）。

```
WS=/home/zhangyi101/project26/contest2026_272_tokenwujixian_workspace
```

## 0. 探活

```bash
$WS/contest2026_272_tokenwujixian/.claude/skills/qemu-voice-loop-recovery/scripts/check_env.sh
```

MISS 行自带对应步骤号；全 MISS（刚重启、或工作区刚清过）则从步骤 1 顺序全跑。
每步的判定标准必须实际确认后再进下一步——不要凭"应该起来了"往下走。

## 1. 固件与构建产物（缺失时）

判定：`$OUT_DIR/nuttx`、`$OUT_DIR/vela_data.bin`、`ai_agent_main` 三者都在。
`$OUT_DIR` 由 `scripts/env.sh` 解析（`cmake_out/vela_goldfish-arm64-v8a-ap` 或
lunch 流程的 `out/openvela_vela_goldfish-arm64-v8a-ap`），不要写死其中一个。
后续步骤用到的 `$OUT_DIR` 这样取：

```bash
OUT_DIR=$(. $WS/contest2026_272_tokenwujixian/.claude/skills/qemu-voice-loop-recovery/scripts/env.sh; printf '%s' "$OUT_DIR")
```

缺任一项就重编（脚本即文档「完整可复现步骤」的固化版）：

```bash
$WS/contest2026_272_tokenwujixian/.claude/skills/qemu-voice-loop-recovery/scripts/rebuild_firmware.sh
# 需要改 Kconfig：--setconfig "MM_KASAN=n TESTING_KASAN=n"
# 需要全新构建：--fresh        （会重新生成 vela_data.bin）
# 只核对命令：  --dry-run
```

脚本展开的序列，手工执行时也照这个顺序：

```bash
cd $WS
export PATH="$WS/prebuilts/gcc/linux-x86_64/aarch64-none-elf/bin:$WS/prebuilts/build-tools/linux-x86_64/bin:$PATH"
export LD_LIBRARY_PATH="$WS/prebuilts/build-tools/linux-x86_64/lib:${LD_LIBRARY_PATH:-}"
./build.sh vendor/openvela/boards/vela/configs/goldfish-arm64-v8a-ap/ --cmake
# 改配置必须在 nuttx/ 里用 setconfig + olddefconfig（同一 kconfiglib 工具链），见脚本第 2 步
rm -f $OUT_DIR/.config.prev $OUT_DIR/include/nuttx/config.h
cmake --build $OUT_DIR -t rebuild_cache
cmake --build $OUT_DIR -j8
```

- `config.h` 必须由上面的 `rebuild_cache` 重新生成。只改 `.config` 不重建，改动不生效。
- 手工构建前先 `unset CROSSDEV`：交互 shell 里 `.bashrc` 导出的 `CROSSDEV=arm-none-eabi-`
  会被 `apps/external/ffmpeg/Makefile` 的 `--cross-prefix=$(CROSSDEV)` 直接采用，
  导致 ffmpeg 的 NEON 代码按 32 位 soft-float ABI 编译而失败。脚本已内置该 `unset`。
- 缺 `construct` / `pyelftools` / `cxxfilt` 时各阶段的 Python 脚本会报
  ModuleNotFoundError 或静默 `EINVAL(22)` 退出；脚本会自动补装。
- **全新构建会重写 `vela_data.bin`**：设备端 `set_llm`/`set_volc_key`/`set_remote*`/
  `mcp_add` 全部丢失，步骤 8 必须重跑。
- `emulator.sh` 的 `CONFIG_ARCH_CHIP_QEMU` 分支判断不到 `CONFIG_ARCH_CHIP_QEMU_ARM64`
  （qemu-arm64 板会静默秒退）。本闭环用 goldfish 板，走 `GOLDFISH_ARM64` 分支正常；
  换板时才需要绕过脚本直调 `qemu-system-aarch64`。

## 2. 停残留 + FAT 预检

```bash
pgrep -af "qemu-system|emulator.sh"        # 应为空；有残留先停掉对应任务
fsck.fat -n $OUT_DIR/vela_data.bin         # 无 error / 无大量 orphan cluster
```

有孤儿簇或接近满（`65467/65467 clusters`）：`fsck.fat -a <镜像>` + 删镜像目录下
`FSCK*.REC`，再复查。孤儿簇根源是 `kill -9` 强杀 qemu——永远不要那样做。

## 3. HFP 蓝牙耳麦（qemu 前必须）

```bash
systemctl status bluetooth ofono --no-pager   # 两个都应 active
bluetoothctl power on
bluetoothctl connect <耳机MAC>                # 值见实验计划书 §2
pactl list sources short                      # 应出现 bluez_source.*.handsfree_head_unit
pactl set-default-source bluez_source.<MAC>.handsfree_head_unit
# 采 3 秒验幅（对着耳机说话，戴耳/凑嘴 peak>3000 才算有效）：
parecord --raw -d <源编号> /tmp/t.pcm & sleep 3; kill %1; ls -la /tmp/t.pcm
```

连不上 → 实时语音降级为文件式 ASR 演示，其余场景不受影响。

## 4. mosquitto（本机 broker，127.0.0.1:1883）

```bash
pgrep -a mosquitto    # 预期: mosquitto -c /tmp/mosq_local.conf
# /tmp 被清则先重建配置：
[ -f /tmp/mosq_local.conf ] || printf 'listener 1883 127.0.0.1\nallow_anonymous true\n' > /tmp/mosq_local.conf
mosquitto -c /tmp/mosq_local.conf -d
```

## 5. mimo bridge（远程编码腿）

```bash
cd $WS/contest2026_272_tokenwujixian/tools/acp-bridge/
env PATH="$HOME/.mimocode/bin:$PATH" DESKMATE_MIMO_BIN="$HOME/.mimocode/bin/mimo" \
  DESKMATE_MQTT_USERNAME=deskmate DESKMATE_MQTT_PASSWORD=<MQTT_PW> \
  DESKMATE_PERMISSION_TIMEOUT_MS=300 \
  bin/deskmate-demo.sh bridge mimo mqtt --broker mqtt://127.0.0.1:1883 \
  --device-id goldfish-1 --mimo-attach --mimo-attach-port 4096
```

判定：`tail -5 /tmp/mimo_bridge.log` 无 `initialize_timeout`；mimo attach 窗口弹出。
`DESKMATE_PERMISSION_TIMEOUT_MS=300` 不可省——默认 30 s 审批窗口超时会断链并
wedge 设备 remote 通道（`set_remote` 都拉不起，只能整机重启）。

## 6. 双 MCP server + Gerrit worktree

```bash
# :8760 主 server（root=整个 workspace）
cd $WS && python3 packages/ai_agent/pc/remote_ctrl_mcp_server.py \
  --root . --host 0.0.0.0 --port 8760 --log-dir /tmp

# :8761 Gerrit 演示 server（root=worktree；/tmp 被清则先重建 worktree）
git -C ~/project26/vela-miwear-bes-o62-25rc05-tflm/apps \
  worktree add --detach /tmp/apps_hello_wt fa64b10b1cc8
#   自检: hello_main.c 含 add(3, 4)；remote vela 指向 gerrit.pt.mioffice.cn
cd $WS && python3 packages/ai_agent/pc/remote_ctrl_mcp_server.py \
  --root /tmp/apps_hello_wt --host 0.0.0.0 --port 8761 --log-dir /tmp
```

- Gerrit 侧 ssh **只许 push 不许 fetch**——worktree 只能本地重建，不能从 Gerrit 拉。
- `/tmp/hello_stub`（gcc 桩）与 `/tmp/gerrit_ps.py`（PS 号查询）被清则从 git 历史
  或既有提交信息重建；桩验证：`cd /tmp/apps_hello_wt && gcc -I/tmp/hello_stub
  examples/hello/hello_main.c -o /tmp/hello_out && /tmp/hello_out`。
- 预检：`curl -s -XPOST localhost:8760/mcp -d '{"jsonrpc":"2.0","id":1,"method":"tools/list"}'`
  返回工具 JSON。

## 7. qemu 窗口启动

```bash
cd $WS
rm -f /tmp/qin && mkfifo /tmp/qin
nohup sleep infinity > /tmp/qin &     # fifo 常驻写端：所有写端关闭→stdin EOF→qemu 秒退
bash prebuilts/emulator/tools/emulator.sh $OUT_DIR \
  -shell-serial mon:stdio -qemu -device virtio-gpu-device \
  < /tmp/qin > /tmp/qemu_stdio.log 2>&1 &
```

- 约 14 s 出 `goldfish-armv8a-ap>` 提示符；`-qemu -device virtio-gpu-device` 必须
  带（LVGL/fb0 需要）。
- `mount 19`、`10g resize`、`emu-crash.db opendir` 等警告全是噪音，忽略。
- 录制时另开展示终端：`gnome-terminal -- bash -c 'tail -n 0 -f /tmp/qemu_stdio.log; exec bash'`。

## 8. 设备端配置块（RAM + /data，按顺序）

直接起 agent：eth0 是 `ai_agent` 启动时 `ifup eth0` 才拉起来的，NSH 阶段网卡还没起，
此时 `ping 10.0.2.2` 只会得到 `sendto failed: 101`（ENETUNREACH）——那不是故障。

```bash
printf 'ai_agent\n' > /tmp/qin    # 等 vela> 提示符（日志里可见 ifup eth0...OK）
printf 'net_test\n'  > /tmp/qin   # 看到 Handshake OK + HTTP 200 才算网络就绪
```

`net_test` 不通才是真的网络问题（见坑表 NAT 竞态）。逐条送入配置（每条 ≥1 s，
等回显再下一条；占位值见实验计划书 §2）：

```
set_llm http://model.mify.ai.srv xiaomi/mimo-v2.5-pro <MIFY_KEY>
set_volc_key <VOLC_KEY>
set_remote_auth deskmate <MQTT_PW>
set_remote 10.0.2.2:1883 goldfish-1
mcp_add pc http://10.0.2.2:8760/mcp
mcp_add gerrit https://onedev.pt.miui.com/mcp-gw/gerrit_mcp <GERRIT_TOKEN>
mcp_add jira https://acc.pt.miui.com/jira-mcp/mcp https://jira-phone.mioffice.cn <JIRA_USER> <JIRA_PAT>
mcp_add apps http://10.0.2.2:8761/mcp
mcp_discover
show_chat
```

- `set_llm` 必须三参 `<host> <model> <key>`：`set_llm mimo <key>` 的内置模型名已过时
  （400 Unsupported model），`set_llm model <name>` 未实现（会把 `model` 当主机名）。
- 配置持久化在 `/data/agent/config`；`config_show` 可核对（key 脱敏）。

## 9. 验证探针

- `net_test` → `Handshake OK` + `HTTP Status: 200`（QEMU 软件加密慢，等 30–50 s）。
- `mcp_discover` 报 55 工具（pc 16 + gerrit 16 + jira 24 + apps 8）；jira PAT 吊销
  则 31 个，跳过 jira 场景。
- `rstat` → `State: Idle`、`Activity: mimo session ready`（bridge 在线）。
- 最小探针：`rask reply with the single word ready and nothing else` → `[Agent]: ready`。
- 屏幕出现聊天 UI（`show_chat`）。

## 坑表

| 症状 | 根因 | 处置 |
|------|------|------|
| qemu 启动即 `arm64_fatal.c:571` panic（连 ai_agent 都没跑到） | defconfig 自带 KASAN/SW_TAGS，与当前 QEMU 版本不兼容 | `rebuild_firmware.sh --setconfig "MM_KASAN=n TESTING_KASAN=n ARM64_MMU_ASSERT=n"` |
| `net_test` 失败、两个 host 同时 `ret=0x52`、`mcp_discover` 0 tools | QEMU user 模式 NAT(SLIRP) 启动竞态，非 MCP/密钥问题（NSH 阶段 ping 报 101 属正常，网卡尚未 up） | 停 qemu 重开（步骤 7），起来后直接 `ai_agent` + `net_test`，不要改配置 |
| TTS 报 -EIO 写失败 | /data FAT 满/孤儿簇 | 停 qemu → `fsck.fat -a` + 删 `FSCK*.REC` → 重启（步骤 2/7） |
| 编译在 ffmpeg 处报 `arm_neon.h: NEON intrinsics not available` / `int16x8_t` 未定义 | 宿主 `CROSSDEV=arm-none-eabi-` 被 ffmpeg Makefile 当成交叉前缀 | 见下方「CROSSDEV 污染清理」 |
| qemu 秒退 | fifo 写端全关（EOF） | 步骤 7 的 `nohup sleep infinity > /tmp/qin &` 必须常驻 |
| 实时语音零识别（其余全通） | 已知隔离在媒体图采集腿 | 演示降级为文件式 ASR；深挖需 nxrecorder 取证，不在恢复范围内 |
| rstat 长期 Offline | bridge 未起/挂 | `tail /tmp/mimo_bridge.log`；有 initialize_timeout → 重启 bridge（步骤 5） |
| rstat 偶发 Offline（闲置后） | MQTT 掉线（非 wedge） | 重发 `set_remote 10.0.2.2:1883 goldfish-1`，4 s 内回 Idle；拉不起才是 wedge → 整机重启 |
| agent 敲错命令后无响应 | cli 线程卡 | 等 10 s；仍卡 → 杀掉 qemu 进程重来（步骤 7），不要在 `vela>` 里敲 `quit`/`reboot` |
| rask 权限残留 | 上轮请求未清 | `rdeny` 提示"无可拒绝"= 已应答过，不堵通道 |
| ask 发出无 Sent 回显 | 文本 >256 字节 UTF-8 被 CLI 静默丢弃 | 拆短；详细步骤放 AGENT_TASK.txt 由 rask 引导 |
| 截屏全黑 | gnome-screenshot/import/scrot/PIL/dbus 全不可用 | 只用 `flameshot full -p /tmp/xx.png` |
| 工具调用报"无此工具" | 写了下划线名 `jira_get_issue` | agent 侧是点分格式，必须写 `jira.get_issue` |

CROSSDEV 污染的清理顺序（`-source_dirinfo.txt` 由 CMake 生成、没有构建规则，
误删会让 ninja 报 `missing and no known rule`）：

```bash
unset CROSSDEV
d=$OUT_DIR/apps/external/ffmpeg
rm -rf "$d/ffmpeg" "$d/ffbuild" "$d"/config.h "$d"/config.mak "$d"/config_components.h
rm -f "$d"/ffmpeg_out_tree-prefix/src/ffmpeg_out_tree-stamp/ffmpeg_out_tree-{download,configure,build,install}
```

之后重跑 `rebuild_firmware.sh`（脚本自己会 `unset CROSSDEV`）。清理后第一次
`ffmpeg_out_tree` 的 configure 步骤偶发因并行而失败（stderr 为空、out 里 configure
输出正常），直接重跑一次即可，已完成的编译目标不会重编。

## 密钥纪律

- 所有占位值（`<MIFY_KEY>`/`<VOLC_KEY>`/`<GERRIT_TOKEN>`/`<JIRA_USER>`/`<JIRA_PAT>`/
  `<MQTT_PW>`/耳机 MAC）在 `~/桌面/实验计划书_ai_agent全功能验证与录制_20260920.md` §2——
  本地文件，**严禁入仓/外发**。
- Mify key 执行时 shell 内替换：`$(tr -d '[:space:]' < ~/桌面/tmp/key.txt)`，
  不得出现在任何输出/日志/对话文本里。
- 同步会话日志入仓前必须先跑 `contest-log-redact` skill。
