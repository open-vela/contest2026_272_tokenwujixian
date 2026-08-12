#!/usr/bin/env python3
"""Build a BK7258 L2-dev image from repository-bundled inputs.

This packer needs no Armino SDK and no beken_genie build tree. It combines the
bundled vendor Bootloader, the caller's OpenVela CP app.bin, and the bundled
synthetic AP placeholder using the repository container encoder.

The AP payload is fixed to the bundled placeholder on purpose: the L0 contract
keeps the secondary cores unreleased. An OpenVela AP component requires its own
profile, not an override here.

Use tools/package_bk7258.py instead when the locked vendor AP payload and the
external SDK are available. Standard library only; never invokes OTA, the
downloader, eFuse or OTP flows, and never writes Flash.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path

import validate_bk7258_profile as profile_validator
from bk7258_container import Section, build_image


PROFILE_NAME = "bk7258-devkit-l0-bundled"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Package an OpenVela CP component with bundled BK7258 inputs."
    )
    parser.add_argument("--profile", type=Path, required=True)
    parser.add_argument("--cp-app-bin", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    return parser.parse_args()


def fail(message: str) -> None:
    raise RuntimeError(message)


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def load_bundled(packaging_dir: Path, entry: dict[str, object], label: str) -> bytes:
    relative_path = entry.get("relative_path")
    if not isinstance(relative_path, str):
        fail(f"bundled input {label} lacks relative_path")

    path = packaging_dir / relative_path
    if not path.is_file():
        fail(f"bundled input is missing: {label}: {path}")

    data = path.read_bytes()
    if len(data) != entry.get("raw_length_bytes"):
        fail(
            f"{label} length mismatch: expected {entry.get('raw_length_bytes')}, "
            f"got {len(data)}: {path}"
        )
    digest = sha256_bytes(data)
    if digest != entry.get("sha256"):
        fail(f"{label} SHA-256 mismatch: expected {entry.get('sha256')}, got {digest}")
    return data


def stage_bootloader(
    normal_bootloader: bytes,
    partition_table: bytes,
    expected: dict[str, object],
) -> bytes:
    staged = normal_bootloader
    staged += b"\x00" * ((-len(staged)) % 32)
    staged += partition_table

    if len(staged) != expected.get("raw_length_bytes"):
        fail(
            "staged Bootloader length mismatch: expected "
            f"{expected.get('raw_length_bytes')}, got {len(staged)}"
        )
    digest = sha256_bytes(staged)
    if digest != expected.get("sha256"):
        fail(
            f"staged Bootloader SHA-256 mismatch: expected {expected.get('sha256')}, "
            f"got {digest}"
        )
    return staged


def require_cp(path: Path, maximum_length: int) -> bytes:
    if not path.is_file():
        fail(f"CP input does not exist: {path}")
    data = path.read_bytes()
    if len(data) < 8:
        fail(f"CP input is too short to hold a Cortex-M vector prefix: {path}")
    if len(data) > maximum_length:
        fail(f"CP input exceeds its raw profile capacity: {len(data)} > {maximum_length}")
    return data


def component_entry(
    partition: dict[str, object],
    raw: bytes,
    source: str,
    extra: dict[str, object] | None = None,
) -> dict[str, object]:
    entry = {
        "raw_length_bytes": len(raw),
        "sha256": sha256_bytes(raw),
        "physical_offset": partition["physical_offset"],
        "physical_size": partition["physical_size"],
        "source": source,
    }
    if extra:
        entry.update(extra)
    return entry


def main() -> int:
    args = parse_args()
    try:
        profile_path = args.profile.resolve()
        profile = json.loads(profile_path.read_text(encoding="utf-8"))
        if profile.get("profile") != PROFILE_NAME:
            fail(
                f"unsupported profile: {profile.get('profile')!r}; "
                f"this packer only builds {PROFILE_NAME}"
            )
        if profile.get("container", {}).get("mode") != "linear-crc-physical-flash":
            fail("profile is not a linear-CRC physical-flash profile")

        output_dir = args.output_dir.resolve()
        output_image = output_dir / "all-app.bin"
        output_manifest = output_dir / "manifest.json"
        if output_image.exists() or output_manifest.exists():
            fail(
                f"refusing to overwrite existing package output in {output_dir}; "
                "use a new output directory or remove prior outputs explicitly"
            )

        packaging_dir = profile_path.parent.parent
        bundled = profile["bundled_inputs"]
        normal_bootloader = load_bundled(
            packaging_dir, bundled["normal_bootloader"], "normal_bootloader"
        )
        ota_partitions_path = (
            packaging_dir / bundled["ota_partitions_json"]["relative_path"]
        )
        load_bundled(packaging_dir, bundled["ota_partitions_json"], "ota_partitions_json")
        ap_raw = load_bundled(packaging_dir, bundled["ap_placeholder"], "ap_placeholder")

        partition_table = profile_validator.serialize_partition_table(
            ota_partitions_path
        )
        expected_table_bytes = profile["partition_table"]["serialized_bytes"]
        if len(partition_table) != expected_table_bytes:
            fail(
                f"serialized partition table is {len(partition_table)} bytes, "
                f"expected {expected_table_bytes}"
            )

        staged = stage_bootloader(
            normal_bootloader, partition_table, bundled["staged_bootloader"]
        )
        partitions = profile["partitions"]
        cp_raw = require_cp(args.cp_app_bin.resolve(), partitions["cp"]["raw_max_bytes"])
        if len(ap_raw) > partitions["ap"]["raw_max_bytes"]:
            fail("bundled AP placeholder exceeds its raw profile capacity")

        image = build_image(
            [
                Section(
                    int(str(partitions[label]["physical_offset"]), 0),
                    int(str(partitions[label]["physical_size"]), 0),
                    name,
                    raw,
                )
                for label, name, raw in (
                    ("bootloader", "bootloader", staged),
                    ("cp", "app", cp_raw),
                    ("ap", "app1", ap_raw),
                )
            ]
        )

        output_dir.mkdir(parents=True, exist_ok=True)
        output_image.write_bytes(image)

        manifest = {
            "schema_version": 1,
            "profile": profile["profile"],
            "container_mode": profile["container"]["mode"],
            "profile_path": str(profile_path),
            "profile_sha256": sha256_bytes(profile_path.read_bytes()),
            "components": {
                "bootloader": component_entry(
                    partitions["bootloader"], staged, "bundled-vendor-bootloader"
                ),
                "cp": component_entry(
                    partitions["cp"],
                    cp_raw,
                    "openvela-component",
                    {
                        "path": str(args.cp_app_bin.resolve()),
                        "xip_vector_base": partitions["cp"]["xip_vector_base"],
                    },
                ),
                "ap": component_entry(
                    partitions["ap"],
                    ap_raw,
                    "bundled-placeholder",
                    {"xip_vector_base": partitions["ap"]["xip_vector_base"]},
                ),
            },
            "image": {
                "path": str(output_image),
                "length_bytes": len(image),
                "sha256": sha256_bytes(image),
            },
            "packer": {
                "implementation": "board/bk7258-devkit/tools/bk7258_container.py",
                "external_sdk_required": False,
                "ota_or_download_flow_invoked": False,
            },
            "hardware_status": (
                "the bundled profile was cold-booted on a DevKit on 2026-08-12; "
                "see ap_placeholder_contract in the profile for what that does "
                "and does not establish"
            ),
        }
        output_manifest.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
        print(json.dumps(manifest, indent=2))
        return 0
    except (KeyError, OSError, RuntimeError, TypeError, ValueError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
