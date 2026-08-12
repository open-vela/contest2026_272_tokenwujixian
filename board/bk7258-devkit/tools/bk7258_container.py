#!/usr/bin/env python3
"""Repository-local implementation of the BK7258 linear-CRC Flash container.

The encoding is the one the locked Armino SDK applies through
``bk_packager.bk_packager_linear_crc``: every partition payload is padded to a
32-byte boundary, split into 32-byte blocks, and each block is followed by a
big-endian CRC16 over that block. Partitions are then written at their physical
Flash offsets with 0xff gap padding, a 34-byte 0xff pre-read tail is appended,
and the image is padded to a 32-byte boundary.

This module exists so a bundled-input package can be produced without the
external SDK. ``tests/test_bk7258_packaging.py`` proves it reproduces the SDK
packer byte-for-byte for the locked vendor inputs; do not change the encoding
without re-running that equivalence check.

Standard library only. Never invokes OTA, downloader, eFuse or OTP flows.
"""

from __future__ import annotations

from typing import NamedTuple


CRC_PAYLOAD_BYTES = 32
CRC_BYTES = 2
CRC_POLYNOMIAL = 0x8005
PADDING_BYTE = 0xFF

# bk_packager_crc_decorator.post_link appends this tail so the Bootloader can
# always pre-read at least one full CRC block past the last payload.
TRAILING_PREREAD_BYTES = 34

# The SDK's bk_build_package.py aligns all-app.bin after the packer returns.
IMAGE_ALIGNMENT_BYTES = 32


class ContainerError(RuntimeError):
    """Raised when inputs cannot form a valid linear-CRC image."""


class Section(NamedTuple):
    """One partition placed at a physical Flash offset."""

    physical_offset: int
    physical_size: int
    name: str
    raw: bytes


def crc16(payload: bytes) -> int:
    """Return the Beken CRC16 (polynomial 0x8005) of one 32-byte block."""
    if len(payload) != CRC_PAYLOAD_BYTES:
        raise ContainerError(
            f"CRC payload is {len(payload)} bytes, expected {CRC_PAYLOAD_BYTES}"
        )

    value = 0xFFFFFFFF
    for byte in payload:
        value ^= byte << 8
        for _ in range(8):
            value = ((value << 1) ^ CRC_POLYNOMIAL) if value & 0x8000 else value << 1
    return value & 0xFFFF


def encoded_length(raw_length: int) -> int:
    """Return the container length a payload of ``raw_length`` bytes occupies."""
    if raw_length < 0:
        raise ContainerError(f"negative raw length: {raw_length}")
    blocks = (raw_length + CRC_PAYLOAD_BYTES - 1) // CRC_PAYLOAD_BYTES
    return blocks * (CRC_PAYLOAD_BYTES + CRC_BYTES)


def encode_component(raw: bytes) -> bytes:
    """Pad ``raw`` to a 32-byte boundary and interleave a CRC16 per block."""
    padding = (-len(raw)) % CRC_PAYLOAD_BYTES
    aligned = raw + bytes([PADDING_BYTE]) * padding

    encoded = bytearray()
    for start in range(0, len(aligned), CRC_PAYLOAD_BYTES):
        block = aligned[start : start + CRC_PAYLOAD_BYTES]
        encoded.extend(block)
        encoded.extend(crc16(block).to_bytes(CRC_BYTES, "big"))
    return bytes(encoded)


def _check_overlaps(sections: list[Section]) -> None:
    intervals = sorted(
        (section.physical_offset, section.physical_offset + section.physical_size)
        for section in sections
    )
    for index in range(1, len(intervals)):
        if intervals[index][0] < intervals[index - 1][1]:
            raise ContainerError("declared partitions overlap")


def build_image(sections: list[Section]) -> bytes:
    """Encode and link ``sections`` into a complete linear-CRC Flash image."""
    if not sections:
        raise ContainerError("no sections to link")
    _check_overlaps(sections)

    image = bytearray()
    position = 0
    for section in sorted(sections):
        encoded = encode_component(section.raw)
        if len(encoded) > section.physical_size:
            raise ContainerError(
                f"partition {section.name} is {len(encoded)} bytes after CRC "
                f"encoding, which exceeds its {section.physical_size}-byte "
                "physical size"
            )
        if position > section.physical_offset:
            raise ContainerError(
                f"partition {section.name} starts at 0x{section.physical_offset:08x} "
                f"but the previous payload already reached 0x{position:08x}"
            )

        # bk_packager_linear_linker only pads once a payload has been written,
        # so a non-zero first offset is not padded. The cursor still advances to
        # that offset, which keeps the following partition's gap correct. Kept
        # identical to the SDK; the locked profile starts at 0x0 either way.
        if position:
            image.extend(bytes([PADDING_BYTE]) * (section.physical_offset - position))
        image.extend(encoded)
        position = section.physical_offset + len(encoded)

    image.extend(bytes([PADDING_BYTE]) * TRAILING_PREREAD_BYTES)
    image.extend(bytes([PADDING_BYTE]) * ((-len(image)) % IMAGE_ALIGNMENT_BYTES))
    return bytes(image)
