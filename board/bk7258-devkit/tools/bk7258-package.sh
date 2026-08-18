#!/usr/bin/env bash
# Package a built BK7258 CP component into a flashable L2 image.
#
# Wraps package_bk7258_bundled.py and then verifies the result with the
# independent decoder, printing only the key facts. Needs no Armino SDK.
#
# Usage: board/bk7258-devkit/tools/bk7258-package.sh [--cp-app-bin PATH]
#                                                    [--cp-validation-report PATH]
#                                                    [--openvela-ap]
#                                                    [--ap-app-bin PATH]
#                                                    [--ap-validation-report PATH]
#                                                    [--placeholder]
#                                                    [--output-dir DIR] [--overwrite]
#
# With no arguments this reads the CP component that build.sh produces for
# configs/cp and writes to a fixed output directory, replacing what was there,
# so bk7258-flash.sh finds the newest image without being handed a path.
#
# Pass --output-dir to keep a run as evidence. Such a directory is never
# replaced unless --overwrite is also given: an image that produced a UART log
# or a hardware observation has to stay exactly as it was flashed.

set -euo pipefail

TOOLS_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
BOARD_DIR="$(dirname -- "$TOOLS_DIR")"
OPENVELA_PROFILE="$BOARD_DIR/packaging/profiles/bk7258-devkit-openvela-ap.json"
PLACEHOLDER_PROFILE="$BOARD_DIR/packaging/profiles/bk7258-devkit-l0-bundled.json"
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

# build.sh derives its CMake binary directory as <board>_<config>, so configs/cp
# lands in cmake_out/bk7258-devkit_cp with no -b needed. Default to that, and to
# a matching fixed output directory, so the daily loop needs no paths at all.
CP_APP_BIN="$WORKSPACE/cmake_out/bk7258-devkit_cp/bk7258/app.bin"
CP_VALIDATION_REPORT="$WORKSPACE/cmake_out/bk7258-devkit_cp/bk7258/l1-validation.json"
AP_APP_BIN="$WORKSPACE/cmake_out/bk7258-devkit_ap/bk7258_ap/app1.bin"
AP_VALIDATION_REPORT="$WORKSPACE/cmake_out/bk7258-devkit_ap/bk7258_ap/l1-validation.json"
DEFAULT_OUTPUT_DIR="$WORKSPACE/cmake_out/bk7258-l2-bundled"
OPENVELA_OUTPUT_DIR="$WORKSPACE/cmake_out/bk7258-l2-openvela-ap"
OUTPUT_DIR=""
OVERWRITE=0
USE_PLACEHOLDER=1
AP_ARGUMENT_SEEN=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --cp-app-bin) CP_APP_BIN="$2"; shift 2 ;;
    --cp-validation-report) CP_VALIDATION_REPORT="$2"; shift 2 ;;
    --openvela-ap) USE_PLACEHOLDER=0; shift ;;
    --ap-app-bin) AP_APP_BIN="$2"; AP_ARGUMENT_SEEN=1; shift 2 ;;
    --ap-validation-report) AP_VALIDATION_REPORT="$2"; AP_ARGUMENT_SEEN=1; shift 2 ;;
    --placeholder) USE_PLACEHOLDER=1; shift ;;
    --output-dir) OUTPUT_DIR="$2"; shift 2 ;;
    --overwrite) OVERWRITE=1; shift ;;
    -h|--help) sed -n '2,15p' "${BASH_SOURCE[0]}"; exit 0 ;;
    *) echo "error: unknown argument: $1" >&2; exit 1 ;;
  esac
done

if [[ ! -f "$CP_APP_BIN" ]]; then
  echo "error: CP image not found: $CP_APP_BIN" >&2
  echo "build it first, from $WORKSPACE:" >&2
  echo "  ./build.sh vendor/beken/boards/bk7258/bk7258-devkit/configs/cp/ \\" >&2
  echo "    --cmake -j12" >&2
  exit 1
fi
if [[ ! -f "$CP_VALIDATION_REPORT" ]]; then
  echo "error: CP L1 validation report not found: $CP_VALIDATION_REPORT" >&2
  echo "rebuild configs/cp before packaging" >&2
  exit 1
