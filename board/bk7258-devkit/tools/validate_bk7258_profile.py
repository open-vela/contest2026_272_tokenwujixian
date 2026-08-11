#!/usr/bin/env python3
"""Validate and stage the locked BK7258 L2-dev vendor profile.

This tool intentionally uses only the Python standard library. It does not
encode a complete image, call the Beken downloader, modify vendor inputs, or
invoke the SDK's OTA/post-package flow.
"""

from __future__ import annotations

import argparse
import binascii
import hashlib
import json
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path


LOCAL_CMAKE_PATTERN = re.compile(
    r'^\s*set\(\s*(BK7258_(?:ARMINO_SDK_ROOT|BEKEN_GENIE_BUILD_ROOT))\s+"([^"]+)"\s*\)\s*$'
)
SIZE_PATTERN = re.compile(r"^(0x[0-9a-fA-F]+|\d+)([KkMm])?$")
REQUIRED_LOCAL_KEYS = {
    "BK7258_ARMINO_SDK_ROOT",
    "BK7258_BEKEN_GENIE_BUILD_ROOT",
}


@dataclass(frozen=True)
class VerifiedFile:
    label: str
    path: Path
    length: int
    sha256: str


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Validate and stage the locked BK7258 vendor packaging profile."
    )
    parser.add_argument(
        "--profile",
        type=Path,
        required=True,
        help="Path to bk7258-devkit-l0-vendor-ap.json.",
    )
    parser.add_argument(
        "--inputs-cmake",
        type=Path,
        required=True,
        help="Local-only CMake file that locates the external SDK and build tree.",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        required=True,
        help="Empty or managed directory for generated staging artifacts.",
    )
    parser.add_argument(
        "--write-report",
        action="store_true",
        help="Write validated-profile.json alongside staged-bootloader.bin.",
    )
    return parser.parse_args()


def fail(message: str) -> None:
    raise RuntimeError(message)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def parse_local_cmake(path: Path) -> dict[str, Path]:
    if not path.is_file():
        fail(f"local input file does not exist: {path}")

    values: dict[str, Path] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        match = LOCAL_CMAKE_PATTERN.match(line)
        if match:
            values[match.group(1)] = Path(match.group(2)).expanduser()

    missing = REQUIRED_LOCAL_KEYS.difference(values)
    if missing:
        fail(f"{path} is missing required keys: {', '.join(sorted(missing))}")

    for key, value in values.items():
        if not value.is_dir():
            fail(f"{key} is not an existing directory: {value}")

    return values


def parse_size(value: str | int) -> int:
    text = str(value)
    match = SIZE_PATTERN.match(text)
    if not match:
        fail(f"unsupported partition-size value: {value!r}")

    size = int(match.group(1), 0)
    suffix = match.group(2)
    if suffix in {"K", "k"}:
        size *= 1024
    elif suffix in {"M", "m"}:
        size *= 1024 * 1024
    return size


def fixed_string(value: str, length: int, label: str) -> bytes:
    encoded = value.encode("utf-8")
    if len(encoded) > length:
        fail(f"{label} exceeds {length} UTF-8 bytes: {value!r}")
    return encoded.ljust(length, b"\x00")


def serialize_partition_table(path: Path) -> bytes:
    try:
        partition_document = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        fail(f"cannot parse partition JSON {path}: {exc}")

    part_table = partition_document.get("part_table")
    if not isinstance(part_table, list) or not part_table:
        fail(f"{path} has no non-empty part_table array")

    encoded_table = bytearray()
    for index, part in enumerate(part_table):
        try:
            record = bytearray()
            record.extend(int(str(part["magic"]), 0).to_bytes(4, "little"))
            record.extend(fixed_string(str(part["name"]), 24, f"part_table[{index}].name"))
            record.extend(
                fixed_string(
                    str(part["flash_name"]),
                    24,
                    f"part_table[{index}].flash_name",
                )
            )
            record.extend(int(str(part["offset"]), 0).to_bytes(4, "little"))
            record.extend(parse_size(part["len"]).to_bytes(4, "little"))
        except (KeyError, OverflowError, ValueError) as exc:
            fail(f"invalid part_table[{index}] in {path}: {exc}")

        if len(record) != 60:
            fail(f"part_table[{index}] serialized to {len(record)} bytes, expected 60")

        record.extend((binascii.crc32(record) & 0xFFFFFFFF).to_bytes(4, "little"))
        encoded_table.extend(record)

    return bytes(encoded_table)


