#!/usr/bin/env python3
"""Validate the dual-core OpenVela BK7258 AP ELF and app1.bin."""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path

import validate_bk7258_cp_image as elf


FLASH_BASE = 0x02160000
FLASH_SIZE = 0x002A0000
RAM_BASE = 0x28010000
RAM_SIZE = 0x00054000
RPMSG_BASE = 0x28064000
RPMSG_SIZE = 0x0000AC00
CP_RAM_BASE = 0x2806EC00
IDLE_STACK_SIZE = 2048
IMAGE_CONTRACT_OFFSET = 0x200
IMAGE_CONTRACT = (
    (0x4F564150).to_bytes(4, "little")
    + (1).to_bytes(4, "little")
    + (0x4F564131).to_bytes(4, "little")
    + FLASH_BASE.to_bytes(4, "little")
)


def fail(message: str) -> None:
    raise RuntimeError(message)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--elf", type=Path, required=True)
    parser.add_argument("--raw", type=Path, required=True)
    parser.add_argument("--config", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    args = parser.parse_args()

    try:
        config_text = args.config.read_text(encoding="utf-8")
        config_lines = config_text.splitlines()
        required_config = (
            "CONFIG_BK7258_COMPONENT_AP=y",
            "CONFIG_ARMV8M_DCACHE=y",
            "CONFIG_ARMV8M_ICACHE=y",
            "CONFIG_BOARD_LATE_INITIALIZE=y",
            "CONFIG_BOARDCTL=y",
            "CONFIG_BOARDCTL_RESET=y",
            "CONFIG_BUILTIN=y",
            "CONFIG_DEV_CONSOLE=y",
            "CONFIG_DEV_SIMPLE_ADDRENV=y",
            'CONFIG_INIT_ENTRYPOINT="nsh_main"',
            "CONFIG_INIT_STACKSIZE=4096",
            "CONFIG_NSH_CONSOLE=y",
            "CONFIG_NSH_BUILTIN_APPS=y",
            "CONFIG_RAM_START=0x28010000",
            "CONFIG_RAM_SIZE=344064",
            "CONFIG_BK7258_RPMSG_SHM_ADDR=0x28064000",
            "CONFIG_BK7258_RPMSG_SHM_SIZE=0xac00",
            "CONFIG_BK7258_MAILBOX=y",
            'CONFIG_RPMSG_LOCAL_CPUNAME="ap"',
            "CONFIG_RPMSG_PING=y",
            "CONFIG_RPMSG_UART=y",
            "CONFIG_RPMSG_UART_CONSOLE=y",
            "CONFIG_RPTUN=y",
            "CONFIG_RPTUN_AUTO_RESET_DISABLE=y",
            "CONFIG_RPTUN_PRIORITY=90",
            "CONFIG_RPTUN_STACKSIZE=4096",
            "CONFIG_SERIAL=y",
            "CONFIG_SMP=y",
            "CONFIG_SMP_NCPUS=2",
            "CONFIG_SYSTEM_NSH=y",
        )
        for setting in required_config:
            if setting not in config_lines:
                fail(f"AP build config lacks required setting: {setting}")
        forbidden_config = (
            "CONFIG_BK7258_COMPONENT_CP=y",
        )
        for setting in forbidden_config:
            if setting in config_lines:
                fail(f"AP build config enables forbidden setting: {setting}")

        elf_data, sections, segments, entry = elf.parse_elf(args.elf)
        symbols = elf.symbol_values(elf_data, sections)
        text = elf.section_by_name(sections, ".text")
        data = elf.section_by_name(sections, ".data")
        bss = elf.section_by_name(sections, ".bss")
        required = (
            "_vectors",
            "start",
            "__start",
            "nsh_main",
            "nsh_consolemain",
            "g_bk7258_ap_image_contract",
            "bk7258_rptun_initialize",
            "bk7258_mbox_init",
            "bk7258_mbox_notify",
            "bk7258_mbox_ipi",
            "board_reset",
            "up_systemreset",
            "rpmsg_serialinit",
            "uart_rpmsg_init",
            "up_enable_icache",
            "up_disable_dcache",
            "up_cpu_index",
            "up_cpu_start",
            "up_cpu_idlestack",
            "up_send_smp_sched",
            "up_send_smp_call",
            "up_get_intstackbase",
            "_vectors_core1",
            "g_bk7258_cpu2_boot_stack",
            "_sdata",
            "_edata",
            "_sbss",
            "_ebss",
            "_enoinit",
            "_eronly",
        )
        for name in required:
            if name not in symbols:
                fail(f"ELF lacks required symbol {name}")

        flash_end = FLASH_BASE + FLASH_SIZE
        ram_end = RAM_BASE + RAM_SIZE
        if ram_end != RPMSG_BASE or RPMSG_BASE + RPMSG_SIZE != CP_RAM_BASE:
            fail("compiled AP/RPMsg/CP constants are not contiguous")
        if text.addr != FLASH_BASE or symbols["_vectors"] != FLASH_BASE:
            fail("AP vector table is not at 0x02160000")
        if symbols["_vectors"] % 512:
            fail("AP vector table is not 512-byte aligned")
        if not (FLASH_BASE <= (entry & ~1) < flash_end) or not entry & 1:
            fail(f"ELF entry 0x{entry:08x} is not a Thumb address in AP XIP")
        if (entry & ~1) != (symbols["__start"] & ~1):
            fail("ELF entry does not resolve to OpenVela AP __start")
        if not (FLASH_BASE <= symbols["nsh_main"] < flash_end):
            fail("nsh_main is outside AP XIP")
        if symbols["g_bk7258_ap_image_contract"] != FLASH_BASE + IMAGE_CONTRACT_OFFSET:
            fail("OpenVela AP image contract is not at fixed XIP offset 0x200")
        vectors_core1 = symbols["_vectors_core1"]
        if not (FLASH_BASE <= vectors_core1 < flash_end):
            fail("AP CPU2 vector table is outside AP XIP")
        if vectors_core1 % 512:
            fail("AP CPU2 vector table is not 512-byte aligned")
        if not (RAM_BASE <= symbols["g_bk7258_cpu2_boot_stack"] < ram_end):
            fail("AP CPU2 boot stack is outside the locked RAM window")
        if data.addr != RAM_BASE or symbols["_sdata"] != RAM_BASE:
            fail("AP .data does not begin at 0x28010000")
        if not (RAM_BASE <= symbols["_sdata"] <= symbols["_edata"] <= ram_end):
            fail("AP .data is outside the locked RAM window")
        if not (RAM_BASE <= symbols["_sbss"] <= symbols["_ebss"] <= ram_end):
            fail("AP .bss is outside the locked RAM window")
        if not (RAM_BASE <= symbols["_enoinit"] <= symbols["_sbss"]):
            fail("AP .noinit does not end below AP .bss")
        if symbols["_ebss"] + IDLE_STACK_SIZE > ram_end:
            fail("AP idle stack extends beyond the locked RAM window")
        if not (FLASH_BASE <= symbols["_eronly"] < flash_end):
            fail("AP .data load address is outside AP XIP")

        loads = [s for s in segments if s.segment_type == elf.PT_LOAD and s.vaddr == data.addr]
        if len(loads) != 1 or loads[0].paddr != symbols["_eronly"]:
            fail("AP .data PT_LOAD address does not match _eronly")

        raw = args.raw.read_bytes()
        if len(raw) < 8 or len(raw) > FLASH_SIZE:
            fail("app1.bin length is outside AP raw capacity")
        msp = int.from_bytes(raw[:4], "little")
        reset = int.from_bytes(raw[4:8], "little")
        if not (RAM_BASE <= msp <= ram_end) or msp % 8:
            fail(f"AP MSP 0x{msp:08x} is outside/aligned incorrectly")
        if msp != symbols["_ebss"] + IDLE_STACK_SIZE:
            fail("AP raw MSP does not match the reserved idle stack top")
        if not reset & 1 or not (FLASH_BASE <= (reset & ~1) < flash_end):
            fail(f"AP reset vector 0x{reset:08x} is invalid")
        if (reset & ~1) != (symbols["start"] & ~1):
            fail("AP reset vector does not target the OpenVela start trampoline")
        if raw[IMAGE_CONTRACT_OFFSET : IMAGE_CONTRACT_OFFSET + len(IMAGE_CONTRACT)] != IMAGE_CONTRACT:
            fail("app1.bin lacks the fixed OpenVela AP image contract")

        flash_loads = [
            segment
            for segment in segments
            if segment.segment_type == elf.PT_LOAD
            and segment.filesz > 0
            and FLASH_BASE <= segment.paddr < flash_end
        ]
        if not flash_loads:
            fail("AP ELF has no loadable AP XIP segments")
        expected_end = max(segment.paddr + segment.filesz for segment in flash_loads)
        expected_raw = bytearray(expected_end - FLASH_BASE)
        for segment in flash_loads:
            start_offset = segment.paddr - FLASH_BASE
            expected_raw[start_offset : start_offset + segment.filesz] = elf_data[
                segment.offset : segment.offset + segment.filesz
            ]
        if raw != bytes(expected_raw):
            fail("app1.bin does not exactly match the AP ELF PT_LOAD image")

        report = {
            "result": "pass",
            "component": "openvela-ap-dual-core-smp",
            "elf": str(args.elf),
            "raw": str(args.raw),
            "sha256": hashlib.sha256(raw).hexdigest(),
            "elf_sha256": hashlib.sha256(elf_data).hexdigest(),
            "config": str(args.config.resolve()),
            "config_sha256": hashlib.sha256(config_text.encode()).hexdigest(),
            "raw_length_bytes": len(raw),
            "flash": {"start": "0x02160000", "end": "0x02400000"},
            "ram": {"start": "0x28010000", "end": "0x28064000"},
            "rpmsg_shm": {"start": "0x28064000", "end": "0x2806ec00"},
            "vectors": {"msp": f"0x{msp:08x}", "reset": f"0x{reset:08x}"},
            "cpu2_vectors": {
                "base": f"0x{vectors_core1:08x}",
                "msp": f"0x{symbols['g_bk7258_cpu2_boot_stack'] + 2048:08x}",
            },
            "sections": {
                "text": f"0x{text.addr:08x}",
                "data": f"0x{data.addr:08x}",
                "bss": f"0x{bss.addr:08x}",
            },
        }
        args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print(json.dumps(report, indent=2))
        return 0
    except (IndexError, OSError, RuntimeError, ValueError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
