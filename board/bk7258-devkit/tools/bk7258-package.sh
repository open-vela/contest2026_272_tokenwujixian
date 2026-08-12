#!/usr/bin/env bash
# Package a built BK7258 CP component into a flashable L2 image.
#
# Wraps package_bk7258_bundled.py and then verifies the result with the
# independent decoder, printing only the key facts. Needs no Armino SDK.
#
# Usage: board/bk7258-devkit/tools/bk7258-package.sh [--cp-app-bin PATH] [--output-dir DIR]

set -euo pipefail

TOOLS_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
BOARD_DIR="$(dirname -- "$TOOLS_DIR")"
PROFILE="$BOARD_DIR/packaging/profiles/bk7258-devkit-l0-bundled.json"
AP_PLACEHOLDER="$BOARD_DIR/packaging/bundled/ap-placeholder.bin"

# The board directory lives two levels below the team repository root, which is
# itself one level below the OpenVela workspace. Locate the workspace by its
# markers instead of counting directories, so worktrees and symlinked board
# paths resolve correctly.
find_workspace_root() {
  local candidate="$1"
  while [[ "$candidate" != "/" ]]; do
    if [[ -f "$candidate/build.sh" && -d "$candidate/nuttx" ]]; then
      printf '%s\n' "$candidate"
      return 0
    fi
    candidate="$(dirname -- "$candidate")"
  done
  return 1
}

if ! WORKSPACE="$(find_workspace_root "$BOARD_DIR")"; then
  echo "error: no OpenVela workspace (build.sh + nuttx/) found above $BOARD_DIR" >&2
  exit 1
fi

CP_APP_BIN="$WORKSPACE/cmake_out/bk7258-devkit_cp/bk7258/app.bin"
OUTPUT_DIR=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --cp-app-bin) CP_APP_BIN="$2"; shift 2 ;;
    --output-dir) OUTPUT_DIR="$2"; shift 2 ;;
    -h|--help) sed -n '2,8p' "${BASH_SOURCE[0]}"; exit 0 ;;
    *) echo "error: unknown argument: $1" >&2; exit 1 ;;
  esac
done

if [[ ! -f "$CP_APP_BIN" ]]; then
  echo "error: CP image not found: $CP_APP_BIN" >&2
  echo "build it first, from $WORKSPACE:" >&2
  echo "  ./build.sh vendor/beken/boards/bk7258/bk7258-devkit/configs/cp/ \\" >&2
  echo "    --cmake -b cmake_out/bk7258-devkit_cp/ -j12" >&2
  exit 1
fi

if [[ -z "$OUTPUT_DIR" ]]; then
  OUTPUT_DIR="$WORKSPACE/cmake_out/bk7258-l2-bundled-$(date +%Y%m%d-%H%M%S)"
fi

PACKER_LOG="$(mktemp -t bk7258-package.XXXXXX)"
trap 'rm -f "$PACKER_LOG"' EXIT

if ! python3 "$TOOLS_DIR/package_bk7258_bundled.py" \
  --profile "$PROFILE" \
  --cp-app-bin "$CP_APP_BIN" \
  --output-dir "$OUTPUT_DIR" >"$PACKER_LOG" 2>&1; then
  cat "$PACKER_LOG" >&2
  exit 1
fi

if ! python3 "$TOOLS_DIR/decode_bk7258_image.py" \
  --image "$OUTPUT_DIR/all-app.bin" \
  --manifest "$OUTPUT_DIR/manifest.json" \
  --compare-cp "$CP_APP_BIN" \
  --compare-ap "$AP_PLACEHOLDER" \
  --report "$OUTPUT_DIR/decode-report.json" >/dev/null; then
  echo "error: independent decode failed; the image must not be flashed" >&2
  exit 1
fi

python3 - "$OUTPUT_DIR" <<'PY'
import json, sys
from pathlib import Path

out = Path(sys.argv[1])
manifest = json.loads((out / "manifest.json").read_text(encoding="utf-8"))
report = json.loads((out / "decode-report.json").read_text(encoding="utf-8"))

def line(label, entry):
    print(f"  {label:<11}{entry['raw_length_bytes']:>9} B  {entry['sha256'][:16]}…  "
          f"{entry['physical_offset']}  {entry['source']}")

print(f"profile      {manifest['profile']}")
print("components")
for label in ("bootloader", "cp", "ap"):
    line(label, manifest["components"][label])
image = manifest["image"]
print(f"image        {image['length_bytes']:>9} B  {image['sha256']}")
print(f"decode       {report['result']}")
print(f"output       {out}")
PY

echo
echo "flash it with:"
echo "  sudo $TOOLS_DIR/bk7258-flash.sh --image $OUTPUT_DIR/all-app.bin"
