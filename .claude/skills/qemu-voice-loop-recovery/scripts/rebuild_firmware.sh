#!/usr/bin/env bash
# Rebuild the goldfish qemu firmware (and its /data FAT image) from the
# workspace source tree, following the verified reproducible build:
#   export toolchain/host-tool PATH, ./build.sh <board>/ --cmake,
#   (setconfig + olddefconfig), force config.h regeneration, cmake --build.
#
# Use when cmake_out/vela_goldfish-arm64-v8a-ap/nuttx (or vela_data.bin) is
# missing, or when a Kconfig symbol must actually take effect. A clean build
# recreates vela_data.bin, so every device-side set_* from SKILL.md step 8 is
# lost afterwards.

set -euo pipefail
umask 077

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=env.sh
. "$SCRIPT_DIR/env.sh"

JOBS=8
FRESH=0
DRY_RUN=0
SETCONFIG_VARS=()
TARGET_OUT="$WS/cmake_out/vela_goldfish-arm64-v8a-ap"

usage() {
  cat >&2 <<EOF
usage: $0 [--jobs N] [--fresh] [--setconfig "SYM=y SYM2=n"] [--dry-run]

  --jobs N        build parallelism (default: 8)
  --fresh         delete the build dir first (regenerates vela_data.bin)
  --setconfig ... run setconfig + olddefconfig before rebuilding; this is the
                  only supported way to change CONFIG values (a bare
                  kconfig-tweak skips dependency resolution and desyncs config.h)
  --dry-run       print the commands without running them
EOF
  exit 2
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --jobs) JOBS="$2"; shift 2 ;;
    --fresh) FRESH=1; shift ;;
    --setconfig) read -r -a SETCONFIG_VARS <<<"$2"; shift 2 ;;
    --dry-run) DRY_RUN=1; shift ;;
    -h|--help) usage ;;
    *) usage ;;
  esac
done

[[ "$JOBS" =~ ^[1-9][0-9]*$ ]] || usage
[[ -d "$WS" ]] || { echo "error: workspace not found: $WS" >&2; exit 1; }

run() {
  printf '+ %s\n' "$*"
  [[ "$DRY_RUN" -eq 1 ]] || "$@"
}

cd "$WS"

# An interactive shell's CROSSDEV (e.g. arm-none-eabi- from .bashrc) overrides
# the board toolchain in apps/external/ffmpeg/Makefile (--cross-prefix=$(CROSSDEV)),
# so ffmpeg's NEON code is compiled for the 32-bit soft-float ABI and the build
# fails in arm_neon.h. Clear it before any target sees the environment.
unset CROSSDEV

export PATH="$GCC_BIN:$HOST_BIN:$PATH"
export LD_LIBRARY_PATH="$HOST_LIB:${LD_LIBRARY_PATH:-}"

# The build's Python helpers (kasan_global.py, mkallsyms.py) need these; a
# missing one shows up as ModuleNotFoundError or as mkallsyms.py silently
# exiting EINVAL(22). Best effort only: the interpreter that runs the build is
# not necessarily this shell's (pyenv/venv), so an install failure must not
# abort a build that may still work.
for mod in construct elftools cxxfilt; do
  python3 -c "import $mod" >/dev/null 2>&1 && continue
  pkg=$mod
  [[ "$mod" == elftools ]] && pkg=pyelftools
  printf '+ pip3 install %s\n' "$pkg"
  if [[ "$DRY_RUN" -eq 0 ]]; then
    pip3 install --user "$pkg" >/dev/null 2>&1 \
      || pip3 install "$pkg" >/dev/null 2>&1 \
      || echo "warning: could not install $pkg; install it manually if the" \
              "build fails in kasan_global.py or mkallsyms.py" >&2
  fi
done

if [[ "$FRESH" -eq 1 ]]; then
  case "$TARGET_OUT" in "$WS"/cmake_out/*) ;; *)
    echo "error: refusing to delete a path outside the workspace: $TARGET_OUT" >&2
    exit 1 ;;
  esac
  [[ ! -L "$TARGET_OUT" ]] || { echo "error: build dir is a symlink" >&2; exit 1; }
  run rm -rf "$TARGET_OUT"
fi

# 1. Configure + build. Also creates vela_data.bin.
run ./build.sh "$CONFIG_PATH/" --cmake

# 2. Optional Kconfig changes, resolved through the same kconfiglib toolchain
#    as the build (setconfig + olddefconfig), never kconfig-tweak alone.
if [[ ${#SETCONFIG_VARS[@]} -gt 0 ]]; then
  (
    cd "$WS/nuttx"
    export PATH="$WS/prebuilts/tools/python/bin:$PATH"
    export KCONFIG_CONFIG="$TARGET_OUT/.config"
    export EXTERNALDIR=dummy
    export APPSDIR="$WS/apps"
    export DRIVERS_PLATFORM_DIR=dummy
    export APPSBINDIR="$TARGET_OUT/apps"
    export BINDIR="$TARGET_OUT"
    run setconfig --kconfig Kconfig "${SETCONFIG_VARS[@]}"
    run olddefconfig
  )
fi

# 3. Force config.h to be regenerated from .config, then build.
run rm -f "$TARGET_OUT/.config.prev" "$TARGET_OUT/include/nuttx/config.h"
run cmake --build "$TARGET_OUT" -t rebuild_cache

run cmake --build "$TARGET_OUT" -j "$JOBS"

if [[ "$DRY_RUN" -eq 1 ]]; then
  exit 0
fi

# 4. Verify the artifacts the rest of the recovery flow depends on.
[[ -f "$TARGET_OUT/nuttx" ]] || {
  echo "error: build produced no kernel: $TARGET_OUT/nuttx" >&2
  exit 1
}
[[ -f "$TARGET_OUT/vela_data.bin" ]] || {
  echo "error: no /data image: $TARGET_OUT/vela_data.bin" >&2
  echo "       /data would fall back to tmpfs and drop every set_* on reboot" >&2
  exit 1
}
NM="$GCC_BIN/aarch64-none-elf-nm"
if [ -x "$NM" ]; then
  n=$("$NM" "$TARGET_OUT/nuttx" 2>/dev/null | grep -c ' ai_agent_main$' || true)
else
  n=$(grep -c ' ai_agent_main$' "$TARGET_OUT/System.map" || true)
fi
if [ "${n:-0}" -lt 1 ]; then
  echo "error: ai_agent is not linked into nuttx" >&2
  echo "       check CONFIG_EXAMPLES_AI_AGENT_VELA=y in $TARGET_OUT/.config" >&2
  exit 1
fi

echo
echo "firmware: $TARGET_OUT/nuttx ($(stat -c %s "$TARGET_OUT/nuttx") bytes)"
echo "data img: $TARGET_OUT/vela_data.bin"
echo "next: start qemu (SKILL.md step 7), then re-run the device configuration (step 8)"
