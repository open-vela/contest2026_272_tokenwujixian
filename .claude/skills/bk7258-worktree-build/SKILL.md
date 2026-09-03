---
name: bk7258-worktree-build
description: "可复现地编译、打包、独立解码并全自动烧录 BK7258 worktree。Use when working in a BK7258 feature worktree, running the CP/AP build-to-flash flow, packaging all-app.bin, debugging build.sh paths, or needing the automatic reboot-bootloader flash (manual-reset fallback prompts during GetBus). Do not use for generic OpenVela boards or raw app.bin flashing."
---

# BK7258 Worktree Build

## Complete workflow

Prefer the bundled end-to-end wrapper. Without `--flash` it performs the safe
build, L1 validation, package, independent decode, and package-integrity gate:

```bash
<skill-dir>/scripts/full_flow.sh \
  --workspace <openvela-workspace> \
  --worktree <feature-worktree> \
  --jobs 12
```

The wrapper derives one stable slot per worktree under
`cmake_out/bk7258-worktrees/`. Its `build/cp` and optional `build/ap` directories
are reused for normal Vela incremental builds, and its `package` directory is
safely replaced on each successful package run. A binding file ties the slot to
the canonical workspace/worktree paths, so another worktree cannot reuse it.
If a component defconfig changes, only that component's generated build cache
is recreated. Use `--fresh` to deliberately recreate this worktree's generated
build cache. Use `--slot NAME` only to choose a stable human-readable slot.

To flash, rerun in an interactive terminal and explicitly add `--flash`:

```bash
<skill-dir>/scripts/full_flow.sh \
  --workspace <openvela-workspace> \
  --worktree <feature-worktree> \
  --flash --device /dev/ttyUSB0
```

To flash an existing verified package without rebuilding or requiring a
workspace/worktree, give the image and the board tools directory explicitly:

```bash
<skill-dir>/scripts/full_flow.sh \
  --image /absolute/path/to/all-app.bin \
  --tools-dir /absolute/path/to/board/bk7258-devkit/tools \
  --flash --device /dev/ttyUSB0
```

Image-only mode requires `manifest.json` and `decode-report.json` next to the
image. It runs the same independent integrity gate and automatic flash wrapper
but skips build/package. `--worktree <feature-worktree>` may replace
`--tools-dir`; the wrapper then derives `board/bk7258-devkit/tools` from that
worktree.

Flashing is destructive and must use an interactive terminal. The PTY wrapper
streams the loader output without buffering and drives the reset itself: once
the loader prints `connect success`, the wrapper opens the same serial device
as a second writer and injects `reboot bootloader` into the running NSH
console. Target firmware carrying the bootloader-reset patch
(`feature/bk7258-bootloader-reset`, `board_reset`
`ENTER_BOOTLOADER` via AON WDT) then performs a chip-level reset with the
vendor warm-jump tag cleared and the reset reason recorded as watchdog, so
the BootROM runs the cold chain and BL2 opens the download listen window
that the already-polling loader catches by itself -- no manual reset.
`Gotten Bus...` produces the handshake-success banner. If the automatic
reset does not engage within a few seconds (firmware without the patch, a
hung console), the wrapper retries once and then falls back to bell-and-
prompt manual resets, numbered 1, 2, ...; press reset on each prompt and
stop after the success banner. Never press reset after `Begin EraseFlash`,
and never type into the terminal during the automatic phase.

The public workflow assumes a trusted, current-user-owned workspace and Git
worktree. It does not defend against malicious processes running as the same
Unix user, SIGKILL, power loss, or tools that ignore its mapping lock. It never
runs worktree scripts through `sudo` and refuses to
run as root. Give the normal user access to the serial device through the
platform's serial-device group (commonly `dialout`). Before writing, the
wrapper verifies the audited `bk_loader` SHA-256, prints the complete image
SHA-256. The explicit `--flash` flag is the authorization switch. The agent
must still announce the target device and complete image hash in commentary
immediately before launching it.

