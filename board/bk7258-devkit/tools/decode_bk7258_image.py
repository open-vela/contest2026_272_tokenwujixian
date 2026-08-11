#!/usr/bin/env python3
"""Independently decode and verify a BK7258 linear-CRC all-app image."""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Decode and independently verify a BK7258 linear-CRC package."
    )
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--report", type=Path)
    parser.add_argument("--compare-cp", type=Path)
    parser.add_argument("--compare-ap", type=Path)
    return parser.parse_args()


def fail(message: str) -> None:
    raise RuntimeError(message)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def crc16(payload: bytes) -> int:
    if len(payload) != 32:
        fail(f"CRC payload is {len(payload)} bytes, expected 32")
    value = 0xFFFFFFFF
    for byte in payload:
        value ^= byte << 8
        for _ in range(8):
            value = ((value << 1) ^ 0x8005) if value & 0x8000 else value << 1
    return value & 0xFFFF


def decode_component(
    image: bytes,
    label: str,
    offset: int,
    raw_length: int,
    expected_hash: str,
) -> dict[str, object]:
    encoded_length = ((raw_length + 31) // 32) * 34
    encoded = image[offset : offset + encoded_length]
    if len(encoded) != encoded_length:
        fail(
            f"{label} encoded range is truncated: expected {encoded_length} bytes "
            f"at 0x{offset:08x}, got {len(encoded)}"
        )

    decoded = bytearray()
    for block_offset in range(0, encoded_length, 34):
        payload = encoded[block_offset : block_offset + 32]
        stored_crc = int.from_bytes(encoded[block_offset + 32 : block_offset + 34], "big")
        actual_crc = crc16(payload)
        if stored_crc != actual_crc:
            fail(
                f"{label} CRC mismatch at physical 0x{offset + block_offset:08x}: "
                f"expected 0x{stored_crc:04x}, got 0x{actual_crc:04x}"
            )
        decoded.extend(payload)

    raw = bytes(decoded[:raw_length])
    padding = decoded[raw_length:]
    if padding and any(byte != 0xFF for byte in padding):
        fail(f"{label} final CRC payload padding is not all 0xff")
    digest = hashlib.sha256(raw).hexdigest()
    if digest != expected_hash:
        fail(f"{label} decoded SHA-256 mismatch: expected {expected_hash}, got {digest}")

    return {
        "label": label,
        "physical_offset": f"0x{offset:08x}",
        "raw_length_bytes": raw_length,
        "encoded_length_bytes": encoded_length,
        "raw_sha256": digest,
        "raw": raw,
    }


def compare_raw(label: str, decoded: bytes, source: Path | None) -> None:
    if source is None:
        return
    if not source.is_file():
        fail(f"{label} comparison file is missing: {source}")
    expected = source.read_bytes()
    if decoded != expected:
        fail(f"{label} decoded bytes differ from explicit comparison input: {source}")


def physical_offset(component: dict[str, object], label: str) -> int:
    value = component.get("physical_offset")
    if not isinstance(value, str):
        fail(f"{label} manifest entry lacks physical_offset")
    return int(value, 0)


def physical_size(component: dict[str, object], label: str) -> int:
    value = component.get("physical_size")
    if not isinstance(value, str):
        fail(f"{label} manifest entry lacks physical_size")
    return int(value, 0)


def validate_ff_padding(image: bytes, start: int, end: int, label: str) -> None:
    if end < start:
        fail(f"{label} padding bounds are inverted")
    padding = image[start:end]
    if any(byte != 0xFF for byte in padding):
        fail(f"{label} physical padding is not all 0xff")


def main() -> int:
    args = parse_args()
    try:
        manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
        if manifest.get("container_mode") != "linear-crc-physical-flash":
            fail("manifest is not a linear-CRC physical-flash package")

        image = args.image.read_bytes()
        image_entry = manifest.get("image", {})
        if len(image) != image_entry.get("length_bytes"):
            fail("image length differs from manifest")
        if sha256_file(args.image) != image_entry.get("sha256"):
            fail("image SHA-256 differs from manifest")

        components = manifest["components"]
        bootloader = components["bootloader"]
        cp = components["cp"]
        ap = components["ap"]
        bootloader_offset = physical_offset(bootloader, "bootloader")
        cp_offset = physical_offset(cp, "cp")
        ap_offset = physical_offset(ap, "ap")
        if bootloader_offset != 0:
            fail(f"bootloader physical offset must be zero, got 0x{bootloader_offset:08x}")
        if not bootloader_offset < cp_offset < ap_offset:
            fail("component physical offsets are not strictly increasing")
        decoded_bootloader = decode_component(
            image,
            "bootloader",
            bootloader_offset,
            bootloader["raw_length_bytes"],
            bootloader["sha256"],
        )
        decoded_cp = decode_component(
            image,
            "cp",
            cp_offset,
            cp["raw_length_bytes"],
            cp["sha256"],
        )
        decoded_ap = decode_component(
            image,
            "ap",
            ap_offset,
            ap["raw_length_bytes"],
            ap["sha256"],
        )
        compare_raw("CP", decoded_cp.pop("raw"), args.compare_cp)
        compare_raw("AP", decoded_ap.pop("raw"), args.compare_ap)
        decoded_bootloader.pop("raw")

        component_details = [
            ("bootloader", bootloader, decoded_bootloader, bootloader_offset),
            ("cp", cp, decoded_cp, cp_offset),
            ("ap", ap, decoded_ap, ap_offset),
        ]
        for label, component, decoded, offset in component_details:
            encoded_end = offset + decoded["encoded_length_bytes"]
            if decoded["encoded_length_bytes"] > physical_size(component, label):
                fail(f"{label} encoded payload exceeds its physical partition")
            decoded["physical_size_bytes"] = physical_size(component, label)
            decoded["physical_end"] = f"0x{encoded_end:08x}"

        validate_ff_padding(
            image,
            bootloader_offset + decoded_bootloader["encoded_length_bytes"],
            cp_offset,
            "bootloader-to-cp",
        )
        validate_ff_padding(
            image,
            cp_offset + decoded_cp["encoded_length_bytes"],
            ap_offset,
            "cp-to-ap",
        )

        final_end = ap_offset + decoded_ap["encoded_length_bytes"]
        final_suffix = image[final_end:]
        if len(final_suffix) < 34:
            fail("linear-CRC package lacks the required final 34-byte pre-read tail")
        if len(final_suffix) > 65 or len(image) % 32 != 0:
            fail("linear-CRC package has an invalid final alignment tail")
        if any(byte != 0xFF for byte in final_suffix):
            fail("linear-CRC package final alignment tail is not all 0xff")

        report = {
            "image": {
                "path": str(args.image),
                "length_bytes": len(image),
                "sha256": sha256_file(args.image),
            },
            "components": [decoded_bootloader, decoded_cp, decoded_ap],
            "final_alignment_tail_bytes": len(final_suffix),
            "result": "pass",
        }
        if args.report:
            args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print(json.dumps(report, indent=2))
        return 0
    except (KeyError, OSError, RuntimeError, TypeError, ValueError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
