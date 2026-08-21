#!/usr/bin/env bash

set -euo pipefail
umask 077

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

usage() {
  cat >&2 <<EOF
usage: $0 --workspace DIR --worktree DIR [options]

options:
  --jobs N             build parallelism (default: 12)
  --slot NAME          stable incremental build/package slot name
  --fresh              rebuild this worktree's bound build slot
  --openvela-ap        build/package CP plus OpenVela AP
  --flash              flash the verified all-app.bin (destructive)
  --device PATH        serial device (default: /dev/ttyUSB0)
  --port N             bk_loader port number (default: 0)
EOF
  exit 2
}

WORKSPACE=""
WORKTREE=""
JOBS=12
SLOT=""
FRESH=0
OPENVELA_AP=0
FLASH=0
DEVICE=/dev/ttyUSB0
PORT=0
EXPECTED_LOADER_SHA256=55221d83d5582c362aab27d8883d799acf5eaa7b735d51135e01b0a24156b9ab

while [[ $# -gt 0 ]]; do
  case "$1" in
    --workspace) WORKSPACE="$2"; shift 2 ;;
    --worktree) WORKTREE="$2"; shift 2 ;;
    --jobs) JOBS="$2"; shift 2 ;;
    --slot) SLOT="$2"; shift 2 ;;
    --fresh) FRESH=1; shift ;;
    --openvela-ap) OPENVELA_AP=1; shift ;;
    --flash) FLASH=1; shift ;;
    --device) DEVICE="$2"; shift 2 ;;
    --port) PORT="$2"; shift 2 ;;
    -h|--help) usage ;;
    *) usage ;;
  esac
done

[[ -n "$WORKSPACE" && -n "$WORKTREE" ]] || usage
WORKSPACE="$(realpath "$WORKSPACE")"
WORKTREE="$(realpath "$WORKTREE")"
[[ "$JOBS" =~ ^[1-9][0-9]*$ ]] || usage

if [[ "$FLASH" -eq 1 ]]; then
  if [[ "$EUID" -eq 0 ]]; then
    echo "error: refusing to execute mutable worktree scripts as root" >&2
    echo "       grant your user serial-device access (for example, dialout)" >&2
    exit 1
  fi
  if [[ ! -t 0 || ! -t 1 || ! -r /dev/tty ]]; then
    echo "error: --flash requires a foreground interactive terminal" >&2
    echo "       do not run it in the background or redirect its output" >&2
    exit 1
  fi
fi

