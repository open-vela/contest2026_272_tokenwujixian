#!/usr/bin/env python3
"""Run one command while securely mapping BK7258 sources into a workspace."""

from __future__ import annotations

import argparse
import fcntl
import os
from pathlib import Path
import signal
import stat
import subprocess
import sys
import uuid


def owned_private_dir(path: Path, label: str) -> None:
    info = path.stat()
    if not stat.S_ISDIR(info.st_mode) or info.st_uid != os.getuid():
        raise RuntimeError(f"{label} must be a directory owned by the current user: {path}")
    if info.st_mode & stat.S_IWOTH:
        raise RuntimeError(f"{label} must not be world writable: {path}")


def contained(child: Path, parent: Path, label: str) -> None:
    try:
        child.relative_to(parent)
    except ValueError as exc:
        raise RuntimeError(f"{label} escapes approved root {parent}: {child}") from exc


def atomic_symlink(link: Path, target: str) -> None:
    temporary = link.with_name(f".{link.name}.bk7258-{uuid.uuid4().hex}")
    os.symlink(target, temporary)
    os.replace(temporary, link)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--workspace", required=True, type=Path)
    parser.add_argument("--worktree", required=True, type=Path)
    parser.add_argument("--component", choices=("cp", "ap"), required=True)
    parser.add_argument("--jobs", type=int, default=12)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    if args.jobs < 1 or args.jobs > 256:
        parser.error("--jobs must be between 1 and 256")

    workspace = args.workspace.resolve(strict=True)
    worktree = args.worktree.resolve(strict=True)
    owned_private_dir(workspace, "workspace")
    owned_private_dir(worktree, "worktree")
    build_script = (workspace / "build.sh").resolve(strict=True)
    contained(build_script, workspace, "workspace build.sh")
    build_info = build_script.stat()
    if not stat.S_ISREG(build_info.st_mode) or build_info.st_uid != os.getuid():
        raise RuntimeError(f"workspace build.sh must be a current-user-owned file: {build_script}")
    if build_info.st_mode & stat.S_IWOTH:
        raise RuntimeError(f"workspace build.sh must not be world writable: {build_script}")

    output = args.output.resolve(strict=False)
    output_root = (workspace / "cmake_out/bk7258-worktrees").resolve(strict=True)
    contained(output, output_root, "build output")

    git_root = subprocess.run(
        ["git", "-C", str(worktree), "rev-parse", "--show-toplevel"],
        check=True,
        text=True,
        capture_output=True,
    ).stdout.strip()
    if Path(git_root).resolve() != worktree:
        raise RuntimeError(f"not a Git worktree root: {worktree}")

    chip_source = (worktree / "chips/bk7258").resolve(strict=True)
    board_source = (worktree / "board/bk7258-devkit").resolve(strict=True)
    contained(chip_source, worktree, "chip source")
    contained(board_source, worktree, "board source")

    chip_link = workspace / "vendor/beken/chips/bk7258"
    board_link = workspace / "vendor/beken/boards/bk7258/bk7258-devkit"
    for link in (chip_link, board_link):
        owned_private_dir(link.parent.resolve(strict=True), "mapping parent")
        if not link.is_symlink():
            raise RuntimeError(f"expected manifest mapping symlink: {link}")

    lock_path = workspace / ".bk7258-worktree-build.lock"
    flags = os.O_CREAT | os.O_RDWR | os.O_CLOEXEC
    if hasattr(os, "O_NOFOLLOW"):
        flags |= os.O_NOFOLLOW
    lock_fd = os.open(lock_path, flags, 0o600)
    try:
        lock_info = os.fstat(lock_fd)
        if not stat.S_ISREG(lock_info.st_mode) or lock_info.st_uid != os.getuid():
            raise RuntimeError(f"unsafe lock file: {lock_path}")
        os.fchmod(lock_fd, 0o600)
        fcntl.flock(lock_fd, fcntl.LOCK_EX)

        old_chip = chip_link.resolve(strict=True)
        old_board = board_link.resolve(strict=True)
        contained(old_chip, workspace, "existing chip mapping")
        contained(old_board, workspace, "existing board mapping")
        old_chip_text = str(old_chip)
        old_board_text = str(old_board)
        atomic_symlink(chip_link, str(chip_source))
        try:
            atomic_symlink(board_link, str(board_source))
        except BaseException:
            atomic_symlink(chip_link, old_chip_text)
            raise

        child: subprocess.Popen[bytes] | None = None

        def terminate(signum: int, _frame: object) -> None:
            if child is not None and child.poll() is None:
                os.killpg(child.pid, signum)

        previous = {
            sig: signal.signal(sig, terminate)
            for sig in (signal.SIGINT, signal.SIGTERM, signal.SIGHUP)
        }
        try:
            command = [
                str(build_script),
                f"vendor/beken/boards/bk7258/bk7258-devkit/configs/{args.component}/",
                "--cmake",
                "-b",
                str(output),
                f"-j{args.jobs}",
            ]
            child = subprocess.Popen(command, cwd=workspace, start_new_session=True)
            return child.wait()
        finally:
            for sig, handler in previous.items():
                signal.signal(sig, handler)
            atomic_symlink(board_link, old_board_text)
            atomic_symlink(chip_link, old_chip_text)
            if chip_link.resolve(strict=True) != old_chip or board_link.resolve(strict=True) != old_board:
                raise RuntimeError("failed to restore BK7258 manifest mappings")
    finally:
        os.close(lock_fd)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, subprocess.CalledProcessError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        raise SystemExit(1)
