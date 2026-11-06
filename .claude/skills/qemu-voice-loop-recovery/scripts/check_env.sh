#!/bin/bash
# Probe the qemu voice-loop demo environment. Read-only, no side effects.
# One line per component; "MISS" lines map to the same-numbered step in SKILL.md.

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=env.sh
. "$SCRIPT_DIR/env.sh"

ok()   { printf '  OK   %s\n' "$1"; }
miss() { printf '  MISS %s\n' "$1"; }
warn() { printf '  WARN %s\n' "$1"; }

echo "== firmware / build output (step 1) =="
if [ -n "$OUT_DIR" ]; then
  ok "build dir: $OUT_DIR"
  [ -f "$OUT_DIR/vela_data.bin" ] && ok "data image vela_data.bin" \
    || miss "vela_data.bin  -> step 1 (rebuild; /data will be tmpfs until then)"
  if [ -x "$GCC_BIN/aarch64-none-elf-nm" ]; then
    n=$("$GCC_BIN/aarch64-none-elf-nm" "$OUT_DIR/nuttx" 2>/dev/null \
        | grep -c ' ai_agent_main$' || true)
  else
    n=$(grep -c ' ai_agent_main$' "$OUT_DIR/System.map" 2>/dev/null || true)
  fi
  if [ "${n:-0}" -ge 1 ]; then
    ok "ai_agent built into nuttx"
  else
    warn "ai_agent_main not found in nuttx -- image may predate the agent"
  fi
  printf '  ..   built: %s\n' "$(date -r "$OUT_DIR/nuttx" '+%F %T' 2>/dev/null)"
else
  miss "no built goldfish image  -> step 1 ($SCRIPT_DIR/rebuild_firmware.sh)"
fi

echo "== host services =="
pgrep -x mosquitto >/dev/null && ok "mosquitto broker" \
  || miss "mosquitto broker  -> step 4"
pgrep -f deskmate-bridge >/dev/null && ok "mimo bridge" \
  || miss "mimo bridge  -> step 5 (needs DESKMATE_PERMISSION_TIMEOUT_MS=300)"
n=$(pgrep -fc remote_ctrl_mcp_server 2>/dev/null || true)
[ "${n:-0}" -ge 1 ] && ok "rc MCP server process(es): $n" \
  || miss "rc MCP server  -> step 6"
for p in 8760 8761; do
  curl -s -m 2 -XPOST localhost:$p/mcp \
       -d '{"jsonrpc":"2.0","id":1,"method":"tools/list"}' \
    | head -c 200 | grep -q '"tools"' && ok "MCP :$p responds" \
    || miss "MCP :$p responds  -> step 6"
done

echo "== bluetooth HFP (must be up BEFORE qemu starts) =="
src=$(pactl list sources short 2>/dev/null | grep bluez_source | head -1 | cut -f2)
if [ -n "$src" ]; then
  ok "HFP source: $src"
  [ "$(pactl get-default-source 2>/dev/null)" = "$src" ] \
    && ok "HFP is default source" \
    || miss "HFP is NOT default source: pactl set-default-source $src"
else
  miss "HFP source  -> step 3 (bluetoothctl power on; connect <MAC>)"
fi

echo "== qemu emulator =="
pgrep -f "qemu-system|emulator.sh" >/dev/null && ok "qemu process" \
  || miss "qemu process  -> step 7"
[ -p /tmp/qin ] && ok "console fifo /tmp/qin" || miss "console fifo  -> step 7"
pgrep -f "sleep infinity" >/dev/null && ok "fifo resident writer (sleep infinity)" \
  || miss "fifo resident writer  -> step 7 (without it qemu dies on stdin EOF)"
if [ -f /tmp/qemu_stdio.log ]; then
  grep -q 'goldfish-armv8a-ap>' /tmp/qemu_stdio.log \
    && ok "boot reached the NSH prompt" || warn "no NSH prompt in the log yet"
  grep -q 'vela>' /tmp/qemu_stdio.log \
    && ok "agent CLI prompt vela>" || miss "agent CLI not started  -> step 8"
  grep -q 'Network timeout' /tmp/qemu_stdio.log \
    && warn "NAT race: 'Network timeout' -- restart qemu (step 7), not a config bug"
  grep -qE 'recursive assert|Assertion failed panic' /tmp/qemu_stdio.log \
    && warn "kernel panic seen -- see the KASAN row in SKILL.md"
  printf '  ..   console tail: %s\n' "$(tail -1 /tmp/qemu_stdio.log | head -c 90)"
else
  miss "/tmp/qemu_stdio.log (no run yet, or /tmp was wiped)"
fi

echo "== gerrit demo worktree + helpers (all under /tmp, wiped on reboot) =="
[ -f /tmp/apps_hello_wt/examples/hello/hello_main.c ] \
  && ok "/tmp/apps_hello_wt (Gerrit worktree)" \
  || miss "/tmp/apps_hello_wt  -> step 6 (rebuild from tflm checkout)"
[ -f /tmp/gerrit_ps.py ] && ok "/tmp/gerrit_ps.py (PS number probe)" \
  || miss "/tmp/gerrit_ps.py  -> step 6"
[ -d /tmp/hello_stub ] && ok "/tmp/hello_stub (gcc -I stub for hello_main.c)" \
  || miss "/tmp/hello_stub  -> step 6"

echo "== FAT health (manual deep check) =="
if [ -n "$OUT_DIR" ]; then
  echo "  run only when TTS reports -EIO:"
  echo "    fsck.fat -n $OUT_DIR/vela_data.bin"
  echo "    # errors/orphans -> fsck.fat -a <img> && rm <img-dir>/FSCK*.REC"
else
  echo "  no vela_data.bin to check; rebuild first (step 1)"
fi
