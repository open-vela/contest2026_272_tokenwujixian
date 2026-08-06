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
git apply /path/to/contest2026_272_tokenwujixian/docs/patches/vendor_openvela-qemu-arm64-v8a-ap-ai-agent.patch
```

应用后重新走一次完整编译流程即可（`--cmake` 生成配置 + `cmake --build`）。

**性质说明**：本 patch 未提交至 `open-vela/vendor_openvela` 官方仓库，仅作为团队内部保管、可复用的配置记录，存放于本专属仓库 `docs/patches/` 目录下。
