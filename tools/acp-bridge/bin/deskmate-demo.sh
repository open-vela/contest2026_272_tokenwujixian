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

Examples:
  bin/deskmate-demo.sh broker --port 18883
  bin/deskmate-demo.sh bridge mimo mqtt --broker mqtt://127.0.0.1:18883 --device-id goldfish-1
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

  while [ "$#" -gt 0 ]; do
    case "$1" in
      --socket) need_value "$@"; socket="$2"; shift 2 ;;
      --broker) need_value "$@"; broker="$2"; shift 2 ;;
      --topic-prefix) need_value "$@"; topic_prefix="$2"; shift 2 ;;
      --device-id) need_value "$@"; device_id="$2"; shift 2 ;;
      --help|-h) usage; exit 0 ;;
      *) printf 'unknown option: %s\n' "$1" >&2; usage >&2; exit 2 ;;
    esac
  done
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
    if [ "$transport" = "loop" ]; then
      exec node "$tool_dir/bin/deskmate-bridge.mjs" --backend "$backend" --transport loop --socket "$socket"
    fi
    exec node "$tool_dir/bin/deskmate-bridge.mjs" --backend "$backend" --transport mqtt --broker "$broker" --topic-prefix "$topic_prefix" --device-id "$device_id"
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