def verify_file(
    label: str,
    root: Path,
    entry: dict[str, object],
) -> VerifiedFile:
    relative_path = entry.get("relative_path")
    expected_hash = entry.get("sha256")
    expected_length = entry.get("raw_length_bytes", entry.get("length_bytes"))
    if not isinstance(relative_path, str) or not isinstance(expected_hash, str):
        fail(f"profile input {label} lacks relative_path or sha256")

    path = root / relative_path
    if not path.is_file():
        fail(f"locked profile input is missing: {label}: {path}")

    length = path.stat().st_size
    if expected_length is not None and length != expected_length:
        fail(
            f"{label} length mismatch: expected {expected_length}, got {length}: {path}"
        )

    digest = sha256_file(path)
    if digest != expected_hash:
        fail(f"{label} SHA-256 mismatch: expected {expected_hash}, got {digest}: {path}")

    return VerifiedFile(label, path, length, digest)


def verify_staged_copy(
    label: str,
    root: Path,
    entry: dict[str, object],
) -> VerifiedFile:
    staged_relative_path = entry.get("staged_relative_path")
    expected_hash = entry.get("sha256")
    expected_length = entry.get("raw_length_bytes")
    if not isinstance(staged_relative_path, str) or not isinstance(expected_hash, str):
        fail(f"profile staged input {label} lacks staged_relative_path or sha256")

    path = root / staged_relative_path
    if not path.is_file():
        fail(f"locked staged profile input is missing: {label}: {path}")

    length = path.stat().st_size
    if expected_length is not None and length != expected_length:
        fail(
            f"{label} length mismatch: expected {expected_length}, got {length}: {path}"
        )

    digest = sha256_file(path)
    if digest != expected_hash:
        fail(f"{label} SHA-256 mismatch: expected {expected_hash}, got {digest}: {path}")

    return VerifiedFile(label, path, length, digest)


