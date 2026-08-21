#!/usr/bin/env python3
"""Run a flash command on a PTY and highlight BK7258 reset windows."""

from __future__ import annotations

import os
import pty
import select
import signal
import sys


RESET_MARKER = b"Getting Bus..."
SUCCESS_MARKER = b"Gotten Bus..."


def banner(message: str, *, bell: bool = False) -> None:
    prefix = "\a" if bell else ""
    text = f"\r\n\033[1;33m{'=' * 68}\r\n{message}\r\n{'=' * 68}\033[0m\r\n"
    os.write(sys.stdout.fileno(), (prefix + text).encode())


def main() -> int:
    command = sys.argv[1:]
    if command[:1] == ["--"]:
        command = command[1:]
    if not command:
        print(f"usage: {sys.argv[0]} -- COMMAND [ARG ...]", file=sys.stderr)
        return 2

    if not os.isatty(sys.stdin.fileno()) or not os.isatty(sys.stdout.fileno()):
        print("error: flashing notifier requires an interactive terminal", file=sys.stderr)
        return 2

    child, master = pty.fork()
    if child == 0:
        if sys.platform.startswith("linux"):
            import ctypes

            libc = ctypes.CDLL(None)
            libc.prctl(1, signal.SIGTERM)
        os.execvp(command[0], command)

    pending = b""
    reset_count = 0
    received_signal: int | None = None

    def forward(signum: int, _frame: object) -> None:
        nonlocal received_signal
        received_signal = signum
        try:
            os.killpg(child, signum)
        except ProcessLookupError:
            pass

    previous = {
        sig: signal.signal(sig, forward)
        for sig in (signal.SIGINT, signal.SIGTERM, signal.SIGHUP)
    }
    try:
        while True:
            readable, _, _ = select.select([master], [], [], 0.25)
            if master not in readable:
                finished, status = os.waitpid(child, os.WNOHANG)
                if finished:
                    return os.waitstatus_to_exitcode(status)
                continue

            try:
                chunk = os.read(master, 4096)
            except OSError:
                chunk = b""
            if not chunk:
                _, status = os.waitpid(child, 0)
                return os.waitstatus_to_exitcode(status)

            os.write(sys.stdout.fileno(), chunk)
            pending = (pending + chunk)[-8192:]

            while RESET_MARKER in pending:
                _, pending = pending.split(RESET_MARKER, 1)
                reset_count += 1
                banner(
                    f"[BK7258] 第 {reset_count} 次 GetBus 握手：请现在手动按一下复位键",
                    bell=True,
                )

            while SUCCESS_MARKER in pending:
                _, pending = pending.split(SUCCESS_MARKER, 1)
                banner("[BK7258] GetBus 握手成功，停止复位，等待烧录完成")
    finally:
        for sig, handler in previous.items():
            signal.signal(sig, handler)
        if received_signal is not None:
            try:
                os.killpg(child, signal.SIGTERM)
            except ProcessLookupError:
                pass
        os.close(master)


if __name__ == "__main__":
    raise SystemExit(main())
