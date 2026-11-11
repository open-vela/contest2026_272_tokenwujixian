#!/bin/bash
# Shared constants for the qemu voice-loop recovery scripts. Source only.
#
# The build output name depends on how the firmware was produced:
#   ./build.sh <board>/ --cmake     -> cmake_out/vela_goldfish-arm64-v8a-ap
#   lunch + build (openvela flow)   -> out/openvela_vela_goldfish-arm64-v8a-ap
# Never hardcode one of them; resolve whichever contains a built kernel.

WS=${WS:-/home/zhangyi101/project26/contest2026_272_tokenwujixian_workspace}
CONFIG_PATH=vendor/openvela/boards/vela/configs/goldfish-arm64-v8a-ap
EMULATOR=$WS/prebuilts/emulator/tools/emulator.sh
GCC_BIN=$WS/prebuilts/gcc/linux-x86_64/aarch64-none-elf/bin
HOST_BIN=$WS/prebuilts/build-tools/linux-x86_64/bin
HOST_LIB=$WS/prebuilts/build-tools/linux-x86_64/lib

# Prints the build output directory of a runnable image, or nothing.
resolve_out_dir() {
  local d
  for d in "$WS/cmake_out/vela_goldfish-arm64-v8a-ap" \
           "$WS/out/openvela_vela_goldfish-arm64-v8a-ap"; do
    if [ -f "$d/nuttx" ]; then
      printf '%s\n' "$d"
      return 0
    fi
  done
  return 1
}

OUT_DIR=$(resolve_out_dir || true)
