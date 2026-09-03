# Patch 使用说明

## vendor_openvela-qemu-arm64-v8a-ap-ai-agent.patch

**用途**：让 `qemu-arm64-v8a-ap` 板卡默认配置支持编译并稳定运行 AI Agent。

**改动内容**：
- 关闭 `CONFIG_MM_KASAN`（内核地址消毒器）：与本地 QEMU 10.0.0 版本存在兼容性问题，开启会导致固件启动即崩溃
- 关闭 `CONFIG_TESTING_KASAN`：依赖 KASAN，随上一项一起关闭
- 开启 `CONFIG_NETUTILS_CJSON`：AI Agent 依赖 cJSON 库解析/构造 JSON，原配置未包含
- 开启 `CONFIG_SYSTEM_POPEN`：AI Agent 部分工具（如截屏）依赖 `popen`/`pclose`
- 开启 `CONFIG_EXAMPLES_AI_AGENT_VELA` 及 `CONFIG_EXAMPLES_AI_AGENT_VELA_SHELL_ALLOWLIST`：启用 AI Agent 主开关，Shell 工具使用白名单安全模式

> 注：`CONFIG_ARM64_MMU_ASSERT` 由 `DEBUG_FEATURES` 自动带出（`default y if DEBUG_FEATURES`），defconfig 精简格式下无需显式写 `# CONFIG_ARM64_MMU_ASSERT is not set` 即可保持关闭状态，因此本 patch 未包含该行。

**应用方法**（在 openvela 工作区的 `vendor/openvela/boards/vela` 目录下）：

```bash
cd vendor/openvela/boards/vela
git apply /path/to/contest2026_272_tokenwujixian/docs/zhangyi101/patches/vendor_openvela-qemu-arm64-v8a-ap-ai-agent.patch
```

应用后重新走一次完整编译流程即可（`--cmake` 生成配置 + `cmake --build`）。

**性质说明**：本 patch 未提交至 `open-vela/vendor_openvela` 官方仓库，仅作为团队内部保管、可复用的配置记录，存放于本专属仓库 `docs/zhangyi101/patches/` 目录下。

## packages_ai_agent-mcp-client-x-user-token.patch

**用途**：让 AI Agent 的 MCP 客户端能通过「MCP 统一网关」（`onedev.pt.miui.com/mcp-gw`）接入 Gerrit 等 HTTP MCP 服务。

**背景**：MCP 客户端 `packages/ai_agent/src/tools/mcp_client.c` 原先把认证 token 以 `************** ****** <token>` 形式放进标准 `************** ******` header；而 MCP 统一网关要求把**裸 token** 放进自定义 header `x-user-token`。两者不一致会导致 `mcp_discover` 被网关 401 拒绝。

**改动内容**：
- `mcp_http_post()` 中，token 改为通过 `x-user-token` header 发送（裸值，不加 `************** ****** ` 前缀）
- 顺带消除了原实现里「有 token / 无 token」两条重复的 `vela_*_post_json` 调用路径，合并为单一路径（`srv->token` 生命周期覆盖整个 POST 调用，无需堆分配）

**前置条件**：`CONFIG_AI_AGENT_MCP` 在 `qemu-arm64-v8a-ap` defconfig 下由 Kconfig 默认值（`default y`）自动带出，无需额外开启；`mcp_add` / `mcp_discover` / `mcp_status` 命令随 `CONFIG_EXAMPLES_AI_AGENT_VELA`（见上一个 patch）一同编入。

**应用方法**（在 openvela 工作区根目录下）：

```bash
cd packages/ai_agent
git apply /path/to/contest2026_272_tokenwujixian/docs/zhangyi101/patches/packages_ai_agent-mcp-client-x-user-token.patch
```

应用后重新编译 `qemu-arm64-v8a-ap` 即可。接入步骤：

```
vela> mcp_add gerrit https://onedev.pt.miui.com/mcp-gw/gerrit_mcp mcp_u-你的Token值
vela> mcp_discover
vela> mcp_status
```

> ⚠️ 网络可达性：`onedev.pt.miui.com` 为小米内网域名，QEMU user 模式 NAT 默认走公网，需确认宿主机已接入内网/VPN，并按需为 QEMU 配置内网 DNS 或宿主机转发代理。

**性质说明**：本 patch 未提交至 `open-vela/packages_ai_agent` 官方仓库，仅作为团队内部保管、可复用的改动记录，存放于本专属仓库 `docs/zhangyi101/patches/` 目录下。

## nuttx-audio-close-shutdown-outside-spinlock.patch

**用途**：修复 `audio_close()` 在持有自旋锁时调用 lower-half `shutdown()` 导致的潜在死锁。

**背景**：NuttX 通用音频设备框架 `audio/audio.c` 的 `audio_close()` 原实现在 `spin_lock_irqsave(&upper->spinlock)` 保护区内调用 `lower->ops->shutdown()`。而 lower-half 的 `shutdown()` 可能同步回传 buffer（经上层回调），从而**阻塞**；在自旋锁内阻塞会导致死锁（BK7258 音频驱动关闭 `/dev/audio/pcm0p` 时即可能触发）。

**改动内容**：
- 在自旋锁内仅记录「是否最后一次关闭」`last_close = (upper->head == NULL)`，随后立即 `spin_unlock_irqrestore`
- 将 `kmm_free(priv)` 与 `lower->ops->shutdown()` 移出自旋锁保护区，在锁外执行

**应用方法**（在 openvela 工作区的 `nuttx` 目录下）：

```bash
cd nuttx
git apply /path/to/contest2026_272_tokenwujixian/docs/zhangyi101/patches/nuttx-audio-close-shutdown-outside-spinlock.patch
```

**性质说明**：本 patch 未提交至 `open-vela/nuttx` 官方仓库，仅作为团队内部保管、可复用的改动记录，存放于本专属仓库 `docs/zhangyi101/patches/` 目录下。属通用框架修复，理想情况应 upstream 至 `apache/nuttx`。

## nuttx-input-button-lower-enodev-no-buttons.patch

**用途**：让 `btn_lower_initialize()` 在板子无按键（`board_button_initialize()` 返回 0）时返回 `-ENODEV`，而不是注册一个空设备。

**改动内容**：
- 增加 `#include <errno.h>`
- `btn_lower_initialize()` 中，当 `g_btnnum == 0` 时直接返回 `-ENODEV`，避免误注册空 `/dev/btnN`

> 注：BK7258 DevKit 当前有 3 个按键（`board_button_initialize()` 返回 `NUM_BUTTONS=3`），该分支在本板上**不会触发**；仅对无按键的板子/组件是通用加固。

**应用方法**（在 openvela 工作区的 `nuttx` 目录下）：

```bash
cd nuttx
git apply /path/to/contest2026_272_tokenwujixian/docs/zhangyi101/patches/nuttx-input-button-lower-enodev-no-buttons.patch
```

**性质说明**：本 patch 未提交至 `open-vela/nuttx` 官方仓库，仅作为团队内部保管、可复用的改动记录，存放于本专属仓库 `docs/zhangyi101/patches/` 目录下。
