#!/usr/bin/env bash
# Flash a packaged BK7258 L2 image over the Beken downloader.
#
# Writes the complete image from physical Flash offset 0x0, streaming the
# loader's full output. Needs root for the USB serial device.
#
# Usage: sudo board/bk7258-devkit/tools/bk7258-flash.sh --image PATH [--port 0] [--device /dev/ttyUSB0]

set -euo pipefail

TOOLS_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
BOARD_DIR="$(dirname -- "$TOOLS_DIR")"
LOADER="$TOOLS_DIR/bk_loader"

IMAGE=""
PORT=0
DEVICE=/dev/ttyUSB0
BAUD=1500000

while [[ $# -gt 0 ]]; do
  case "$1" in
    --image) IMAGE="$2"; shift 2 ;;
    --port) PORT="$2"; shift 2 ;;
    --device) DEVICE="$2"; shift 2 ;;
    -h|--help) sed -n '2,7p' "${BASH_SOURCE[0]}"; exit 0 ;;
    *) echo "error: unknown argument: $1" >&2; exit 1 ;;
  esac
done

if [[ -z "$IMAGE" ]]; then
  echo "error: --image is required" >&2
  exit 1
fi
if [[ ! -f "$IMAGE" ]]; then
  echo "error: image not found: $IMAGE" >&2
  exit 1
fi
if [[ ! -x "$LOADER" ]]; then
  echo "error: bundled loader is missing or not executable: $LOADER" >&2
  exit 1
fi
if [[ ! -e "$DEVICE" ]]; then
  echo "error: $DEVICE does not exist; connect the DevKit and put it in the" >&2
  echo "       downloader-accessible state" >&2
  exit 1
fi
if [[ $EUID -ne 0 ]]; then
  echo "error: run this with sudo; the downloader needs the serial device" >&2
  exit 1
fi

# A serial monitor holding the port makes the loader fail at GetBus, after the
# board has already been reset. Catch it before touching Flash.
if command -v fuser >/dev/null 2>&1 && fuser "$DEVICE" >/dev/null 2>&1; then
  echo "error: $DEVICE is busy; close minicom or any other serial monitor first" >&2
  exit 1
fi

# The decode report is the packaging gate. Refuse to flash an image that was
# never independently verified.
IMAGE_DIR="$(cd -- "$(dirname -- "$IMAGE")" && pwd)"
REPORT="$IMAGE_DIR/decode-report.json"
if [[ ! -f "$REPORT" ]]; then
  echo "error: no decode-report.json next to the image; package it with" >&2
  echo "       bk7258-package.sh so the image is independently verified" >&2
  exit 1
fi
if ! grep -q '"result": "pass"' "$REPORT"; then
  echo "error: $REPORT does not report a passing decode" >&2
  exit 1
fi

echo "image    $IMAGE"
echo "sha256   $(sha256sum "$IMAGE" | cut -d' ' -f1)"
echo "bytes    $(stat -c%s "$IMAGE")"
echo "target   $DEVICE (loader port $PORT) at $BAUD"
echo
echo "This overwrites the Bootloader, CP and AP areas from physical offset 0x0."
echo

# bk_loader writes its own log to a Windows-style relative path, which on Linux
# becomes a literal backslash filename: "tools\bk_loader_COM<n>.log". Observed
# beside the board directory when invoked by a relative path; search the current
# directory too in case the loader resolves it against the working directory.
LOG_SEARCH_DIRS=("$BOARD_DIR")
if [[ "$PWD" != "$BOARD_DIR" ]]; then
  LOG_SEARCH_DIRS+=("$PWD")
fi

find_loader_logs() {
  find "${LOG_SEARCH_DIRS[@]}" -maxdepth 1 -name '*bk_loader_COM*.log' "$@" 2>/dev/null
}

# Clear any earlier log so the completion gate can only see this run.
find_loader_logs -delete || true

# No separate read-only handshake: `download` does its own do_reset_signal and
# GetBus, and a failure there happens before Begin EraseFlash, so Flash stays
# untouched either way.
#
# The loader runs straight to the terminal rather than through a pipe: piping it
# would switch its stdout to block buffering and the erase/write progress would
# only appear at the end. Its own log file is what the gate reads afterwards.
echo "== writing Flash =="
"$LOADER" download -p "$PORT" -b "$BAUD" -s 0 -i "$IMAGE" || true

FLASH_LOG=""
while IFS= read -r -d '' log; do
  FLASH_LOG="$IMAGE_DIR/bk_loader-$(date +%Y%m%d-%H%M%S).log"
  mv -- "$log" "$FLASH_LOG"
done < <(find_loader_logs -print0)

echo
if [[ -z "$FLASH_LOG" ]]; then
  echo "error: the loader wrote no log in ${LOG_SEARCH_DIRS[*]}, so this script" >&2
  echo "       cannot confirm the write finished. Look for 'Writing Flash OK' in" >&2
  echo "       the output above before trusting what is on Flash." >&2
  exit 1
fi

if ! grep -q 'Writing Flash OK' "$FLASH_LOG"; then
  echo "error: the loader never reported 'Writing Flash OK'." >&2
  if grep -qE 'LinkCheck Timeout|GetBus fail' "$FLASH_LOG"; then
    echo "       It failed at the bus handshake, before the image was parsed or" >&2
    echo "       Flash was erased, so Flash is unchanged. Check the USB cable and" >&2
    echo "       the downloader boot mode." >&2
  else
    echo "       It got past the bus handshake, so Flash may be partially written." >&2
    echo "       Re-flash before power-cycling, and keep the vendor recovery image" >&2
    echo "       available." >&2
  fi
  echo "       Full log: $FLASH_LOG" >&2
  exit 1
fi

echo "log      $FLASH_LOG"

echo
echo "flashed. open the console with:"
echo "  sudo minicom -o -D $DEVICE -b 115200"
echo "in minicom, turn hardware and software flow control off (Ctrl-A O)."
