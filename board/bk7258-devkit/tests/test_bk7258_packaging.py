#!/usr/bin/env python3
"""Offline regression tests for the locked BK7258 L2-dev package profile.

The test reads locked vendor inputs but writes only a temporary directory. It
does not use the downloader, OTA flow, eFuse/OTP operations, or hardware.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


BOARD_DIR = Path(__file__).resolve().parents[1]
TOOLS_DIR = BOARD_DIR / "tools"
sys.path.insert(0, str(TOOLS_DIR))

import validate_bk7258_profile as profile_validator


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run BK7258 vendor reconstruction and component replacement regressions."
    )
    parser.add_argument("--profile", type=Path, required=True)
    parser.add_argument("--inputs-cmake", type=Path, required=True)
    return parser.parse_args()


def fail(message: str) -> None:
    raise RuntimeError(message)


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: Path) -> str:
    return sha256_bytes(path.read_bytes())


def crc16(payload: bytes) -> int:
    if len(payload) != 32:
        fail(f"CRC payload is {len(payload)} bytes, expected 32")
    value = 0xFFFFFFFF
    for byte in payload:
        value ^= byte << 8
        for _ in range(8):
            value = ((value << 1) ^ 0x8005) if value & 0x8000 else value << 1
    return value & 0xFFFF


def run(command: list[str], *, env: dict[str, str] | None = None) -> str:
    result = subprocess.run(command, text=True, capture_output=True, env=env, check=False)
    if result.returncode != 0:
        fail(
            f"command failed ({' '.join(command)}):\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    return result.stdout


def package_with_sdk(
    sdk_root: Path,
    work_dir: Path,
    package_json: Path,
    output_image: Path,
) -> None:
    sdk_python_path = sdk_root / "tools/env_tools/bk_py_libs"
    source = (
        "from pathlib import Path\n"
        "import bk_packager\n"
        f"packer = bk_packager.bk_packager_linear_crc(Path({str(work_dir)!r}), "
        f"Path({str(package_json)!r}), Path({str(output_image)!r}))\n"
        "packer.pack()\n"
    )
    environment = os.environ.copy()
    environment["PYTHONPATH"] = str(sdk_python_path)
    run([sys.executable, "-c", source], env=environment)
    padding = (-output_image.stat().st_size) % 32
    if padding:
        with output_image.open("ab") as output:
            output.write(b"\xff" * padding)


def make_manifest(
    profile: dict[str, object],
    output_image: Path,
    staged_bootloader: Path,
    cp_input: Path,
    ap_input: Path,
) -> Path:
    components = {
        "bootloader": {
            "raw_length_bytes": staged_bootloader.stat().st_size,
            "sha256": sha256_file(staged_bootloader),
            "physical_offset": profile["partitions"]["bootloader"]["physical_offset"],
            "physical_size": profile["partitions"]["bootloader"]["physical_size"],
        },
        "cp": {
            "raw_length_bytes": cp_input.stat().st_size,
            "sha256": sha256_file(cp_input),
            "physical_offset": profile["partitions"]["cp"]["physical_offset"],
            "physical_size": profile["partitions"]["cp"]["physical_size"],
        },
        "ap": {
            "raw_length_bytes": ap_input.stat().st_size,
            "sha256": sha256_file(ap_input),
            "physical_offset": profile["partitions"]["ap"]["physical_offset"],
            "physical_size": profile["partitions"]["ap"]["physical_size"],
        },
    }
    manifest = {
        "container_mode": "linear-crc-physical-flash",
        "image": {
            "length_bytes": output_image.stat().st_size,
            "sha256": sha256_file(output_image),
        },
        "components": components,
    }
    manifest_path = output_image.with_suffix(".manifest.json")
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    return manifest_path


def package_fixture(
    sdk_root: Path,
    package_json: Path,
    staged_bootloader: Path,
    cp_input: Path,
    ap_input: Path,
    output_dir: Path,
) -> tuple[Path, Path]:
    work_dir = output_dir / "work"
    work_dir.mkdir(parents=True)
    shutil.copy2(staged_bootloader, work_dir / "bootloader.bin")
    shutil.copy2(cp_input, work_dir / "app.bin")
    shutil.copy2(ap_input, work_dir / "app1.bin")
    work_package_json = work_dir / "bk_package.json"
    shutil.copy2(package_json, work_package_json)
    image = output_dir / "all-app.bin"
    package_with_sdk(sdk_root, work_dir, work_package_json, image)
    return image, work_dir


def assert_payload_mapping(image: bytes, offset: int, fixture: bytes, label: str) -> None:
    if len(fixture) != 64:
        fail(f"{label} fixture must be exactly 64 bytes")
    if image[offset : offset + 32] != fixture[:32]:
        fail(f"{label} first payload block is not at physical offset 0x{offset:08x}")
    stored_crc = image[offset + 32 : offset + 34]
    expected_crc = crc16(fixture[:32]).to_bytes(2, "big")
    if stored_crc != expected_crc:
        fail(f"{label} first CRC16 bytes are not at physical offset 0x{offset + 32:08x}")
    if image[offset + 34 : offset + 66] != fixture[32:64]:
        fail(f"{label} second payload block is not at physical offset 0x{offset + 34:08x}")


def run_decoder(
    decoder: Path,
    image: Path,
    manifest: Path,
    cp_input: Path,
    ap_input: Path,
) -> None:
    run(
        [
            sys.executable,
            str(decoder),
            "--image",
            str(image),
            "--manifest",
            str(manifest),
            "--compare-cp",
            str(cp_input),
            "--compare-ap",
            str(ap_input),
        ]
    )


def main() -> int:
    args = parse_args()
    try:
        profile = json.loads(args.profile.read_text(encoding="utf-8"))
        local_inputs = profile_validator.parse_local_cmake(args.inputs_cmake)
        sdk_root = local_inputs["BK7258_ARMINO_SDK_ROOT"]
        build_root = local_inputs["BK7258_BEKEN_GENIE_BUILD_ROOT"]
        locked = profile["locked_inputs"]
        package_json = build_root / locked["package_json"]["relative_path"]
        vendor_cp = build_root / locked["vendor_cp_fixture"]["relative_path"]
        vendor_ap = build_root / locked["vendor_ap_input"]["relative_path"]
        recovery = build_root / locked["vendor_all_app_recovery_fixture"]["relative_path"]

        with tempfile.TemporaryDirectory(prefix="bk7258-packaging-test-") as temporary:
            temp = Path(temporary)
            local_cmake = temp / "bk7258-vendor-inputs.cmake"
            shutil.copy2(args.inputs_cmake, local_cmake)
            baseline_dir = temp / "baseline"
            run(
                [
                    sys.executable,
                    str(TOOLS_DIR / "package_bk7258.py"),
                    "--profile",
                    str(args.profile.resolve()),
                    "--inputs-cmake",
                    str(local_cmake),
                    "--cp-app-bin",
                    str(vendor_cp),
                    "--ap-app-bin",
                    str(vendor_ap),
                    "--output-dir",
                    str(baseline_dir),
                ]
            )
            baseline = baseline_dir / "all-app.bin"
            if baseline.read_bytes() != recovery.read_bytes():
                fail("vendor no-change reconstruction differs from locked all-app fixture")
            run_decoder(
                TOOLS_DIR / "decode_bk7258_image.py",
                baseline,
                baseline_dir / "manifest.json",
                vendor_cp,
                vendor_ap,
            )

            staged_bootloader = baseline_dir / "staging/staged-bootloader.bin"
            cp_fixture = temp / "synthetic-cp.bin"
            cp_fixture.write_bytes(
                (0x28001234).to_bytes(4, "little")
                + (0x02010001).to_bytes(4, "little")
                + bytes(range(0x10, 0x48))
            )
            cp_image, _ = package_fixture(
                sdk_root,
                package_json,
                staged_bootloader,
                cp_fixture,
                vendor_ap,
                temp / "synthetic-cp",
            )
            cp_manifest = make_manifest(
                profile, cp_image, staged_bootloader, cp_fixture, vendor_ap
            )
            run_decoder(
                TOOLS_DIR / "decode_bk7258_image.py",
                cp_image,
                cp_manifest,
                cp_fixture,
                vendor_ap,
            )
            cp_offset = int(profile["partitions"]["cp"]["physical_offset"], 0)
            ap_offset = int(profile["partitions"]["ap"]["physical_offset"], 0)
            cp_image_bytes = cp_image.read_bytes()
            baseline_bytes = baseline.read_bytes()
            assert_payload_mapping(cp_image_bytes, cp_offset, cp_fixture.read_bytes(), "CP")
            if cp_image_bytes[:cp_offset] != baseline_bytes[:cp_offset]:
                fail("synthetic CP replacement changed Bootloader or pre-CP padding")
            if cp_image_bytes[ap_offset:] != baseline_bytes[ap_offset:]:
                fail("synthetic CP replacement changed AP payload or final tail")

            ap_fixture = temp / "synthetic-ap.bin"
            ap_fixture.write_bytes(
                (0x28005678).to_bytes(4, "little")
                + (0x02160001).to_bytes(4, "little")
                + bytes(range(0x80, 0xB8))
            )
            ap_image, _ = package_fixture(
                sdk_root,
                package_json,
                staged_bootloader,
                vendor_cp,
                ap_fixture,
                temp / "synthetic-ap",
            )
            ap_manifest = make_manifest(
                profile, ap_image, staged_bootloader, vendor_cp, ap_fixture
            )
            run_decoder(
                TOOLS_DIR / "decode_bk7258_image.py",
                ap_image,
                ap_manifest,
                vendor_cp,
                ap_fixture,
            )
            ap_image_bytes = ap_image.read_bytes()
            assert_payload_mapping(ap_image_bytes, ap_offset, ap_fixture.read_bytes(), "AP")
            if ap_image_bytes[:ap_offset] != baseline_bytes[:ap_offset]:
                fail("synthetic AP replacement changed Bootloader or CP payload")

        print("vendor no-change reconstruction: pass")
        print("independent baseline decode: pass")
        print("synthetic CP replacement mapping and isolation: pass")
        print("synthetic AP replacement mapping and isolation: pass")
        return 0
    except (KeyError, OSError, RuntimeError, TypeError, ValueError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
