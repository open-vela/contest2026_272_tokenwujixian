#!/usr/bin/env bash

set -euo pipefail

usage() {
  echo "usage: $0 --workspace DIR --worktree DIR [--component cp|ap] [--jobs N] [--output DIR]" >&2
  exit 2
}

WORKSPACE=""
WORKTREE=""
COMPONENT="cp"
JOBS="12"
OUTPUT=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --workspace) WORKSPACE="$2"; shift 2 ;;
    --worktree) WORKTREE="$2"; shift 2 ;;
    --component) COMPONENT="$2"; shift 2 ;;
    --jobs) JOBS="$2"; shift 2 ;;
    --output) OUTPUT="$2"; shift 2 ;;
    -h|--help) usage ;;
    *) usage ;;
  esac
done

[[ -n "$WORKSPACE" && -n "$WORKTREE" ]] || usage
WORKSPACE="$(realpath "$WORKSPACE")"
WORKTREE="$(realpath "$WORKTREE")"
[[ "$COMPONENT" == "cp" || "$COMPONENT" == "ap" ]] || usage
[[ "$JOBS" =~ ^[1-9][0-9]*$ ]] || usage

[[ -f "$WORKSPACE/build.sh" && -d "$WORKSPACE/nuttx" ]] || {
  echo "error: workspace must contain build.sh and nuttx/: $WORKSPACE" >&2
  exit 1
}
[[ -d "$WORKTREE/chips/bk7258" && -d "$WORKTREE/board/bk7258-devkit" ]] || {
  echo "error: worktree lacks BK7258 chip/board directories: $WORKTREE" >&2
  exit 1
}
[[ -n "$OUTPUT" ]] || {
  echo "error: --output is required to prevent stale shared CMake state" >&2
  exit 1
}

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
exec "$SCRIPT_DIR/with_mapping.py" --workspace "$WORKSPACE" \
  --worktree "$WORKTREE" --component "$COMPONENT" --jobs "$JOBS" \
  --output "$OUTPUT"
