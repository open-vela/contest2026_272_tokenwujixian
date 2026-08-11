#!/usr/bin/env python3
"""Build the locked BK7258 L2-dev linear-CRC Flash image.

This tool calls only the locked SDK's bk_packager_linear_crc substep. It never
calls the SDK's post-package/OTA flow, the downloader, or any hardware tool.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

import validate_bk7258_profile as profile_validator


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Package a locked BK7258 CP/AP component set into all-app.bin."
    )
    parser.add_argument("--profile", type=Path, required=True)
    parser.add_argument("--inputs-cmake", type=Path, required=True)
    parser.add_argument("--cp-app-bin", type=Path, required=True)
    parser.add_argument(
        "--ap-app-bin",
        type=Path,
        required=True,
        help="Explicit AP input. The L0 profile accepts only its locked vendor app1.bin.",
    )
    parser.add_argument("--output-dir", type=Path, required=True)
    return parser.parse_args()


def fail(message: str) -> None:
    raise RuntimeError(message)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def require_raw_component(
    label: str,
    path: Path,
    maximum_length: int,
) -> tuple[int, str]:
    if not path.is_file():
        fail(f"{label} input does not exist: {path}")
    length = path.stat().st_size
    if length < 8:
        fail(f"{label} input is too short to hold a Cortex-M vector prefix: {path}")
    if length > maximum_length:
        fail(
            f"{label} input exceeds its raw profile capacity: "
            f"{length} > {maximum_length}: {path}"
        )
    return length, sha256_file(path)


def run_profile_gate(
    script_path: Path,
    profile_path: Path,
    inputs_cmake: Path,
    staging_dir: Path,
) -> dict[str, object]:
    command = [
        sys.executable,
        str(script_path),
        "--profile",
        str(profile_path),
        "--inputs-cmake",
        str(inputs_cmake),
        "--output-dir",
        str(staging_dir),
        "--write-report",
    ]
    result = subprocess.run(command, text=True, capture_output=True, check=False)
    if result.returncode != 0:
        fail(f"profile gate failed:\n{result.stderr.strip()}")

    report_path = staging_dir / "validated-profile.json"
    if not report_path.is_file():
        fail(f"profile gate did not create {report_path}")
    return json.loads(report_path.read_text(encoding="utf-8"))


def build_sdk_package(
    sdk_root: Path,
    work_dir: Path,
    package_json: Path,
    output_image: Path,
) -> None:
    sdk_python_path = sdk_root / "tools/env_tools/bk_py_libs"
    if not sdk_python_path.is_dir():
        fail(f"locked SDK Python library directory is missing: {sdk_python_path}")

    source = (
        "from pathlib import Path\n"
        "import bk_packager\n"
        f"packer = bk_packager.bk_packager_linear_crc(Path({str(work_dir)!r}), "
        f"Path({str(package_json)!r}), Path({str(output_image)!r}))\n"
        "packer.pack()\n"
    )
    environment = os.environ.copy()
    environment["PYTHONPATH"] = str(sdk_python_path)
    result = subprocess.run(
        [sys.executable, "-c", source],
        text=True,
        capture_output=True,
        env=environment,
        check=False,
    )
    if result.returncode != 0:
        fail(f"locked SDK linear-CRC packer failed:\n{result.stderr.strip()}")

    # SDK bk_build_package.py additionally aligns its all-app.bin output to a
    # 32-byte boundary after bk_packager_linear_crc appends its 34-byte tail.
    output_length = output_image.stat().st_size
    trailing_alignment = (-output_length) % 32
    if trailing_alignment:
        with output_image.open("ab") as output:
            output.write(b"\xff" * trailing_alignment)


def main() -> int:
    args = parse_args()
    try:
        profile = json.loads(args.profile.read_text(encoding="utf-8"))
        if profile.get("profile") != "bk7258-devkit-l0-vendor-ap":
            fail(f"unsupported profile: {profile.get('profile')!r}")
        if profile.get("container", {}).get("mode") != "linear-crc-physical-flash":
            fail("profile is not a linear-CRC physical-flash profile")

        output_dir = args.output_dir.resolve()
        staging_dir = output_dir / "staging"
        work_dir = output_dir / "packer-work"
        output_image = output_dir / "all-app.bin"
        output_manifest = output_dir / "manifest.json"

        if output_image.exists() or output_manifest.exists():
            fail(
                f"refusing to overwrite existing package output in {output_dir}; "
                "use a new output directory or remove prior generated outputs explicitly"
            )

        profile_gate = run_profile_gate(
            Path(__file__).with_name("validate_bk7258_profile.py"),
            args.profile.resolve(),
            args.inputs_cmake.resolve(),
            staging_dir,
        )
        local_inputs = profile_validator.parse_local_cmake(args.inputs_cmake.resolve())
        build_root = local_inputs["BK7258_BEKEN_GENIE_BUILD_ROOT"]
        sdk_root = local_inputs["BK7258_ARMINO_SDK_ROOT"]

        cp_length, cp_hash = require_raw_component(
            "CP", args.cp_app_bin.resolve(), profile["partitions"]["cp"]["raw_max_bytes"]
        )
        ap_length, ap_hash = require_raw_component(
            "AP", args.ap_app_bin.resolve(), profile["partitions"]["ap"]["raw_max_bytes"]
        )
        vendor_ap = profile["locked_inputs"]["vendor_ap_input"]
        if ap_length != vendor_ap["raw_length_bytes"] or ap_hash != vendor_ap["sha256"]:
            fail(
                "the L0 profile accepts only its SHA-256-locked vendor AP input; "
                "an OpenVela AP component requires a later profile"
            )

        work_dir.mkdir(parents=True, exist_ok=False)
        shutil.copy2(staging_dir / "staged-bootloader.bin", work_dir / "bootloader.bin")
        shutil.copy2(args.cp_app_bin.resolve(), work_dir / "app.bin")
        shutil.copy2(args.ap_app_bin.resolve(), work_dir / "app1.bin")
        package_json = build_root / profile["locked_inputs"]["package_json"]["relative_path"]
        work_package_json = work_dir / "bk_package.json"
        shutil.copy2(package_json, work_package_json)

        output_dir.mkdir(parents=True, exist_ok=True)
        build_sdk_package(sdk_root, work_dir, work_package_json, output_image)
        output_hash = sha256_file(output_image)

        manifest = {
            "schema_version": 1,
            "profile": profile["profile"],
            "container_mode": profile["container"]["mode"],
            "profile_path": str(args.profile.resolve()),
            "profile_sha256": sha256_file(args.profile.resolve()),
            "components": {
                "bootloader": {
                    **profile_gate["staged_bootloader"],
                    "physical_offset": profile["partitions"]["bootloader"]["physical_offset"],
                    "physical_size": profile["partitions"]["bootloader"]["physical_size"],
                },
                "cp": {
                    "path": str(args.cp_app_bin.resolve()),
                    "raw_length_bytes": cp_length,
                    "sha256": cp_hash,
                    "physical_offset": profile["partitions"]["cp"]["physical_offset"],
                    "physical_size": profile["partitions"]["cp"]["physical_size"],
                    "xip_vector_base": profile["partitions"]["cp"]["xip_vector_base"],
                },
                "ap": {
                    "path": str(args.ap_app_bin.resolve()),
                    "raw_length_bytes": ap_length,
                    "sha256": ap_hash,
                    "physical_offset": profile["partitions"]["ap"]["physical_offset"],
                    "physical_size": profile["partitions"]["ap"]["physical_size"],
                    "source": "locked-vendor-input",
                },
            },
            "image": {
                "path": str(output_image),
                "length_bytes": output_image.stat().st_size,
                "sha256": output_hash,
            },
            "profile_gate": profile_gate,
            "packer": {
                "sdk_root": str(sdk_root),
                "sdk_revision": profile["locked_inputs"]["armino_sdk"]["revision"],
                "implementation": "bk_packager.bk_packager_linear_crc",
                "ota_or_download_flow_invoked": False,
            },
        }
        output_manifest.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
        print(json.dumps(manifest, indent=2))
        return 0
    except (KeyError, OSError, RuntimeError, TypeError, ValueError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
