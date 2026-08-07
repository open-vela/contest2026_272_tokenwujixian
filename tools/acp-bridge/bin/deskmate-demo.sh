#!/usr/bin/env bash
set -euo pipefail

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
tool_dir=$(CDPATH= cd -- "$script_dir/.." && pwd)

usage() {
  cat <<'EOF'
Usage:
  deskmate-demo.sh broker [--port PORT]
  deskmate-demo.sh bridge <fake|mimo|kiro> <loop|mqtt> [options]
  deskmate-demo.sh client <loop|mqtt> [options]

Common options:
  --socket PATH          Unix socket for loop transport
  --broker URL           MQTT broker URL, for example mqtt://127.0.0.1:1883
  --topic-prefix PREFIX  MQTT topic prefix (default: deskmate)
  --device-id ID         MQTT device ID (default: demo-1)
  --mimo-attach          Open `mimo attach` after a MiMo bridge is ready (tmux or GNOME Terminal)
  --mimo-attach-port N   Explicit MiMo ACP/attach port (default: 4096)

Examples:
  bin/deskmate-demo.sh broker --port 18883
  bin/deskmate-demo.sh bridge mimo mqtt --broker mqtt://127.0.0.1:18883 --device-id goldfish-1
  bin/deskmate-demo.sh bridge mimo mqtt --broker mqtt://127.0.0.1:18883 --device-id goldfish-1 --mimo-attach
  bin/deskmate-demo.sh client mqtt --broker mqtt://127.0.0.1:18883 --device-id goldfish-1
  bin/deskmate-demo.sh bridge kiro loop --socket /tmp/deskmate-link.sock
  bin/deskmate-demo.sh client loop --socket /tmp/deskmate-link.sock

MQTT credentials are read only from DESKMATE_MQTT_USERNAME and
DESKMATE_MQTT_PASSWORD. Do not put credentials on the command line.
EOF
}

need_value() {
  if [ "$#" -lt 2 ] || [ -z "$2" ]; then
    printf '%s requires a value\n' "$1" >&2
    exit 2
  fi
}

parse_transport_options() {
  socket="${XDG_RUNTIME_DIR:+$XDG_RUNTIME_DIR/vela-deskmate/deskmate-link.sock}"
  socket="${socket:-/tmp/deskmate-link.sock}"
  broker="${DESKMATE_MQTT_BROKER:-mqtt://127.0.0.1:1883}"
  topic_prefix="${DESKMATE_MQTT_TOPIC_PREFIX:-deskmate}"
  device_id="${DESKMATE_DEVICE_ID:-demo-1}"
  mimo_attach=false
  mimo_attach_port="${DESKMATE_MIMO_ATTACH_PORT:-4096}"

  while [ "$#" -gt 0 ]; do
    case "$1" in
      --socket) need_value "$@"; socket="$2"; shift 2 ;;
      --broker) need_value "$@"; broker="$2"; shift 2 ;;
      --topic-prefix) need_value "$@"; topic_prefix="$2"; shift 2 ;;
      --device-id) need_value "$@"; device_id="$2"; shift 2 ;;
      --mimo-attach) mimo_attach=true; shift ;;
      --mimo-attach-port) need_value "$@"; mimo_attach_port="$2"; shift 2 ;;
      --help|-h) usage; exit 0 ;;
      *) printf 'unknown option: %s\n' "$1" >&2; usage >&2; exit 2 ;;
    esac
  done
}

valid_port() {
  case "$1" in
    ''|*[!0-9]*) return 1 ;;
    *) [ "$1" -ge 1 ] 2>/dev/null && [ "$1" -le 65535 ] 2>/dev/null ;;
  esac
}

valid_device_id() {
  case "$1" in
    ''|*[!A-Za-z0-9_-]*) return 1 ;;
    *) [ "${#1}" -le 32 ] ;;
  esac
}

read_attach_ready() {
  READY_FILE="$1" node --input-type=module -e '
    import { readFile } from "node:fs/promises";
    const file = process.env.READY_FILE;
    const value = JSON.parse(await readFile(file, "utf8"));
    const match = typeof value?.url === "string" && /^http:\/\/127\.0\.0\.1:([1-9][0-9]{0,4})$/.exec(value.url);
    if (value?.v !== 1 || !match || Number(match[1]) > 65535 ||
        typeof value.session_id !== "string" || !/^[A-Za-z0-9_-]{1,128}$/.test(value.session_id)) process.exit(2);
    process.stdout.write(`${value.url}\n${value.session_id}\n`);
  '
}

open_mimo_attach() {
  attach_url="$1"
  attach_session="$2"
  if [ -n "${TMUX:-}" ] && command -v tmux >/dev/null 2>&1; then
    printf -v attach_command 'mimo attach %q --session %q; exec %q' "$attach_url" "$attach_session" "${SHELL:-/bin/bash}"
    tmux new-window -n deskmate-mimo "$attach_command"
    printf 'opened tmux window deskmate-mimo for MiMo session %s\n' "$attach_session" >&2
  elif command -v gnome-terminal >/dev/null 2>&1 &&
       { [ -n "${DISPLAY:-}" ] || [ -n "${WAYLAND_DISPLAY:-}" ]; }; then
    if gnome-terminal --title='DeskMate MiMo' -- mimo attach "$attach_url" --session "$attach_session"; then
      printf 'opened GNOME Terminal for MiMo session %s\n' "$attach_session" >&2
    else
      printf 'could not open GNOME Terminal; run in another terminal:\n  mimo attach %q --session %q\n' "$attach_url" "$attach_session" >&2
    fi
  else
    printf 'MiMo attach is ready; run in another terminal:\n  mimo attach %q --session %q\n' "$attach_url" "$attach_session" >&2
  fi
}

