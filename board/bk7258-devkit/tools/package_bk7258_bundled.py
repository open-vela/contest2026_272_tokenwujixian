#!/usr/bin/env python3
"""Build a BK7258 L2-dev image from repository-bundled inputs.

This packer needs no Armino SDK and no beken_genie build tree. It supports two
separate profiles: the CP-only recovery profile with its bundled AP placeholder,
and the OpenVela AP profile which requires an explicit validated app1.bin.

Use tools/package_bk7258.py instead when the locked vendor AP payload and the
external SDK are available. Standard library only; never invokes OTA, the
downloader, eFuse or OTP flows, and never writes Flash.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
import tempfile
from pathlib import Path

import validate_bk7258_profile as profile_validator
from bk7258_container import Section, build_image


PLACEHOLDER_PROFILE = "bk7258-devkit-l0-bundled"
OPENVELA_AP_PROFILE = "bk7258-devkit-openvela-ap"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Package an OpenVela CP component with bundled BK7258 inputs."
    )
    parser.add_argument("--profile", type=Path, required=True)
    parser.add_argument("--cp-app-bin", type=Path, required=True)
    parser.add_argument("--cp-validation-report", type=Path)
    parser.add_argument("--ap-app-bin", type=Path)
    parser.add_argument("--ap-validation-report", type=Path)
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


def require_openvela_ap(
    path: Path, partition: dict[str, object], execution: dict[str, object]
) -> bytes:
    if not path.is_file():
        fail(f"AP input does not exist: {path}")
    data = path.read_bytes()
    if len(data) < 8:
        fail(f"AP input is too short to hold a Cortex-M vector prefix: {path}")
    if len(data) > partition["raw_max_bytes"]:
        fail(
            f"AP input exceeds its raw profile capacity: "
            f"{len(data)} > {partition['raw_max_bytes']}"
        )

    flash_start = int(str(partition["xip_vector_base"]), 0)
    flash_end = flash_start + int(partition["raw_max_bytes"])
    ram_start = int(str(execution["sram_start"]), 0)
    ram_end = int(str(execution["sram_end"]), 0)
    msp = int.from_bytes(data[0:4], "little")
    reset = int.from_bytes(data[4:8], "little")
    if not ram_start <= msp <= ram_end or msp % 8:
        fail(f"AP initial MSP 0x{msp:08x} is outside/aligned incorrectly")
    if not reset & 1 or not flash_start <= (reset & ~1) < flash_end:
        fail(f"AP reset vector 0x{reset:08x} is outside AP XIP or not Thumb")
    return data


def require_cp_validation(
    report_path: Path,
    cp_path: Path,
    cp_raw: bytes,
    packaging_dir: Path,
) -> dict[str, object]:
    if not report_path.is_file():
        fail(f"CP L1 validation report does not exist: {report_path}")
    report = json.loads(report_path.read_text(encoding="utf-8"))
    if report.get("result") != "pass":
        fail("CP L1 validation report does not pass")
    if report.get("component") != "openvela-cp-ap-launcher":
        fail("CP L1 validation report lacks the AP launcher contract")
    if Path(str(report.get("raw", ""))).resolve() != cp_path:
        fail("CP L1 validation report names a different raw image")
    if report.get("sha256") != sha256_bytes(cp_raw):
        fail("CP L1 validation report SHA-256 does not match app.bin")
    if report.get("raw_length_bytes") != len(cp_raw):
        fail("CP L1 validation report length does not match app.bin")

    elf_path = Path(str(report.get("elf", ""))).resolve()
    if not elf_path.is_file():
        fail(f"CP L1 validation report ELF is missing: {elf_path}")
    elf_hash = sha256_bytes(elf_path.read_bytes())
    if report.get("elf_sha256") != elf_hash:
        fail("CP L1 validation report ELF SHA-256 does not match its ELF")
    config_path = Path(str(report.get("config", ""))).resolve()
    if not config_path.is_file():
        fail(f"CP L1 validation report config is missing: {config_path}")
    config_hash = sha256_bytes(config_path.read_bytes())
    if report.get("config_sha256") != config_hash:
        fail("CP L1 validation report config SHA-256 does not match its config")

    cp_profile = packaging_dir / "profiles/bk7258-devkit-l0-vendor-ap.json"
    with tempfile.TemporaryDirectory(prefix="bk7258-cp-validation-") as temporary:
        regenerated_path = Path(temporary) / "l1-validation.json"
        result = subprocess.run(
            [
                sys.executable,
                str(Path(__file__).with_name("validate_bk7258_cp_image.py")),
                "--profile",
                str(cp_profile),
                "--elf",
                str(elf_path),
                "--raw",
                str(cp_path),
                "--config",
                str(config_path),
                "--report",
                str(regenerated_path),
            ],
            text=True,
            capture_output=True,
            check=False,
        )
        if result.returncode != 0:
            fail(
                "CP L1 validation could not be reproduced:\n"
                f"{result.stdout}{result.stderr}"
            )
        regenerated = json.loads(regenerated_path.read_text(encoding="utf-8"))
        if regenerated != report:
            fail("CP L1 validation report is stale or was not produced by the validator")

    return {
        "path": str(report_path),
        "sha256": sha256_bytes(report_path.read_bytes()),
        "elf": str(elf_path),
        "elf_sha256": elf_hash,
        "config": str(config_path),
        "config_sha256": config_hash,
    }


def require_ap_validation(
    report_path: Path,
    ap_path: Path,
    ap_raw: bytes,
    profile: dict[str, object],
) -> dict[str, object]:
    if not report_path.is_file():
        fail(f"AP L1 validation report does not exist: {report_path}")
    report = json.loads(report_path.read_text(encoding="utf-8"))
    if report.get("result") != "pass":
        fail("AP L1 validation report does not pass")
    if report.get("component") not in ("openvela-ap-dual-core-smp",
                                       "openvela-ap-single-core"):
        fail("AP L1 validation report has the wrong component identity")
    if Path(str(report.get("raw", ""))).resolve() != ap_path:
        fail("AP L1 validation report names a different raw image")
    if report.get("sha256") != sha256_bytes(ap_raw):
        fail("AP L1 validation report SHA-256 does not match app1.bin")
    if report.get("raw_length_bytes") != len(ap_raw):
        fail("AP L1 validation report length does not match app1.bin")

    execution = profile["ap_execution"]
    if report.get("flash") != {"start": "0x02160000", "end": "0x02400000"}:
        fail("AP L1 validation report has the wrong XIP window")
    if report.get("ram") != {
        "start": execution["sram_start"],
        "end": execution["sram_end"],
    }:
        fail("AP L1 validation report has the wrong RAM window")

    elf_path = Path(str(report.get("elf", ""))).resolve()
    if not elf_path.is_file():
        fail(f"AP L1 validation report ELF is missing: {elf_path}")
    elf_hash = sha256_bytes(elf_path.read_bytes())
    if report.get("elf_sha256") != elf_hash:
        fail("AP L1 validation report ELF SHA-256 does not match its ELF")
    config_path = Path(str(report.get("config", ""))).resolve()
    if not config_path.is_file():
        fail(f"AP L1 validation report config is missing: {config_path}")
    config_hash = sha256_bytes(config_path.read_bytes())
    if report.get("config_sha256") != config_hash:
        fail("AP L1 validation report config SHA-256 does not match its config")

    with tempfile.TemporaryDirectory(prefix="bk7258-ap-validation-") as temporary:
        regenerated_path = Path(temporary) / "l1-validation.json"
        result = subprocess.run(
            [
                sys.executable,
                str(Path(__file__).with_name("validate_bk7258_ap_image.py")),
                "--elf",
                str(elf_path),
                "--raw",
                str(ap_path),
                "--config",
                str(config_path),
                "--report",
                str(regenerated_path),
            ],
            text=True,
            capture_output=True,
            check=False,
        )
        if result.returncode != 0:
            fail(
                "AP L1 validation could not be reproduced:\n"
                f"{result.stdout}{result.stderr}"
            )
        regenerated = json.loads(regenerated_path.read_text(encoding="utf-8"))
        if regenerated != report:
            fail("AP L1 validation report is stale or was not produced by the validator")

    return {
        "path": str(report_path),
        "sha256": sha256_bytes(report_path.read_bytes()),
        "elf": str(elf_path),
        "elf_sha256": elf_hash,
        "config": str(config_path),
        "config_sha256": config_hash,
    }


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
        profile_name = profile.get("profile")
        if profile_name not in (PLACEHOLDER_PROFILE, OPENVELA_AP_PROFILE):
            fail(
                f"unsupported profile: {profile_name!r}; this packer supports "
                f"{PLACEHOLDER_PROFILE} and {OPENVELA_AP_PROFILE}"
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
        cp_path = args.cp_app_bin.resolve()
        cp_raw = require_cp(cp_path, partitions["cp"]["raw_max_bytes"])
        if args.cp_validation_report is not None:
            cp_validation = require_cp_validation(
                args.cp_validation_report.resolve(), cp_path, cp_raw, packaging_dir
            )
            cp_source = "openvela-component"
        elif profile_name == OPENVELA_AP_PROFILE:
            fail("the OpenVela AP profile requires --cp-validation-report")
        else:
            cp_validation = None
            cp_source = "unvalidated-test-fixture"
        if profile_name == OPENVELA_AP_PROFILE:
            if args.ap_app_bin is None:
                fail("the OpenVela AP profile requires --ap-app-bin")
            if args.ap_validation_report is None:
                fail("the OpenVela AP profile requires --ap-validation-report")
            ap_path = args.ap_app_bin.resolve()
            ap_raw = require_openvela_ap(
                ap_path, partitions["ap"], profile["ap_execution"]
            )
            ap_validation = require_ap_validation(
                args.ap_validation_report.resolve(), ap_path, ap_raw, profile
            )
            ap_source = "openvela-component"
        else:
            if args.ap_app_bin is not None or args.ap_validation_report is not None:
                fail("the placeholder profile does not accept OpenVela AP inputs")
            ap_path = packaging_dir / bundled["ap_placeholder"]["relative_path"]
            ap_raw = load_bundled(
                packaging_dir, bundled["ap_placeholder"], "ap_placeholder"
            )
            ap_source = "bundled-placeholder"
            ap_validation = None

        if len(ap_raw) > partitions["ap"]["raw_max_bytes"]:
            fail("AP component exceeds its raw profile capacity")

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
                    cp_source,
                    {
                        "path": str(cp_path),
                        "xip_vector_base": partitions["cp"]["xip_vector_base"],
                        **({"l1_validation": cp_validation} if cp_validation else {}),
                    },
                ),
                "ap": component_entry(
                    partitions["ap"],
                    ap_raw,
                    ap_source,
                    {
                        "path": str(ap_path),
                        "xip_vector_base": partitions["ap"]["xip_vector_base"],
                        **({"l1_validation": ap_validation} if ap_validation else {}),
                    },
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
            "flashable": cp_validation is not None,
            "hardware_status": (
                "host package validation only; OpenVela AP execution and heartbeat "
                "require a recorded BK7258 cold-boot run"
                if profile_name == OPENVELA_AP_PROFILE
                else "CP-only placeholder recovery profile"
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