When an agent launches the flash stage for the user, it must first announce in
commentary the target device, the complete image SHA-256, and that the wrapper
injects `reboot bootloader` automatically (manual-reset fallback prompts
appear only if the auto reset does not engage), then run the command with
interactive terminal support. Never launch `--flash` in the background,
through redirected output, or in a non-interactive shell. Keep the turn open
until `Writing Flash OK` or a clear failure is reported.

The existing board scripts remain the authority: `bk7258-package.sh` must
produce `decode pass`, and `bk7258-flash.sh` must reproduce the report, validate
all hashes, save the loader log, and find `Writing Flash OK`.

## Scope

This workflow is for a BK7258 team-repository worktree whose sources are
linked into the parent OpenVela workspace. The parent workspace is the
directory containing both `build.sh` and `nuttx/`; it is not the team-repository
checkout and not the worktree directory itself.

Never build a BK7258 worktree by passing `board/bk7258-devkit/configs/cp` to
`build.sh`. That path is valid for repository-local Python tools, but
`build.sh`/`lunch` resolves board configs through the manifest path under
`vendor/beken/boards/...`.

## 1. Establish the worktree mapping

For normal worktree builds, use the bundled wrapper; it serializes access to
the shared mapping and restores both links on exit:

```bash
<skill-dir>/scripts/build_worktree.sh \
  --workspace <openvela-workspace> \
  --worktree <feature-worktree> \
  --component cp \
  --jobs 12
```

Use `--component ap` for AP. Use `--output /absolute/output/path` only for an
isolated build whose artifacts will be passed explicitly to packaging.

From the parent OpenVela workspace, verify the mapped paths:

```bash
readlink -f vendor/beken/chips/bk7258
readlink -f vendor/beken/boards/bk7258/bk7258-devkit
```

For a feature worktree, both targets must resolve into that worktree:

```text
<worktree>/chips/bk7258
<worktree>/board/bk7258-devkit
```

If they point at another checkout, stop and resolve the mapping before
building. In a shared workspace, changing symlinks is visible to other
processes. Prefer a serialized build, save both original link targets, and
restore them in a shell `trap` even when the build fails. Do not delete the
symlink targets or use a broad recursive cleanup.

The project manifest is the source of truth for the normal mapping. The build
tree consumes the mapped `vendor/` paths, while packaging scripts may be run
from the worktree because they locate the workspace by the `build.sh` and
`nuttx/` markers.

## 2. Configure and build CP/AP

Run from the parent workspace. Use the mapped path and keep the trailing slash:

```bash
./build.sh vendor/beken/boards/bk7258/bk7258-devkit/configs/cp/ --cmake -j12
./build.sh vendor/beken/boards/bk7258/bk7258-devkit/configs/ap/ --cmake -j12
```

For a CP-only or Wi-Fi-disabled platform change, the first command is the
minimum build. Confirm the generated `.config` does not enable Wi-Fi/WPA when
the acceptance requires a normal CP configuration with Wi-Fi disabled.

`build.sh` derives output names from the board/config selection. Do not add a
random `-b` directory unless the task explicitly needs isolation:

```text
cmake_out/bk7258-devkit_cp/bk7258/app.bin
cmake_out/bk7258-devkit_cp/bk7258/nuttx.elf
cmake_out/bk7258-devkit_cp/bk7258/l1-validation.json
cmake_out/bk7258-devkit_ap/bk7258_ap/app1.bin
cmake_out/bk7258-devkit_ap/bk7258_ap/l1-validation.json
```

The normal build command supplies the project environment and compiler wrapper.
Direct `cmake --build` can fail with missing `ccache` or stale source paths and
is not equivalent evidence.

## 3. Package and independently decode

The default flow builds and packages the real CP plus OpenVela AP AMP image.
The bundled AP placeholder is recovery-only and must be selected explicitly
with `--placeholder`; it is expected to fail CPU1 AP release validation at
runtime.

For the repository-bundled recovery package, the project wrapper is the
preferred path:

```bash
<worktree>/board/bk7258-devkit/tools/bk7258-package.sh
```

It reads the CP/AP outputs, selects the real OpenVela AP profile, and runs the
independent decoder. The wrapper must print `decode       pass` before an image
is considered valid.

