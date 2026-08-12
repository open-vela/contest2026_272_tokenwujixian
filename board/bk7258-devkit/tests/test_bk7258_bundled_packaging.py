#!/usr/bin/env python3
"""Offline regression tests for the bundled BK7258 L2-dev package profile.

Everything this test needs is committed in the repository: no Armino SDK, no
beken_genie build tree, and no hardware. It writes only a temporary directory.

Run it from the team repository root:

    python3 board/bk7258-devkit/tests/test_bk7258_bundled_packaging.py

The companion test_bk7258_packaging.py covers the vendor-input profile and
additionally proves the repository container encoder matches the SDK packer
byte-for-byte; that one needs the machine-local vendor tree.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
import tempfile
from pathlib import Path


BOARD_DIR = Path(__file__).resolve().parents[1]
TOOLS_DIR = BOARD_DIR / "tools"
PACKAGING_DIR = BOARD_DIR / "packaging"
DEFAULT_PROFILE = PACKAGING_DIR / "profiles/bk7258-devkit-l0-bundled.json"
sys.path.insert(0, str(TOOLS_DIR))

import bk7258_container as container


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run BK7258 bundled-input packaging regressions."
    )
    parser.add_argument("--profile", type=Path, default=DEFAULT_PROFILE)
    return parser.parse_args()


def fail(message: str) -> None:
    raise RuntimeError(message)


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def run(command: list[str]) -> str:
    result = subprocess.run(command, text=True, capture_output=True, check=False)
    if result.returncode != 0:
        fail(
            f"command failed ({' '.join(command)}):\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    return result.stdout


def expect_failure(command: list[str], reason: str) -> None:
    result = subprocess.run(command, text=True, capture_output=True, check=False)
    if result.returncode == 0:
        fail(f"command unexpectedly succeeded ({reason}): {' '.join(command)}")


def check_bundled_inputs(profile: dict[str, object]) -> None:
    for label, entry in profile["bundled_inputs"].items():
        relative_path = entry.get("relative_path")
        if relative_path is None:
            # staged_bootloader is derived, not a committed file.
            continue
        path = PACKAGING_DIR / relative_path
        if not path.is_file():
            fail(f"bundled input is missing: {label}: {path}")
        data = path.read_bytes()
        if len(data) != entry["raw_length_bytes"]:
            fail(
                f"{label} length mismatch: expected {entry['raw_length_bytes']}, "
                f"got {len(data)}"
            )
        digest = sha256_bytes(data)
        if digest != entry["sha256"]:
            fail(f"{label} SHA-256 mismatch: expected {entry['sha256']}, got {digest}")


def check_placeholder_is_reproducible(profile: dict[str, object], temp: Path) -> None:
    entry = profile["bundled_inputs"]["ap_placeholder"]
    regenerated = temp / "ap-placeholder-regenerated.bin"
    run(
        [
            sys.executable,
            str(TOOLS_DIR / "make_ap_placeholder.py"),
            "--output",
            str(regenerated),
        ]
    )
    committed = (PACKAGING_DIR / entry["relative_path"]).read_bytes()
    if regenerated.read_bytes() != committed:
        fail("make_ap_placeholder.py no longer reproduces the committed placeholder")

    payload = committed
    msp = int.from_bytes(payload[0:4], "little")
    reset = int.from_bytes(payload[4:8], "little")
    contract = profile["ap_placeholder_contract"]
    if msp != int(str(contract["msp"]), 0):
        fail(f"placeholder MSP is 0x{msp:08x}, profile declares {contract['msp']}")
    if reset != int(str(contract["reset_target"]), 0):
        fail(
            f"placeholder reset vector is 0x{reset:08x}, profile declares "
            f"{contract['reset_target']}"
        )
    if reset & 1 != 1:
        fail("placeholder reset vector is not a Thumb target")

    loop_offset = int(contract["vector_words"]) * 4
    if payload[loop_offset : loop_offset + 2] != (0xE7FE).to_bytes(2, "little"):
        fail("placeholder does not park the core in a self-branch loop")


def check_container_roundtrip() -> None:
    for raw_length in (1, 31, 32, 33, 66, 1024):
        raw = bytes((index * 7 + 1) & 0xFF for index in range(raw_length))
        encoded = container.encode_component(raw)
        if len(encoded) != container.encoded_length(raw_length):
            fail(
                f"encoded length disagrees with encoded_length() for {raw_length} bytes"
            )

        decoded = bytearray()
        for start in range(0, len(encoded), 34):
            block = encoded[start : start + 32]
            stored = int.from_bytes(encoded[start + 32 : start + 34], "big")
            if stored != container.crc16(block):
                fail(f"CRC16 roundtrip failed at block offset {start}")
            decoded.extend(block)
        if bytes(decoded[:raw_length]) != raw:
            fail(f"payload roundtrip failed for {raw_length} bytes")
        if any(byte != 0xFF for byte in decoded[raw_length:]):
            fail(f"final block padding is not 0xff for {raw_length} bytes")


def check_container_rejects_oversized_payload() -> None:
    try:
        container.build_image(
            [container.Section(0x0, 64, "tiny", bytes(64))]
        )
    except container.ContainerError:
        return
    fail("container accepted a payload that cannot fit its partition after CRC")


def assert_payload_mapping(image: bytes, offset: int, raw: bytes, label: str) -> None:
    block = raw[:32].ljust(32, b"\xff")
    if image[offset : offset + 32] != block:
        fail(f"{label} first payload block is not at physical 0x{offset:08x}")
    stored = image[offset + 32 : offset + 34]
    if stored != container.crc16(block).to_bytes(2, "big"):
        fail(f"{label} first CRC16 is not at physical 0x{offset + 32:08x}")


def package(profile_path: Path, cp_bin: Path, output_dir: Path) -> dict[str, object]:
    run(
        [
            sys.executable,
            str(TOOLS_DIR / "package_bk7258_bundled.py"),
            "--profile",
            str(profile_path),
            "--cp-app-bin",
            str(cp_bin),
            "--output-dir",
            str(output_dir),
        ]
    )
    return json.loads((output_dir / "manifest.json").read_text(encoding="utf-8"))


def decode(output_dir: Path, cp_bin: Path, ap_bin: Path) -> None:
    run(
        [
            sys.executable,
            str(TOOLS_DIR / "decode_bk7258_image.py"),
            "--image",
            str(output_dir / "all-app.bin"),
            "--manifest",
            str(output_dir / "manifest.json"),
            "--compare-cp",
            str(cp_bin),
            "--compare-ap",
            str(ap_bin),
        ]
    )


def main() -> int:
    args = parse_args()
    try:
        profile_path = args.profile.resolve()
        profile = json.loads(profile_path.read_text(encoding="utf-8"))
        if profile.get("profile") != "bk7258-devkit-l0-bundled":
            fail(f"unexpected profile: {profile.get('profile')!r}")

        check_bundled_inputs(profile)
        check_container_roundtrip()
        check_container_rejects_oversized_payload()

        ap_bin = PACKAGING_DIR / profile["bundled_inputs"]["ap_placeholder"]["relative_path"]
        cp_offset = int(str(profile["partitions"]["cp"]["physical_offset"]), 0)
        ap_offset = int(str(profile["partitions"]["ap"]["physical_offset"]), 0)

        with tempfile.TemporaryDirectory(prefix="bk7258-bundled-test-") as temporary:
            temp = Path(temporary)
            check_placeholder_is_reproducible(profile, temp)

            cp_fixture = temp / "synthetic-cp.bin"
            cp_fixture.write_bytes(
                (0x28001234).to_bytes(4, "little")
                + (0x02010001).to_bytes(4, "little")
                + bytes(range(0x10, 0x48))
            )
            first_dir = temp / "first"
            manifest = package(profile_path, cp_fixture, first_dir)
            decode(first_dir, cp_fixture, ap_bin)

            staged = profile["bundled_inputs"]["staged_bootloader"]
            if manifest["components"]["bootloader"]["sha256"] != staged["sha256"]:
                fail(
                    "packaged Bootloader SHA-256 differs from the profile's staged "
                    "Bootloader; the bundled Bootloader partition is no longer the "
                    "vendor-identical one"
                )

            image = (first_dir / "all-app.bin").read_bytes()
            if len(image) != manifest["image"]["length_bytes"]:
                fail("image length differs from its manifest")
            if len(image) % container.IMAGE_ALIGNMENT_BYTES != 0:
                fail("image is not 32-byte aligned")
            assert_payload_mapping(image, cp_offset, cp_fixture.read_bytes(), "CP")
            assert_payload_mapping(image, ap_offset, ap_bin.read_bytes(), "AP")

            # A second CP payload must move only the CP region.
            other_cp = temp / "synthetic-cp-2.bin"
            other_cp.write_bytes(
                (0x28005678).to_bytes(4, "little")
                + (0x02010101).to_bytes(4, "little")
                + bytes(range(0x80, 0xB8))
            )
            second_dir = temp / "second"
            package(profile_path, other_cp, second_dir)
            decode(second_dir, other_cp, ap_bin)
            other_image = (second_dir / "all-app.bin").read_bytes()

            if len(other_image) != len(image):
                fail("CP replacement changed the image length")
            if other_image[:cp_offset] != image[:cp_offset]:
                fail("CP replacement changed the Bootloader region")
            if other_image[ap_offset:] != image[ap_offset:]:
                fail("CP replacement changed the AP region or the final tail")
            if other_image[cp_offset:ap_offset] == image[cp_offset:ap_offset]:
                fail("CP replacement did not change the CP region")

            expect_failure(
                [
                    sys.executable,
                    str(TOOLS_DIR / "package_bk7258_bundled.py"),
                    "--profile",
                    str(profile_path),
                    "--cp-app-bin",
                    str(cp_fixture),
                    "--output-dir",
                    str(first_dir),
                ],
                "refusing to overwrite an existing package output",
            )
            expect_failure(
                [
                    sys.executable,
                    str(TOOLS_DIR / "package_bk7258_bundled.py"),
                    "--profile",
                    str(
                        PACKAGING_DIR / "profiles/bk7258-devkit-l0-vendor-ap.json"
                    ),
                    "--cp-app-bin",
                    str(cp_fixture),
                    "--output-dir",
                    str(temp / "wrong-profile"),
                ],
                "rejecting the vendor-AP profile in the bundled packer",
            )

        print("bundled input hashes match the profile: pass")
        print("AP placeholder is reproducible and parks the core: pass")
        print("container CRC roundtrip and capacity checks: pass")
        print("bundled package builds and independently decodes: pass")
        print("CP replacement mapping and isolation: pass")
        print("packager refuses overwrite and foreign profiles: pass")
        return 0
    except (KeyError, OSError, RuntimeError, TypeError, ValueError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
