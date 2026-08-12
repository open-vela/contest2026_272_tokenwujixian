#!/usr/bin/env python3
"""Generate the deterministic BK7258 AP placeholder payload.

The L0 contract never releases the secondary cores, so the AP partition only has
to be occupied by a well-formed payload instead of the vendor AP image. This
generator emits a minimal Cortex-M image whose reset handler and every populated
exception vector branch to a single self-branch loop, so that a hypothetical
Bootloader handoff parks the core instead of executing 0xff padding.

The output is committed as packaging/bundled/ap-placeholder.bin and its SHA-256
is locked in the bundled profile. Re-running this generator must reproduce that
file byte-for-byte.

Standard library only. Reads nothing external, writes only its output path.
"""

from __future__ import annotations

import argparse
import hashlib
from pathlib import Path


# Physical AP offset 0x00176000 maps to a CRC-visible offset of
# 0x00176000 / 34 * 32 = 0x00160000, so the AP XIP vector base is
# 0x02000000 + 0x00160000. The locked vendor AP reset vector 0x021d5e14 lies
# 0x75e14 into that window, which is consistent with this base but does not on
# its own prove it. Under the L0 contract the placeholder never executes, so the
# base only has to be self-consistent for the parked loop.
AP_XIP_VECTOR_BASE = 0x02160000

# 16 words: initial MSP, reset, and the architected exception vectors.
VECTOR_WORDS = 16
LOOP_OFFSET = VECTOR_WORDS * 4

# Top of the 64 KiB shared SRAM bank at 0x28000000. The loop never pushes, so
# this only matters if the core takes an exception; it stays clear of the
# CPU0 allocation at 0x2806ec00.
AP_PLACEHOLDER_MSP = 0x28010000

# Thumb "b ." — branch to self.
SELF_BRANCH_THUMB = 0xE7FE


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Emit the deterministic BK7258 AP placeholder payload."
    )
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument(
        "--print-only",
        action="store_true",
        help="Report the payload length and SHA-256 without writing the output.",
    )
    return parser.parse_args()


def build_placeholder() -> bytes:
    loop_target = (AP_XIP_VECTOR_BASE + LOOP_OFFSET) | 1

    payload = bytearray()
    payload.extend(AP_PLACEHOLDER_MSP.to_bytes(4, "little"))
    for _ in range(VECTOR_WORDS - 1):
        payload.extend(loop_target.to_bytes(4, "little"))
    if len(payload) != LOOP_OFFSET:
        raise RuntimeError(
            f"vector table is {len(payload)} bytes, expected {LOOP_OFFSET}"
        )

    payload.extend(SELF_BRANCH_THUMB.to_bytes(2, "little"))
    return bytes(payload)


def main() -> int:
    args = parse_args()
    payload = build_placeholder()
    digest = hashlib.sha256(payload).hexdigest()

    if not args.print_only:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_bytes(payload)

    print(f"raw_length_bytes: {len(payload)}")
    print(f"sha256: {digest}")
    print(f"xip_vector_base: 0x{AP_XIP_VECTOR_BASE:08x}")
    print(f"msp: 0x{AP_PLACEHOLDER_MSP:08x}")
    print(f"loop_target: 0x{(AP_XIP_VECTOR_BASE + LOOP_OFFSET) | 1:08x}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