fi
if [[ "$USE_PLACEHOLDER" -eq 1 && "$AP_ARGUMENT_SEEN" -eq 1 ]]; then
  echo "error: AP input arguments require --openvela-ap" >&2
  exit 1
fi

if [[ "$USE_PLACEHOLDER" -eq 0 && ! -f "$AP_APP_BIN" ]]; then
  echo "error: OpenVela AP image not found: $AP_APP_BIN" >&2
  echo "build it first, from $WORKSPACE:" >&2
  echo "  ./build.sh vendor/beken/boards/bk7258/bk7258-devkit/configs/ap/ --cmake -j12" >&2
  exit 1
fi
if [[ "$USE_PLACEHOLDER" -eq 0 && ! -f "$AP_VALIDATION_REPORT" ]]; then
  echo "error: OpenVela AP L1 validation report not found: $AP_VALIDATION_REPORT" >&2
  exit 1
fi

# The packer refuses to overwrite its own outputs, which is what protects an
# evidence directory. The fixed default directory is the opposite case: it is the
# scratch slot for the daily loop, so clear the previous package there instead of
# forcing a new directory on every run. Only the packager's own artifacts are
# removed, never the directory itself or anything else inside it.
if [[ -z "$OUTPUT_DIR" ]]; then
  if [[ "$USE_PLACEHOLDER" -eq 1 ]]; then
    OUTPUT_DIR="$DEFAULT_OUTPUT_DIR"
  else
    OUTPUT_DIR="$OPENVELA_OUTPUT_DIR"
  fi
  OVERWRITE=1
fi

if [[ "$OVERWRITE" -eq 1 && -d "$OUTPUT_DIR" ]]; then
  rm -f -- "$OUTPUT_DIR/all-app.bin" \
           "$OUTPUT_DIR/manifest.json" \
           "$OUTPUT_DIR/decode-report.json"

  # Loader logs describe the image that was there before, so they must not be
  # left beside a replacement image.
  find "$OUTPUT_DIR" -maxdepth 1 -name 'bk_loader-*.log' -delete 2>/dev/null || true
fi

PACKER_LOG="$(mktemp -t bk7258-package.XXXXXX)"
trap 'rm -f "$PACKER_LOG"' EXIT

PACKER_ARGS=(--profile "$PLACEHOLDER_PROFILE" --cp-app-bin "$CP_APP_BIN"
             --cp-validation-report "$CP_VALIDATION_REPORT"
             --output-dir "$OUTPUT_DIR")
AP_COMPARE="$AP_PLACEHOLDER"
if [[ "$USE_PLACEHOLDER" -eq 0 ]]; then
  PACKER_ARGS=(--profile "$OPENVELA_PROFILE" --cp-app-bin "$CP_APP_BIN"
               --cp-validation-report "$CP_VALIDATION_REPORT"
               --ap-app-bin "$AP_APP_BIN"
               --ap-validation-report "$AP_VALIDATION_REPORT"
               --output-dir "$OUTPUT_DIR")
  AP_COMPARE="$AP_APP_BIN"
fi

if ! python3 "$TOOLS_DIR/package_bk7258_bundled.py" \
  "${PACKER_ARGS[@]}" >"$PACKER_LOG" 2>&1; then
  cat "$PACKER_LOG" >&2
  exit 1
fi

if ! python3 "$TOOLS_DIR/decode_bk7258_image.py" \
  --image "$OUTPUT_DIR/all-app.bin" \
  --manifest "$OUTPUT_DIR/manifest.json" \
  --compare-cp "$CP_APP_BIN" \
  --compare-ap "$AP_COMPARE" \
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

# bk7258-flash.sh defaults to the same fixed directory, so the daily loop needs
# no path at all. Only an explicit evidence directory has to be named.
echo "flash it with:"
if [[ "$OUTPUT_DIR" == "$DEFAULT_OUTPUT_DIR" ]]; then
  echo "  sudo $TOOLS_DIR/bk7258-flash.sh"
else
  echo "  sudo $TOOLS_DIR/bk7258-flash.sh --image $OUTPUT_DIR/all-app.bin"
fi
