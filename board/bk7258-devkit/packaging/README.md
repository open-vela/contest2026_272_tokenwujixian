# BK7258 external packaging inputs

The BK7258 L2-dev package is assembled outside the NuttX component builds.
It combines a staged vendor Bootloader, a CP `app.bin`, and an explicit AP
`app1.bin` using the locked Beken linear-CRC profile.

The packer must load machine-local paths from:

```text
<openvela-workspace>/local/bk7258-vendor-inputs.cmake
```

The repository provides [bk7258-vendor-inputs.cmake.example](bk7258-vendor-inputs.cmake.example)
as the template. The real file is local-only: do not add it, vendor binaries,
SDK sources, keys, or generated headers to this repository.

`bk7258_vendor_inputs.cmake` supplies the L2 package target with
`bk7258_load_vendor_inputs()`. The helper is deliberately not called by L1 CP
or AP component builds. L2 packaging requires the real local file, validates
the two required directory variables, and then lets the profile/packer
validate the individual files and SHA-256 values.

The template is a path locator, not a profile selector. The component config
and locked profile choose the build contract; a future packer validates that
the assets found at these paths match the profile's recorded SHA-256 values.

The current L0 contract is stored in
[`profiles/bk7258-devkit-l0-vendor-ap.json`](profiles/bk7258-devkit-l0-vendor-ap.json).
It records no absolute vendor path or binary payload. It locks the required
relative inputs, raw lengths, SHA-256 values, partition layout, tool sources
and the required linear-CRC container behavior. The locked inputs include the
generated partition, RAM, PPC and SDK-config artifacts as one atomic profile;
do not pair the L0 package files with output from another Armino project.

The generated PPC files prove the locked vendor build's intended all-secure
peripheral policy, including SYS, UART0, TIMER0, FLASH, AON and GPIO. They do
not by themselves prove when the opaque Bootloader applies or preserves that
policy; the first Flash cold-boot test verifies the runtime state.

Before a complete-image packer may run, invoke
`../tools/validate_bk7258_profile.py` with the profile, local-only CMake input
file and a build-output staging directory. It verifies the locked SDK/build
inputs and produces only `staged-bootloader.bin` plus an optional validation
report. It neither CRC-encodes the staging file nor generates `all-app.bin`.

For example, from the team repository root, after creating the local-only
input file:

```bash
python3 board/bk7258-devkit/tools/validate_bk7258_profile.py \
  --profile board/bk7258-devkit/packaging/profiles/bk7258-devkit-l0-vendor-ap.json \
  --inputs-cmake ../local/bk7258-vendor-inputs.cmake \
  --output-dir cmake_out/bk7258-profile-staging \
  --write-report
```

This command is read-only with respect to external vendor inputs and hardware;
it writes only its requested output directory. It is an L2 profile gate, not
the complete-image packer and not a flashing command.

The L2-dev sequence is explicit and component-oriented:

1. Run `validate_bk7258_profile.py` to lock external inputs and stage the
   Bootloader.
2. Run `../tools/package_bk7258.py` with explicit CP `app.bin` and AP
   `app1.bin` paths. For the current L0 profile the AP path must match the
   locked vendor input exactly.
3. Run `../tools/decode_bk7258_image.py` with the generated manifest to
   independently recalculate each section CRC and compare decoded CP/AP bytes.

Both package tools use only Python's standard library plus the locked SDK's
`bk_packager_linear_crc` substep. They refuse to invoke OTA, downloader,
eFuse, OTP or other hardware operations.

The CP CMake L1 export invokes `../tools/validate_bk7258_cp_image.py` as a
build gate. Its report is `bk7258/l1-validation.json` and checks the ELF/raw
vector base, MSP, Thumb reset target, `.data` LMA/VMA, `.bss` range and raw
capacity plus the locked idle-stack reservation before an L2 package is
created.

Run the complete offline regression from the team repository root before using
a new packer/profile revision:

```bash
python3 board/bk7258-devkit/tests/test_bk7258_packaging.py \
  --profile board/bk7258-devkit/packaging/profiles/bk7258-devkit-l0-vendor-ap.json \
  --inputs-cmake ../local/bk7258-vendor-inputs.cmake
```

The regression reconstructs the locked vendor `all-app.bin` byte-for-byte,
then substitutes 64-byte CP and AP fixtures separately. It verifies each
fixture's two payload blocks and CRC16 positions and proves that the
unreplaced Bootloader/component range remains unchanged. The fixture package
uses the locked SDK primitive directly because the production L0 wrapper
correctly rejects a non-locked AP input.

## Verified CPU0 L0 boundary

The L2 packaging and independent decode path are verified offline. On
2026-08-11, a CPU0 replacement L2 image was also flashed to a BK7258 DevKit
and cold-booted to `NuttShell (NSH)` / `nsh>`. This confirms the normal
Bootloader + CRC container path for the observed CPU0 console bring-up only.
It does not validate the preserved vendor AP payload, its release state,
peripherals, networking, USB, or sustained operation. Record the exact image
SHA-256, loader command and UART log for every further hardware experiment in
the CPU0 bring-up contract.

## End-to-end CPU0 L0 Runbook

This section is the reproducible path after synchronizing the team repository
into an OpenVela workspace. It builds and validates the experimental CPU0 L0
image; it does not claim AP, network, USB, peripheral or stability acceptance.

### 1. Provision local-only vendor inputs

The repository intentionally does not contain the Armino SDK, normal
Bootloader, generated partition files, vendor AP payload, keys or generated
vendor headers. Obtain the exact external assets that match the locked profile,
then create the local-only locator from the workspace root:

```bash
cd <openvela-workspace>
mkdir -p local
cp contest2026_272_tokenwujixian/board/bk7258-devkit/packaging/bk7258-vendor-inputs.cmake.example \
  local/bk7258-vendor-inputs.cmake
```

Set `BK7258_ARMINO_SDK_ROOT` and `BK7258_BEKEN_GENIE_BUILD_ROOT` in that copied
file. Do not commit it. The profile gate verifies the SDK revision and all
locked external input hashes before packaging; a different SDK/build tuple is
expected to fail.

### 2. Build the CP component

```bash
cd <openvela-workspace>

./build.sh vendor/beken/boards/bk7258/bk7258-devkit/configs/cp/ \
  --cmake -b cmake_out/bk7258-devkit_cp/ -j12
```

Success requires `bk7258/l1-validation.json` to report `"result": "pass"`.
`bear --` may wrap this command when a local compilation database is desired,
but it is not required to build, package or flash the image.
The component inputs are then:

```text
cmake_out/bk7258-devkit_cp/bk7258/app.bin
cmake_out/bk7258-devkit_cp/bk7258/nuttx.elf
cmake_out/bk7258-devkit_cp/bk7258/nuttx.map
```

### 3. Run the packaging regression

```bash
cd <openvela-workspace>/contest2026_272_tokenwujixian

python3 board/bk7258-devkit/tests/test_bk7258_packaging.py \
  --profile board/bk7258-devkit/packaging/profiles/bk7258-devkit-l0-vendor-ap.json \
  --inputs-cmake ../local/bk7258-vendor-inputs.cmake
```

All four baseline/replacement checks must print `pass` before using a new
profile or packer revision.

### 4. Package and independently decode L2

```bash
cd <openvela-workspace>

BK7258_BUILD_ROOT=<path-from-local-input-file>
BK7258_OUTPUT_DIR="cmake_out/bk7258-l2-$(date +%Y%m%d-%H%M%S)"

python3 contest2026_272_tokenwujixian/board/bk7258-devkit/tools/package_bk7258.py \
  --profile contest2026_272_tokenwujixian/board/bk7258-devkit/packaging/profiles/bk7258-devkit-l0-vendor-ap.json \
  --inputs-cmake local/bk7258-vendor-inputs.cmake \
  --cp-app-bin cmake_out/bk7258-devkit_cp/bk7258/app.bin \
  --ap-app-bin "$BK7258_BUILD_ROOT/bk7258_ap/app.bin" \
  --output-dir "$BK7258_OUTPUT_DIR"

python3 contest2026_272_tokenwujixian/board/bk7258-devkit/tools/decode_bk7258_image.py \
  --image "$BK7258_OUTPUT_DIR/all-app.bin" \
  --manifest "$BK7258_OUTPUT_DIR/manifest.json" \
  --compare-cp cmake_out/bk7258-devkit_cp/bk7258/app.bin \
  --compare-ap "$BK7258_BUILD_ROOT/bk7258_ap/app.bin" \
  --report "$BK7258_OUTPUT_DIR/decode-report.json"
```

The decoder must report `"result": "pass"`. Preserve `all-app.bin`,
`manifest.json` and `decode-report.json` together. Never write raw CP
`app.bin` to physical offset `0x11000`.

### 5. Confirm the downloader channel before any write

`tools/bk_loader` is the Linux Beken loader version 2.1.11.8, pinned by this
repository to SHA-256
`55221d83d5582c362aab27d8883d799acf5eaa7b735d51135e01b0a24156b9ab`.

Place the DevKit in the documented downloader-accessible state, then perform a
read-only handshake first:

```bash
cd <openvela-workspace>

BK7258_PROBE_DIR=$(mktemp -d -t bk7258-read.XXXXXX)
BK7258_PROBE_FILE="$BK7258_PROBE_DIR/flash0.bin"
: > "$BK7258_PROBE_FILE"

sudo contest2026_272_tokenwujixian/board/bk7258-devkit/tools/bk_loader read \
  -p 0 -b 1500000 -s 0-100 -i "$BK7258_PROBE_FILE"
```

Proceed only after the log includes `Gotten Bus...`, `Current Chip is :
BK7236`, `Current baudrate : 1500000 success` and `Read Flash OK`. A
`LinkCheck Timeout` / `GetBus fail` is a downloader/USB/boot-mode problem that
occurs before the image is parsed or Flash is changed.

### 6. Flash the complete image

This action overwrites the normal Bootloader, CP and AP areas from physical
Flash offset `0x0`. Retain the locked vendor recovery `all-app.bin` before
continuing.

```bash
cd <openvela-workspace>

sha256sum "$BK7258_OUTPUT_DIR/all-app.bin"

sudo contest2026_272_tokenwujixian/board/bk7258-devkit/tools/bk_loader download \
  -p 0 -b 1500000 -s 0 \
  -i "$BK7258_OUTPUT_DIR/all-app.bin"
```

The expected write evidence includes `WriteFlash ->pass` and `Writing Flash
OK`. `-s 0` is mandatory for the complete L2 image.

### 7. Validate UART0 after reboot

```bash
sudo minicom -o -D /dev/ttyUSB0 -b 115200
```

Use 115200 8N1 with hardware flow control **No** and software flow control
**No**. The observed CPU0 acceptance so far is `BK`, `NuttShell (NSH)`,
`nsh>`, and input of `?`. The UART TX FIFO-drain fix still requires the
long-output test: one `?` or `help` command must print its entire output
without another keypress. Also record `uname`, `ps`, cold-reboot count and any
fault log before claiming T7 complete.