def validate_package_layout(package_path: Path, profile: dict[str, object]) -> None:
    try:
        package = json.loads(package_path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        fail(f"cannot parse package JSON {package_path}: {exc}")

    if package.get("crc_enable") is not True:
        fail(f"{package_path} does not enable CRC packaging")

    sections = package.get("section")
    if not isinstance(sections, list):
        fail(f"{package_path} has no section array")

    expected = {
        "bootloader": profile["partitions"]["bootloader"],
        "app": profile["partitions"]["cp"],
        "app1": profile["partitions"]["ap"],
    }
    actual = {section.get("partition"): section for section in sections}
    if set(actual) != set(expected):
        fail(
            f"{package_path} partitions differ from locked profile: "
            f"expected {sorted(expected)}, got {sorted(actual)}"
        )

    for partition, expected_layout in expected.items():
        section = actual[partition]
        try:
            offset = int(str(section["start_addr"]), 0)
            size = parse_size(section["size"])
            expected_offset = int(str(expected_layout["physical_offset"]), 0)
            expected_size = int(str(expected_layout["physical_size"]), 0)
        except (KeyError, ValueError) as exc:
            fail(f"invalid {partition} section/layout: {exc}")

        if offset != expected_offset or size != expected_size:
            fail(
                f"{partition} layout mismatch: expected "
                f"offset 0x{expected_offset:08x}, size 0x{expected_size:x}; got "
                f"offset 0x{offset:08x}, size 0x{size:x}"
            )


def check_sdk_revision(sdk_root: Path, expected_revision: str) -> dict[str, object]:
    result = subprocess.run(
        ["git", "-C", str(sdk_root), "rev-parse", "HEAD"],
        text=True,
        capture_output=True,
        check=False,
    )
    if result.returncode != 0:
        fail(f"cannot read SDK Git revision at {sdk_root}: {result.stderr.strip()}")

    revision = result.stdout.strip()
    if revision != expected_revision:
        fail(f"SDK revision mismatch: expected {expected_revision}, got {revision}")

    status = subprocess.run(
        ["git", "-C", str(sdk_root), "status", "--porcelain"],
        text=True,
        capture_output=True,
        check=False,
    )
    if status.returncode != 0:
        fail(f"cannot inspect SDK worktree state at {sdk_root}: {status.stderr.strip()}")

    return {"revision": revision, "dirty": bool(status.stdout.strip())}


def stage_bootloader(
    normal_bootloader: VerifiedFile,
    partition_table: bytes,
    output_dir: Path,
    expected_staged: dict[str, object],
) -> VerifiedFile:
    output_dir.mkdir(parents=True, exist_ok=True)
    staged = normal_bootloader.path.read_bytes()
    staged += b"\x00" * ((-len(staged)) % 32)
    staged += partition_table

    staged_path = output_dir / "staged-bootloader.bin"
    staged_path.write_bytes(staged)

    expected_length = expected_staged.get("raw_length_bytes")
    expected_hash = expected_staged.get("sha256")
    digest = sha256_file(staged_path)
    if len(staged) != expected_length:
        fail(
            "staged Bootloader length mismatch: "
            f"expected {expected_length}, got {len(staged)}"
        )
    if digest != expected_hash:
        fail(
            "staged Bootloader SHA-256 mismatch: "
            f"expected {expected_hash}, got {digest}"
        )

    return VerifiedFile("staged_bootloader", staged_path, len(staged), digest)


def main() -> int:
    args = parse_args()
    try:
        profile = json.loads(args.profile.read_text(encoding="utf-8"))
        if profile.get("profile") != "bk7258-devkit-l0-vendor-ap":
            fail(f"unsupported profile: {profile.get('profile')!r}")
        if profile.get("container", {}).get("mode") != "linear-crc-physical-flash":
            fail("profile is not a linear-CRC physical-flash profile")

        local_inputs = parse_local_cmake(args.inputs_cmake)
        sdk_root = local_inputs["BK7258_ARMINO_SDK_ROOT"]
        build_root = local_inputs["BK7258_BEKEN_GENIE_BUILD_ROOT"]
        locked_inputs = profile["locked_inputs"]

        sdk_state = check_sdk_revision(sdk_root, locked_inputs["armino_sdk"]["revision"])
        verified = [
            verify_file("normal_bootloader", sdk_root, locked_inputs["normal_bootloader"]),
            verify_file("vendor_cp_fixture", build_root, locked_inputs["vendor_cp_fixture"]),
            verify_file("vendor_ap_input", build_root, locked_inputs["vendor_ap_input"]),
            verify_file("package_json", build_root, locked_inputs["package_json"]),
            verify_file("ota_partitions_json", build_root, locked_inputs["ota_partitions_json"]),
            verify_file(
                "vendor_all_app_recovery_fixture",
                build_root,
                locked_inputs["vendor_all_app_recovery_fixture"],
            ),
            verify_staged_copy(
                "vendor_ap_staged_copy",
                build_root,
                locked_inputs["vendor_ap_input"],
            ),
        ]
        for label in (
            "generated_partitions",
            "generated_ram_regions",
            "generated_tfm_hal_ppc",
            "generated_ppc_macros",
            "generated_cp_sdkconfig",
        ):
            verified.append(verify_file(label, build_root, locked_inputs[label]))
        validate_package_layout(verified[3].path, profile)

        for tool_name, expected_hash in profile["tool_fingerprints"].items():
            if tool_name == "bk_loader":
                continue
            tool_path = sdk_root / {
                "bk_crc16.py": "tools/env_tools/bk_py_libs/bk_crc/bk_crc16.py",
                "bk_packager_linear_crc.py": "tools/env_tools/bk_py_libs/bk_packager/bk_packager_linear_crc.py",
                "bk_packager_crc_decorator.py": "tools/env_tools/bk_py_libs/bk_packager/bk_packager_crc_decorator.py",
                "bk_packager_linear_linker.py": "tools/env_tools/bk_py_libs/bk_packager/bk_packager_linear_linker.py",
                "bk_ota_pack.py": "tools/build_tools/build_process/bk_sdk/bk_ota_pack.py",
            }[tool_name]
            actual_hash = sha256_file(tool_path)
            if actual_hash != expected_hash:
                fail(
                    f"packaging tool hash mismatch for {tool_name}: "
                    f"expected {expected_hash}, got {actual_hash}"
                )

        table = serialize_partition_table(verified[4].path)
        staged = stage_bootloader(
            verified[0],
            table,
            args.output_dir,
            locked_inputs["staged_bootloader"],
        )

        report = {
            "profile": profile["profile"],
            "sdk": sdk_state,
            "verified_files": [
                {
                    "label": item.label,
                    "path": str(item.path),
                    "raw_length_bytes": item.length,
                    "sha256": item.sha256,
                }
                for item in verified
            ],
            "serialized_partition_table_bytes": len(table),
            "staged_bootloader": {
                "path": str(staged.path),
                "raw_length_bytes": staged.length,
                "sha256": staged.sha256,
            },
        }
        if args.write_report:
            report_path = args.output_dir / "validated-profile.json"
            report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")

        print(json.dumps(report, indent=2))
        return 0
    except (KeyError, OSError, RuntimeError, TypeError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