shell_command() {
  local quoted=""
  local argument

  for argument in "$@"; do
    printf -v argument '%q' "$argument"
    quoted+="$argument "
  done
  printf '%s' "$quoted"
}

command_name="${1:-}"
case "$command_name" in
  broker)
    shift
    port="1883"
    while [ "$#" -gt 0 ]; do
      case "$1" in
        --port) need_value "$@"; port="$2"; shift 2 ;;
        --help|-h) usage; exit 0 ;;
        *) printf 'unknown broker option: %s\n' "$1" >&2; usage >&2; exit 2 ;;
      esac
    done
    exec mosquitto -p "$port" -v
    ;;
  bridge)
    backend="${2:-}"
    transport="${3:-}"
    [ "$#" -ge 3 ] || { usage >&2; exit 2; }
    case "$backend" in fake|mimo|kiro) ;; *) printf 'backend must be fake, mimo, or kiro\n' >&2; exit 2 ;; esac
    case "$transport" in loop|mqtt) ;; *) printf 'transport must be loop or mqtt\n' >&2; exit 2 ;; esac
    shift 3
    parse_transport_options "$@"
    if [ "$mimo_attach" = true ] && [ "$backend" != "mimo" ]; then
      printf '%s\n' '--mimo-attach requires backend mimo' >&2
      exit 2
    fi
    if [ "$mimo_attach" = true ] && ! valid_port "$mimo_attach_port"; then
      printf '%s\n' '--mimo-attach-port must be an integer from 1 through 65535' >&2
      exit 2
    fi
    if [ "$mimo_attach" = true ] && ! valid_device_id "$device_id"; then
      printf '%s\n' '--mimo-attach requires a device ID of up to 32 letters, digits, _ or -' >&2
      exit 2
    fi
    if [ "$transport" = "loop" ]; then
      bridge_command=(node "$tool_dir/bin/deskmate-bridge.mjs" --backend "$backend" --transport loop --socket "$socket")
    else
      bridge_command=(node "$tool_dir/bin/deskmate-bridge.mjs" --backend "$backend" --transport mqtt --broker "$broker" --topic-prefix "$topic_prefix" --device-id "$device_id")
    fi
    if [ "$mimo_attach" != true ]; then
      exec "${bridge_command[@]}"
    fi
    runtime_dir="${XDG_RUNTIME_DIR:-/tmp}/vela-deskmate"
    ready_file="$runtime_dir/mimo-attach-${device_id}.json"
    mkdir -p "$runtime_dir"
    chmod 700 "$runtime_dir"
    rm -f "$ready_file"
    bridge_command+=(--mimo-acp-port "$mimo_attach_port" --mimo-attach-ready-file "$ready_file")
    if [ -n "${TMUX:-}" ] && command -v tmux >/dev/null 2>&1; then
      bridge_shell_command=$(shell_command "${bridge_command[@]}")
      tmux new-window -d -n deskmate-bridge "$bridge_shell_command"
      attach_deadline=$((SECONDS + 30))
      while [ ! -f "$ready_file" ] && [ "$SECONDS" -lt "$attach_deadline" ]; do
        sleep 1
      done
      if [ ! -f "$ready_file" ]; then
        printf '%s\n' 'timed out waiting for MiMo ACP attach readiness' >&2
        exit 1
      fi
      mapfile -t attach_info < <(read_attach_ready "$ready_file") || {
        printf '%s\n' 'invalid MiMo ACP attach readiness file' >&2
        exit 1
      }
      open_mimo_attach "${attach_info[0]}" "${attach_info[1]}"
      printf '%s\n' 'bridge is running in tmux window deskmate-bridge' >&2
      exit 0
    fi
    "${bridge_command[@]}" &
    bridge_pid=$!
    cleanup_bridge() {
      kill "$bridge_pid" 2>/dev/null || true
      wait "$bridge_pid" 2>/dev/null || true
      rm -f "$ready_file"
    }
    trap 'cleanup_bridge; exit 130' INT TERM
    attach_deadline=$((SECONDS + 30))
    while [ ! -f "$ready_file" ] && [ "$SECONDS" -lt "$attach_deadline" ]; do
      if ! kill -0 "$bridge_pid" 2>/dev/null; then
        wait "$bridge_pid"
        exit $?
      fi
      sleep 1
    done
    if [ ! -f "$ready_file" ]; then
      printf '%s\n' 'timed out waiting for MiMo ACP attach readiness' >&2
      cleanup_bridge
      exit 1
    fi
    mapfile -t attach_info < <(read_attach_ready "$ready_file") || {
      printf '%s\n' 'invalid MiMo ACP attach readiness file' >&2
      cleanup_bridge
      exit 1
    }
    open_mimo_attach "${attach_info[0]}" "${attach_info[1]}"
    wait "$bridge_pid"
    bridge_status=$?
    rm -f "$ready_file"
    exit "$bridge_status"
    ;;
  client)
    transport="${2:-}"
    [ "$#" -ge 2 ] || { usage >&2; exit 2; }
    case "$transport" in loop|mqtt) ;; *) printf 'transport must be loop or mqtt\n' >&2; exit 2 ;; esac
    shift 2
    parse_transport_options "$@"
    if [ "$transport" = "loop" ]; then
      exec node "$tool_dir/bin/deskmate-client.mjs" --transport loop --socket "$socket"
    fi
    exec node "$tool_dir/bin/deskmate-client.mjs" --transport mqtt --broker "$broker" --topic-prefix "$topic_prefix" --device-id "$device_id"
    ;;
  --help|-h|help|"")
    usage
    ;;
  *)
    printf 'unknown command: %s\n' "$command_name" >&2
    usage >&2
    exit 2
    ;;
esac