EVIDENCE_ROOT="$WORKSPACE/cmake_out"
mkdir -p "$EVIDENCE_ROOT"
EVIDENCE_ROOT="$(realpath "$EVIDENCE_ROOT")"
case "$EVIDENCE_ROOT" in "$WORKSPACE"/*) ;; *) echo "error: cmake_out escapes workspace" >&2; exit 1 ;; esac
[[ ! -L "$WORKSPACE/cmake_out" ]] || { echo "error: cmake_out must not be a symlink" >&2; exit 1; }

if [[ -z "$SLOT" ]]; then
  WORKTREE_NAME="$(basename "$WORKTREE" | tr -c 'A-Za-z0-9._-' '-')"
  WORKTREE_HASH="$(printf '%s' "$WORKTREE" | sha256sum | cut -c1-12)"
  SLOT="${WORKTREE_NAME}-${WORKTREE_HASH}"
fi
[[ "$SLOT" =~ ^[A-Za-z0-9][A-Za-z0-9._-]{0,79}$ ]] || {
  echo "error: invalid --slot; use 1-80 letters, digits, dot, underscore or dash" >&2
  exit 1
}

SLOTS_ROOT="$EVIDENCE_ROOT/bk7258-worktrees"
mkdir -p "$SLOTS_ROOT"
[[ ! -L "$SLOTS_ROOT" ]] || { echo "error: slots root must not be a symlink" >&2; exit 1; }
SLOT_ROOT="$SLOTS_ROOT/$SLOT"
if [[ -e "$SLOT_ROOT" ]]; then
  [[ -d "$SLOT_ROOT" && ! -L "$SLOT_ROOT" ]] || {
    echo "error: unsafe slot path: $SLOT_ROOT" >&2
    exit 1
  }
else
  mkdir "$SLOT_ROOT"
fi

BINDING="$SLOT_ROOT/.binding"
EXPECTED_BINDING="workspace=$WORKSPACE
worktree=$WORKTREE"
if [[ -e "$BINDING" ]]; then
  [[ -f "$BINDING" && ! -L "$BINDING" ]] || {
    echo "error: unsafe slot binding: $BINDING" >&2
    exit 1
  }
  [[ "$(cat "$BINDING")" == "$EXPECTED_BINDING" ]] || {
    echo "error: slot '$SLOT' belongs to another workspace/worktree" >&2
    exit 1
  }
else
  printf '%s\n' "$EXPECTED_BINDING" >"$BINDING"
  chmod 0400 "$BINDING"
fi

BUILD_ROOT="$SLOT_ROOT/build"
OUTPUT_DIR="$SLOT_ROOT/package"

CP_BUILD="$BUILD_ROOT/cp"
AP_BUILD="$BUILD_ROOT/ap"

reset_generated_dir() {
  local target="$1"
  case "$target" in "$SLOT_ROOT"/*) ;; *) echo "error: cleanup escapes bound slot" >&2; exit 1 ;; esac
  [[ ! -L "$target" ]] || { echo "error: refusing symlink cleanup: $target" >&2; exit 1; }
  rm -rf -- "$target"
}

CP_DEFCONFIG_SHA="$(sha256sum "$WORKTREE/board/bk7258-devkit/configs/cp/defconfig" | cut -d' ' -f1)"
CP_SHA_FILE="$SLOT_ROOT/.cp-defconfig.sha256"
if [[ "$FRESH" -eq 1 ]] || { [[ -f "$CP_SHA_FILE" ]] && [[ "$(cat "$CP_SHA_FILE")" != "$CP_DEFCONFIG_SHA" ]]; }; then
  reset_generated_dir "$CP_BUILD"
fi
if [[ ! -f "$CP_SHA_FILE" ]] || [[ "$(cat "$CP_SHA_FILE")" != "$CP_DEFCONFIG_SHA" ]]; then
  chmod 0600 "$CP_SHA_FILE" 2>/dev/null || true
  printf '%s\n' "$CP_DEFCONFIG_SHA" >"$CP_SHA_FILE"
  chmod 0400 "$CP_SHA_FILE"
fi

if [[ "$OPENVELA_AP" -eq 1 ]]; then
  AP_DEFCONFIG_SHA="$(sha256sum "$WORKTREE/board/bk7258-devkit/configs/ap/defconfig" | cut -d' ' -f1)"
  AP_SHA_FILE="$SLOT_ROOT/.ap-defconfig.sha256"
  if [[ "$FRESH" -eq 1 ]] || { [[ -f "$AP_SHA_FILE" ]] && [[ "$(cat "$AP_SHA_FILE")" != "$AP_DEFCONFIG_SHA" ]]; }; then
    reset_generated_dir "$AP_BUILD"
  fi
  if [[ ! -f "$AP_SHA_FILE" ]] || [[ "$(cat "$AP_SHA_FILE")" != "$AP_DEFCONFIG_SHA" ]]; then
    chmod 0600 "$AP_SHA_FILE" 2>/dev/null || true
    printf '%s\n' "$AP_DEFCONFIG_SHA" >"$AP_SHA_FILE"
    chmod 0400 "$AP_SHA_FILE"
  fi
fi

BUILD="$SCRIPT_DIR/build_worktree.sh"
PACKAGE="$WORKTREE/board/bk7258-devkit/tools/bk7258-package.sh"
FLASHER="$WORKTREE/board/bk7258-devkit/tools/bk7258-flash.sh"
LOADER="$WORKTREE/board/bk7258-devkit/tools/bk_loader"
NOTIFIER="$SCRIPT_DIR/notify_flash.py"

for executable in "$BUILD" "$PACKAGE" "$FLASHER" "$NOTIFIER"; do
  [[ -x "$executable" ]] || {
    echo "error: required executable is missing: $executable" >&2
    exit 1
  }
done

echo "== BK7258 CP build =="
"$BUILD" --workspace "$WORKSPACE" --worktree "$WORKTREE" \
  --component cp --jobs "$JOBS" --output "$CP_BUILD"

CP_DIR="$CP_BUILD/bk7258"
PACKAGE_ARGS=(
  --cp-app-bin "$CP_DIR/app.bin"
  --cp-validation-report "$CP_DIR/l1-validation.json"
)

if [[ "$OPENVELA_AP" -eq 1 ]]; then
  echo "== BK7258 AP build =="
  "$BUILD" --workspace "$WORKSPACE" --worktree "$WORKTREE" \
    --component ap --jobs "$JOBS" --output "$AP_BUILD"
  AP_DIR="$AP_BUILD/bk7258_ap"
  PACKAGE_ARGS+=(
    --openvela-ap
    --ap-app-bin "$AP_DIR/app1.bin"
    --ap-validation-report "$AP_DIR/l1-validation.json"
  )
fi

if [[ -e "$OUTPUT_DIR" ]]; then
  [[ -d "$OUTPUT_DIR" && ! -L "$OUTPUT_DIR" ]] || {
    echo "error: unsafe package slot: $OUTPUT_DIR" >&2
    exit 1
  }
else
  mkdir "$OUTPUT_DIR"
fi
PACKAGE_BINDING="$OUTPUT_DIR/.binding"
if [[ -e "$PACKAGE_BINDING" ]]; then
  [[ -f "$PACKAGE_BINDING" && ! -L "$PACKAGE_BINDING" && "$(cat "$PACKAGE_BINDING")" == "$EXPECTED_BINDING" ]] || {
    echo "error: package slot binding mismatch" >&2
    exit 1
  }
else
  printf '%s\n' "$EXPECTED_BINDING" >"$PACKAGE_BINDING"
  chmod 0400 "$PACKAGE_BINDING"
fi
chmod 0600 "$OUTPUT_DIR"/all-app.bin "$OUTPUT_DIR"/manifest.json "$OUTPUT_DIR"/decode-report.json 2>/dev/null || true
PACKAGE_ARGS+=(--output-dir "$OUTPUT_DIR" --overwrite)

echo "== BK7258 package + independent decode =="
"$PACKAGE" "${PACKAGE_ARGS[@]}"

for evidence in all-app.bin manifest.json decode-report.json; do
  path="$OUTPUT_DIR/$evidence"
  [[ -f "$path" && ! -L "$path" && "$(stat -c %U "$path")" == "$(id -un)" ]] || {
    echo "error: unsafe package evidence file: $path" >&2
    exit 1
  }
  chmod 0400 "$path"
done

IMAGE="$OUTPUT_DIR/all-app.bin"
echo "== BK7258 package integrity gate =="
"$FLASHER" --image "$IMAGE" --verify-only

if [[ "$FLASH" -eq 0 ]]; then
  echo
  echo "verified image: $IMAGE"
  echo "build evidence: $BUILD_ROOT"
  echo "flash was not requested; re-run with --flash in an interactive terminal"
  exit 0
fi

if [[ ! -c "$DEVICE" || -L "$DEVICE" ]]; then
  echo "error: serial device is not connected: $DEVICE" >&2
  exit 1
fi
if [[ ! -x "$LOADER" ]]; then
  echo "error: bundled loader is missing or not executable: $LOADER" >&2
  exit 1
fi
LOADER_SHA256="$(sha256sum "$LOADER" | cut -d' ' -f1)"
if [[ "$LOADER_SHA256" != "$EXPECTED_LOADER_SHA256" ]]; then
  echo "error: bundled bk_loader SHA-256 is not the audited version" >&2
  echo "       expected: $EXPECTED_LOADER_SHA256" >&2
  echo "       actual:   $LOADER_SHA256" >&2
  exit 1
fi

echo
echo "WARNING: flashing overwrites Bootloader, CP and AP from offset 0x0."
echo "During GetBus, the terminal will ring and ask for two manual resets."
IMAGE_SHA256="$(sha256sum "$IMAGE" | cut -d' ' -f1)"
echo "image SHA-256: $IMAGE_SHA256"
echo

FLASH_COMMAND=("$FLASHER" --image "$IMAGE" --device "$DEVICE" --port "$PORT")
exec "$NOTIFIER" -- "${FLASH_COMMAND[@]}"
