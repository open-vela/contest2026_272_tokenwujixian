#!/usr/bin/env python3
"""Run a flash command on a PTY and drive the BK7258 reset automatically.

The wrapped command is bk7258-flash.sh, which execs bk_loader.  Once the
loader reports `connect success`, this wrapper opens the same serial device
as a second writer and injects `reboot bootloader` into the running NSH
console.  Target firmware carrying the bootloader-reset support
(feature/bk7258-bootloader-reset, board_reset ENTER_BOOTLOADER) then
performs an AON-WDT chip-level reset with the vendor warm-jump tag cleared
and the reset reason recorded as watchdog, so the BootROM runs the cold
chain and BL2 opens the download listen window that the already-polling
loader catches by itself -- no manual reset.

Fallback: if the automatic reset does not engage (firmware without the
patch, a hung console, an occupied target), the wrapper retries once and
then falls back to the historical behavior -- a bell and a highlighted
prompt ask the operator to press the reset button during the GetBus
windows.

The injector is write-only.  It never reads the device (a second reader
would steal the loader's handshake bytes) and never touches the termios
(the loader configured them).  After `Gotten Bus...` the loader may switch
the line to a different baud rate, so no further writes are ever issued.
"""

from __future__ import annotations

import os
import pty
import select
import signal
import sys
import time


RESET_MARKER = b"Getting Bus..."
SUCCESS_MARKER = b"Gotten Bus..."
CONNECT_MARKER = b"connect success"

REBOOT_CMD = b"reboot bootloader\r\n"
RETRY_AFTER = 4.0   # seconds after the first injection before one retry
MANUAL_AFTER = 8.0  # seconds after the first injection before the manual fallback


def banner(message: str, *, bell: bool = False) -> None:
    prefix = "\a" if bell else ""
    text = f"\r\n\033[1;33m{'=' * 68}\r\n{message}\r\n{'=' * 68}\033[0m\r\n"
    os.write(sys.stdout.fileno(), (prefix + text).encode())


def child_serial_device(command: list[str]) -> str:
    """Serial device the wrapped bk_loader talks on: the wrapped
    bk7258-flash.sh --device value, else /dev/ttyUSB<--port>, else
    /dev/ttyUSB0."""

    device = None
    port = "0"
    for i, arg in enumerate(command):
        if arg == "--device" and i + 1 < len(command):
            device = command[i + 1]
        elif arg == "--port" and i + 1 < len(command):
            port = command[i + 1]
    return device if device is not None else f"/dev/ttyUSB{port}"


def inject_reboot_bootloader(device: str) -> bool:
    try:
        fd = os.open(device, os.O_WRONLY | os.O_NOCTTY | os.O_NONBLOCK)
    except OSError as exc:
        banner(f"[BK7258] 自动复位注入失败（无法打开 {device}）：{exc}")
        return False
    try:
        os.write(fd, b"\r\n")
        time.sleep(0.15)
        os.write(fd, REBOOT_CMD)
        return True
    except OSError as exc:
        banner(f"[BK7258] 自动复位注入失败：{exc}")
        return False
    finally:
        os.close(fd)


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

    device = child_serial_device(command)

    child, master = pty.fork()
    if child == 0:
        if sys.platform.startswith("linux"):
            import ctypes

            libc = ctypes.CDLL(None)
            libc.prctl(1, signal.SIGTERM)
        os.execvp(command[0], command)

    pending = b""
    reset_count = 0
    got_bus = False
    injected = False
    injected_at = 0.0
    retried = False
    manual_prompted = False
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
            readable, _, _ = select.select([master], [], [], 0.1)

            if master not in readable:
                now = time.monotonic()
                if injected and not got_bus:
                    if not retried and now - injected_at >= RETRY_AFTER:
                        retried = True
                        if inject_reboot_bootloader(device):
                            banner("[BK7258] 自动复位重试已发送")
                    elif not manual_prompted and now - injected_at >= MANUAL_AFTER:
                        manual_prompted = True
                        banner(
                            "[BK7258] 自动复位未生效（固件可能不含 bootloader 复位补丁）；"
                            "GetBus 期间请手动按复位键",
                            bell=True,
                        )
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

            if not injected and CONNECT_MARKER in pending:
                injected = True
                injected_at = time.monotonic()
                if inject_reboot_bootloader(device):
                    banner(
                        "[BK7258] downloader 就绪：已自动注入 reboot bootloader"
                        "（软件复位进下载模式，无需人工）"
                    )

            while SUCCESS_MARKER in pending:
                _, pending = pending.split(SUCCESS_MARKER, 1)
                got_bus = True
                banner("[BK7258] GetBus 握手成功（零人工），等待烧录完成")

            while RESET_MARKER in pending:
                _, pending = pending.split(RESET_MARKER, 1)
                reset_count += 1
                if manual_prompted:
                    banner(
                        f"[BK7258] 第 {reset_count} 次 GetBus 握手：请现在手动按一下复位键",
                        bell=True,
                    )
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