The default package slot intentionally replaces its previous package after the
new CP/AP build and L1 validation pass. `all-app.bin`, `manifest.json`, and
`decode-report.json` remain bound and are verified together before flashing.
Copy that trio elsewhere explicitly when a run must be preserved as historical
hardware evidence.

The package wrapper validates both L1 reports, invokes the container packer,
and independently decodes the final linear-CRC image. Do not flash `app.bin`
or `app1.bin` directly.

## 4. Offline regression checks

Run from the parent workspace or pass paths explicitly:

```bash
python3 <worktree>/board/bk7258-devkit/tests/test_bk7258_bundled_packaging.py
```

The decoder must produce the same report for relative and absolute input paths.
The report normalizes `image.path` with `Path.resolve()`; do not regress this
to the caller's spelling of the path, because evidence hashes and reports must
be stable across invocation directories.

For the vendor-input profile, first validate the machine-local input file and
then run the profile/package test described in
`board/bk7258-devkit/packaging/README.md`. Never invent vendor input paths or
copy credentials into the repository.

## 5. Troubleshooting

| Symptom | Cause | Action |
|---|---|---|
| `No config file found at` | `build.sh` received `board/...` instead of mapped `vendor/...` path, or the path lacks the expected config directory | Run from the parent workspace with `vendor/beken/boards/bk7258/bk7258-devkit/configs/cp/` or `ap/` and verify `readlink -f` |
| Early `grep: dor/.../defconfig: No such file` followed by a valid `lunch`, build, and L1 pass | `build.sh` has a non-fatal path-slicing check before `lunch` for full mapped paths | Judge the final command result; do not treat this line alone as failure. A later `No config file found at` or nonzero build result is still a real failure |
| Output builds the wrong source checkout | Shared `vendor/` symlink points at another worktree | Save/replace the two BK7258 links for the build, serialize the operation, and restore them with a trap |
| Image size/config unexpectedly changes while build still passes | A standard CMake output directory retained another worktree's `.config` | Use `full_flow.sh`; its binding prevents cross-worktree cache reuse, and defconfig changes rebuild only the affected component slot |
| `ccache: not found` from direct CMake | Bypassed the environment setup used by `build.sh` | Re-run through `./build.sh ... --cmake` |
| CP image not found during packaging | CP build was not successful or used a custom output directory | Check `cmake_out/bk7258-devkit_cp/bk7258/app.bin`, its L1 report, and pass `--cp-app-bin` when using `-b` |
| Decoder report differs by working directory | Decoder received relative input and stored it verbatim | Use the current decoder that resolves `args.image`; run the path-stability regression |
| Too many generated directories | A one-shot workflow was used instead of the stable bound slot | Use the default `full_flow.sh`; it incrementally reuses one build slot and replaces one package slot per worktree |
| `Getting Bus...` keeps printing but `Gotten Bus...` never appears, then the manual-reset banner shows up | The automatic `reboot bootloader` did not engage: target firmware lacks the bootloader-reset patch, its console was hung, or the injection window was missed | Press reset on each numbered bell/banner from `notify_flash.py`; stop resetting after `Gotten Bus...`. Deploy `feature/bk7258-bootloader-reset` firmware for the automatic path |
| No reset prompt and no auto-reset banner are visible | Flash was launched without a PTY or through a buffered pipe | Run `full_flow.sh --flash` in an interactive terminal; do not redirect loader stdout |
| `GetBus fail` or `LinkCheck Timeout` | Reset timing, USB, downloader mode, or a busy serial port prevented handshake; Flash has not been erased yet | Close serial monitors and rerun the interactive flash stage |
| Flash refuses root or reports serial permission denied | Public workflow does not execute mutable worktree code as root | Add the user to the serial-device group, re-login, and run without `sudo` |
| Loader SHA-256 mismatch | The bundled proprietary loader differs from the audited repository version | Stop; review the binary change and update the pinned hash only through code review |

## Acceptance boundary

A successful host build proves compilation and L1 image validation only. A
passing package/decode proves the linear-CRC container contract. Hardware IRQ,
register readback, GPIO waveform, cold boot, and UART behavior require separate
target evidence and must not be claimed from this workflow alone.
