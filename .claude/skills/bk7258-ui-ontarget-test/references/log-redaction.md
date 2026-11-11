# 会话日志脱敏入仓

本文件固化「选手仓 `logs/JUSTiceMaMo/*.jsonl` 入仓前的强制密钥脱敏」流程，已并入本 skill（不再单独成 skill）。脚本 `scripts/redact_log_secrets.py` 含密钥分片，**已 `.gitignore` 忽略，严禁入仓/外发/打包成 .skill 分发**，仅存本机（本目录是唯一权威版本，/tmp 下的旧副本可能过时）。

## Why

`logs/JUSTiceMaMo/*.jsonl` 是会话原始记录，命令文本里出现过的每个密钥都被原样留痕（`set_remote_auth`、jq 写 token、grep 密钥、拼接源码全算）。历史上 GitHub push protection 拦下过一次（旧提交已重写为打码版），且分片形式两次漏网进过公开历史。**脱敏只能保护未来副本；已进历史的只能换钥匙**（gerrit token / jira PAT 的换发优先级应提高）。

## 流程

### 1. 打码

```bash
cd <repo-root>   # contest2026_272_tokenwujixian/
python3 .claude/skills/bk7258-ui-ontarget-test/scripts/redact_log_secrets.py \
  logs/JUSTiceMaMo/<date>/*.jsonl
```

- 同 inode 写回（`open('w')`），正在追加写该文件的会话 fd 不断。
- 输出 JSON 报告每类替换计数（jira-pat / gerrit-mcp / volc-key / mify-key / emu-token / sudo-pw / mqtt-pw / github / mimo-gw-token + 各分片段）。
- Mify key 与 mimo 网关 token 自动从本机对应文件读入扫尾；文件缺失则对应模式静默跳过。

### 2. 复验（必须真实查看结果，不许 `|| echo CLEAN` 糊弄）

```bash
python3 .claude/skills/bk7258-ui-ontarget-test/scripts/redact_log_secrets.py \
  --verify logs/JUSTiceMaMo/<date>/*.jsonl
echo "exit=$?"
```

- 输出 `CLEAN` 且退出码 0 → 才可提交。
- 任何 `RESIDUAL` 行：定位该形态（在文件里 grep 残留串的上下文），往脚本 `SUBS` 加模式后重跑第 1、2 步。
- **可接受残留**：volc UUID 的 4 字符中段——头尾已抹、无法重组，不是漏网。

### 3. 提交

- commit message：`logs: sync claude-code session <会话id> (<主题>), secrets redacted`
- 只推 myfork（`git push myfork dev-ai-contest-2026`）。
- 选手仓 docs/ 下仅 `docs/zhangyi101/patches/` 可提交；`.claude/settings.local.json` 永不入仓。

## 分片密钥是漏网重灾区（两轮实测教训）

以下形式都能绕过整串正则，脚本已覆盖，但新形态还会出现：

- `'-'.join([...])` / `printf %s-%s` 拼接源码
- `G=<头7字符>` 变量拆分、`E='<前段>'+'<后段>'` 字面量相加
- 截断的 grep 输出（`<头7字符>...: 2 files`）

规则：

1. 任何打码后必须 `--verify` 为 CLEAN 才提交。
2. 发现新形态 → 在 `SUBS` 加模式；**用 `_A()` 分片组装，脚本文件内不出现完整密钥字面量**。
3. 写命令时引用密钥尽量避免完整字面量：从来源文件读（`python3 -c "json.load(...)"`）或分段拼接，从源头减少入日志的密钥量。

## 安全

- **本脚本含密钥分片：严禁入仓/外发/打包成 .skill 分发**，仅存本机（本仓 `.claude/skills/bk7258-ui-ontarget-test/scripts/redact_log_secrets.py`，已 `.gitignore` 忽略，永不提交/推送）。
- 桌面实验计划书含完整密钥，同样严禁入仓/外发。
