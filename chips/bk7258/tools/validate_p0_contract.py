#!/usr/bin/env python3
"""Validate the source-level BK7258 P0 contract without hardware access."""

from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[3]
CHIP = ROOT / "chips" / "bk7258"


def require(text: str, pattern: str, label: str) -> None:
    if re.search(pattern, text, re.MULTILINE) is None:
        raise SystemExit(f"missing {label}: {pattern}")


def main() -> int:
    memorymap = (CHIP / "include" / "bk7258_memorymap.h").read_text()
    irq = (CHIP / "include" / "irq.h").read_text()
    clock = (CHIP / "bk7258_clock.c").read_text()
    sysctrl = (CHIP / "bk7258_sysctrl.c").read_text()
    irq_source = (CHIP / "bk7258_irq.c").read_text()
    gpio = (CHIP / "bk7258_gpio.c").read_text()
    cp_defconfig = (ROOT / "board" / "bk7258-devkit" / "configs" / "cp" /
                    "defconfig").read_text()

    for name, bit in (("BK7258_SYS_MAC_CKEN", 26),
                      ("BK7258_SYS_PHY_CKEN", 27),
                      ("BK7258_SYS_WIFI_MAC_POWERDOWN", 9),
                      ("BK7258_SYS_WIFI_PHY_POWERDOWN", 10)):
        require(memorymap, rf"#define\s+{name}\s+\(UINT32_C\(1\) << {bit}\)", name)

    for name, source in (("BK7258_IRQ_MODEM", 29),
                         ("BK7258_IRQ_MODEM_RC", 30),
                         ("BK7258_IRQ_MAC_TXRX_TIMER", 31),
                         ("BK7258_IRQ_MAC_TXRX_MISC", 32),
                         ("BK7258_IRQ_MAC_WAKEUP", 38)):
        require(irq, rf"#define\s+{name}\s+\(16 \+ {source}\)", name)

    require(gpio, r"bk7258_gpio_wifi_mux\(26\)", "TXEN GPIO26 mux")
    require(gpio, r"bk7258_gpio_wifi_mux\(28\)", "RXEN GPIO28 mux")
    require(sysctrl, r"return -ENOTSUP", "unsupported reset result")
    for source, label in ((clock, "clock"), (sysctrl, "sysctrl"),
                          (irq_source, "IRQ"), (gpio, "GPIO")):
        require(source, r"enter_critical_section\(\)",
                f"{label} critical register update")

    if re.search(r"CONFIG_(?:BK7258_)?WIFI|CONFIG_WPA", cp_defconfig):
        raise SystemExit("CP defconfig unexpectedly enables Wi-Fi")

    print("BK7258 P0 source contract: PASS")
    print("hardware readback/IRQ delivery/GPIO waveform: target required")
    return 0


if __name__ == "__main__":
    sys.exit(main())
