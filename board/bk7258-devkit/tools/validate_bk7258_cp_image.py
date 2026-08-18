#!/usr/bin/env python3
"""Validate the locked BK7258 CP L1 ELF and raw component image.

The validator uses only the Python standard library. It is a build-time gate:
it validates the ELF/raw component before the L2 packaging step, never touches
hardware, and never invokes the downloader.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
from dataclasses import dataclass
from pathlib import Path


ELF_MAGIC = b"\x7fELF"
ELFCLASS32 = 1
ELFDATA2LSB = 1
SHT_SYMTAB = 2
PT_LOAD = 1


@dataclass(frozen=True)
class Section:
    name: str
    addr: int
    offset: int
    size: int
    link: int
    entsize: int
    section_type: int


@dataclass(frozen=True)
class Segment:
    offset: int
    vaddr: int
    paddr: int
    filesz: int
    memsz: int
    segment_type: int


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Validate a BK7258 CP secure L1 ELF/raw image pair."
    )
    parser.add_argument("--profile", type=Path, required=True)
    parser.add_argument("--elf", type=Path, required=True)
    parser.add_argument("--raw", type=Path, required=True)
    parser.add_argument("--config", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    return parser.parse_args()


def fail(message: str) -> None:
    raise RuntimeError(message)


def cstring(data: bytes, offset: int) -> str:
    end = data.find(b"\x00", offset)
    if end < 0:
        fail("unterminated ELF string table entry")
    return data[offset:end].decode("ascii", errors="strict")


def parse_elf(path: Path) -> tuple[bytes, list[Section], list[Segment], int]:
    data = path.read_bytes()
    if data[:4] != ELF_MAGIC or data[4] != ELFCLASS32 or data[5] != ELFDATA2LSB:
        fail(f"{path} is not a 32-bit little-endian ELF")

    header = struct.unpack_from("<HHIIIIIHHHHHH", data, 16)
    entry = header[3]
    phoff = header[4]
    shoff = header[5]
    phentsize = header[8]
    phnum = header[9]
    shentsize = header[10]
    shnum = header[11]
    shstrndx = header[12]
    if phentsize != 32 or shentsize != 40 or shstrndx >= shnum:
        fail(f"{path} has unsupported ELF table sizes")

    raw_sections: list[tuple[int, ...]] = []
    for index in range(shnum):
        raw_sections.append(struct.unpack_from("<IIIIIIIIII", data, shoff + index * shentsize))

    shstr = raw_sections[shstrndx]
    shstr_data = data[shstr[4] : shstr[4] + shstr[5]]
    sections = [
        Section(
            cstring(shstr_data, section[0]),
            section[3],
            section[4],
            section[5],
            section[6],
            section[9],
            section[1],
        )
        for section in raw_sections
    ]

    segments = []
    for index in range(phnum):
        segment = struct.unpack_from("<IIIIIIII", data, phoff + index * phentsize)
        segments.append(
            Segment(
                offset=segment[1],
                vaddr=segment[2],
                paddr=segment[3],
                filesz=segment[4],
                memsz=segment[5],
                segment_type=segment[0],
            )
        )
    return data, sections, segments, entry


def symbol_values(data: bytes, sections: list[Section]) -> dict[str, int]:
    values: dict[str, int] = {}
    for section in sections:
        if section.section_type != SHT_SYMTAB or section.entsize != 16:
            continue
        if section.link >= len(sections):
            fail("ELF symbol table has invalid linked string table")
        strings = sections[section.link]
        strings_data = data[strings.offset : strings.offset + strings.size]
        for offset in range(0, section.size, section.entsize):
            name_offset, value, _size, _info, _other, _shndx = struct.unpack_from(
                "<IIIBBH", data, section.offset + offset
            )
            if name_offset:
                values[cstring(strings_data, name_offset)] = value
    return values


def section_by_name(sections: list[Section], name: str) -> Section:
    for section in sections:
        if section.name == name:
            return section
    fail(f"ELF lacks required section {name}")


def require_config(path: Path) -> str:
    text = path.read_text(encoding="utf-8")
    required = (
        "CONFIG_BK7258_COMPONENT_CP=y",
        "CONFIG_BOARD_LATE_INITIALIZE=y",
        "CONFIG_BOARDCTL_START_CPU=y",
        "CONFIG_BUILTIN=y",
        "CONFIG_ARMV8M_DCACHE=y",
        "CONFIG_ARMV8M_ICACHE=y",
        "CONFIG_DEV_CONSOLE=y",
        "CONFIG_DEV_SIMPLE_ADDRENV=y",
        "CONFIG_SERIAL=y",
        'CONFIG_INIT_ENTRYPOINT="nsh_main"',
        "CONFIG_INIT_STACKSIZE=4096",
        "CONFIG_NSH_BUILTIN_APPS=y",
        "CONFIG_NSH_CONSOLE=y",
        "CONFIG_RAM_START=0x2806ec00",
        "CONFIG_RAM_SIZE=199424",
        "CONFIG_BK7258_RPMSG_SHM_ADDR=0x28064000",
        "CONFIG_BK7258_RPMSG_SHM_SIZE=0xac00",
        "CONFIG_BK7258_MAILBOX=y",
        "CONFIG_RPMSG_LOCAL_CPUNAME=\"cp\"",
        "CONFIG_RPMSG_PING=y",
        "CONFIG_RPMSG_UART=y",
        "CONFIG_RPTUN=y",
        "CONFIG_RPTUN_AUTO_RESET_DISABLE=y",
        "CONFIG_RPTUN_PRIORITY=90",
        "CONFIG_RPTUN_STACKSIZE=4096",
        "CONFIG_SYSTEM_CUTERM=y",
        'CONFIG_SYSTEM_CUTERM_DEFAULT_DEVICE="/dev/ttyAP"',
    )
    for setting in required:
        if setting not in text.splitlines():
            fail(f"CP build config lacks required setting: {setting}")
    if "CONFIG_BK7258_COMPONENT_AP=y" in text.splitlines():
        fail("CP build config also selects the AP component")
    return text


def main() -> int:
    args = parse_args()
    try:
        config_text = require_config(args.config)
        profile = json.loads(args.profile.read_text(encoding="utf-8"))
        if profile.get("profile") != "bk7258-devkit-l0-vendor-ap":
            fail(f"unsupported profile: {profile.get('profile')!r}")

        execution = profile["cpu0_l0_execution"]
        flash_base = int(execution["flash_vector_base"], 0)
        flash_limit = flash_base + profile["partitions"]["cp"]["raw_max_bytes"]
        sram_base = int(execution["sram_start"], 0)
        sram_limit = sram_base + int(execution["sram_size"], 0)
        rpmsg_base = 0x28064000
        rpmsg_size = 0x0000AC00
        if rpmsg_base + rpmsg_size != sram_base:
            fail("RPMSG_SHM does not end at the locked CP RAM base")
        vector_alignment = execution["vector_alignment_bytes"]

        elf_data, sections, segments, entry = parse_elf(args.elf)
        symbols = symbol_values(elf_data, sections)
        text = section_by_name(sections, ".text")
        data = section_by_name(sections, ".data")
        bss = section_by_name(sections, ".bss")
        for required in (
            "_vectors",
            "__start",
            "_sdata",
            "_edata",
            "_sbss",
            "_ebss",
            "_enoinit",
            "_eronly",
            "board_start_cpu",
            "bk7258_ap_start_monitor",
            "bk7258_ap_record_prepare",
            "bk7258_rptun_initialize",
            "bk7258_mbox_init",
            "bk7258_mbox_notify",
            "cu_main",
            "rpmsg_serialinit",
            "uart_rpmsg_init",
            "up_enable_icache",
            "up_disable_dcache",
        ):
            if required not in symbols:
                fail(f"ELF lacks required symbol {required}")

        if text.addr != flash_base:
            fail(f".text base 0x{text.addr:08x} != vector base 0x{flash_base:08x}")
        if symbols["_vectors"] != flash_base:
            fail(f"_vectors 0x{symbols['_vectors']:08x} != image base 0x{flash_base:08x}")
        if symbols["_vectors"] % vector_alignment:
            fail("_vectors does not meet locked vector alignment")
        if not (flash_base <= (entry & ~1) < flash_limit) or not entry & 1:
            fail(f"ELF entry 0x{entry:08x} is not a Thumb address in CP XIP")
        if not (flash_base <= symbols["__start"] < flash_limit):
            fail("__start is outside CP XIP")
        if data.addr != sram_base or symbols["_sdata"] != sram_base:
            fail(".data does not begin at locked CP SRAM base")
        if not (sram_base <= symbols["_sbss"] <= symbols["_ebss"] <= sram_limit):
            fail(".bss is outside locked CP SRAM")
        if not (sram_base <= symbols["_sdata"] <= symbols["_edata"] <= sram_limit):
            fail(".data is outside locked CP SRAM")
        if not (flash_base <= symbols["_eronly"] < flash_limit):
            fail(".data load address is outside CP XIP")

        idle_stack_size = execution["idle_stack_bytes"]
        if not (sram_base <= symbols["_enoinit"] <= symbols["_sbss"]):
            fail(".noinit does not end below CP .bss")
        idle_stack_top = symbols["_ebss"] + idle_stack_size
        if idle_stack_top > sram_limit:
            fail("idle stack extends beyond locked CP SRAM")

        data_loads = [
            segment
            for segment in segments
            if segment.segment_type == PT_LOAD and segment.vaddr == data.addr
        ]
        if len(data_loads) != 1 or data_loads[0].paddr != symbols["_eronly"]:
            fail(".data PT_LOAD address does not match _eronly")

        raw = args.raw.read_bytes()
        if len(raw) > profile["partitions"]["cp"]["raw_max_bytes"]:
            fail("raw app.bin exceeds CP virtual capacity")
        if len(raw) < 8:
            fail("raw app.bin lacks a Cortex-M vector prefix")
        msp = int.from_bytes(raw[:4], "little")
        reset = int.from_bytes(raw[4:8], "little")
        if not (sram_base <= msp <= sram_limit) or msp % 8:
            fail(f"raw MSP 0x{msp:08x} is outside/aligned incorrectly")
        if not reset & 1 or not (flash_base <= (reset & ~1) < flash_limit):
            fail(f"raw reset vector 0x{reset:08x} is invalid")

        flash_loads = [
            segment
            for segment in segments
            if segment.segment_type == PT_LOAD
            and segment.filesz > 0
            and flash_base <= segment.paddr < flash_limit
        ]
        if not flash_loads:
            fail("CP ELF has no loadable CP XIP segments")
        expected_end = max(segment.paddr + segment.filesz for segment in flash_loads)
        expected_raw = bytearray(expected_end - flash_base)
        for segment in flash_loads:
            start_offset = segment.paddr - flash_base
            expected_raw[start_offset : start_offset + segment.filesz] = elf_data[
                segment.offset : segment.offset + segment.filesz
            ]
        if raw != bytes(expected_raw):
            fail("app.bin does not exactly match the CP ELF PT_LOAD image")

        report = {
            "result": "pass",
            "component": "openvela-cp-ap-launcher",
            "elf": str(args.elf),
            "raw": str(args.raw),
            "sha256": hashlib.sha256(raw).hexdigest(),
            "elf_sha256": hashlib.sha256(elf_data).hexdigest(),
            "config": str(args.config.resolve()),
            "config_sha256": hashlib.sha256(config_text.encode()).hexdigest(),
            "flash_vector_base": f"0x{flash_base:08x}",
            "sram": {"start": f"0x{sram_base:08x}", "end": f"0x{sram_limit:08x}"},
            "rpmsg_shm": {
                "start": f"0x{rpmsg_base:08x}",
                "end": f"0x{rpmsg_base + rpmsg_size:08x}",
            },
            "vectors": {"msp": f"0x{msp:08x}", "reset": f"0x{reset:08x}"},
            "idle_stack_top": f"0x{idle_stack_top:08x}",
            "raw_length_bytes": len(raw),
            "sections": {
                "text": f"0x{text.addr:08x}",
                "data": f"0x{data.addr:08x}",
                "bss": f"0x{bss.addr:08x}",
            },
        }
        args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print(json.dumps(report, indent=2))
        return 0
    except (IndexError, KeyError, OSError, RuntimeError, UnicodeDecodeError, struct.error, TypeError, ValueError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
