# BK7258 host tools

## Wrappers

These two cover build output through flashing, together with `build.sh` and
`minicom`; see the four-command loop in `../packaging/README.md`. Both locate the
OpenVela workspace by its `build.sh` + `nuttx/` markers, so they run from any
directory.

- `bk7258-package.sh` packages a built CP component and then verifies the result
  with the independent decoder, printing only the key facts. It reports success
  only if the decode passes, so an unverified image never reaches the loader.
- `bk7258-flash.sh` flashes a packaged image. It requires a passing
  `decode-report.json` beside the image and refuses to run while another process
  holds the serial port. The loader's full output goes straight to the terminal
  so erase and write progress is visible live; afterwards the script moves the
  loader's own log next to the image and fails unless that log contains
  `Writing Flash OK`. Needs root.

The console step is plain `minicom`, no wrapper:
`sudo minicom -o -D /dev/ttyUSB0 -b 115200`. Turn hardware and software flow
control off under `Ctrl-A O`; this DevKit does not wire RTS/CTS, and minicom's
built-in default enables it. Exit with `Ctrl-A X` before flashing again, since
minicom holds the port.

## Underlying tools

- `bk7258_container.py` implements the linear-CRC Flash container: per-partition
  32-byte payload blocks with a big-endian CRC16 each, `0xff` gap padding to
  physical offsets, a 34-byte pre-read tail and 32-byte image alignment. Both
  packers use it. `tests/test_bk7258_packaging.py` asserts it reproduces the
  locked SDK packer byte-for-byte.
- `package_bk7258.py` creates the L2 linear-CRC `all-app.bin` from the staged
  normal Bootloader, a CP raw `app.bin` and the profile-locked vendor AP raw
  `app1.bin`. Requires the external Armino SDK and `beken_genie` build tree.
- `package_bk7258_bundled.py` creates the same container from repository-bundled
  inputs only: the bundled Bootloader, a CP raw `app.bin` and the synthetic AP
  placeholder. Needs no external SDK or build tree.
- `make_ap_placeholder.py` regenerates `../packaging/bundled/ap-placeholder.bin`
  deterministically. Its output is locked by the bundled profile.
- `decode_bk7258_image.py` independently verifies section CRCs, physical
  offsets, padding, image hash and decoded CP/AP bytes. Works for both paths.
- `validate_bk7258_profile.py` verifies local SDK/build inputs and stages the
  normal Bootloader. It does not flash hardware.
- `validate_bk7258_cp_image.py` validates the CP ELF/raw vector, section and
  capacity contract.
- `bk_loader` is the committed Linux x86_64 Beken downloader, version 2.1.11.8.
  Its SHA-256 is
  `55221d83d5582c362aab27d8883d799acf5eaa7b735d51135e01b0a24156b9ab`.

The loader is used only after offline package/decode validation. `download`
performs its own reset and bus handshake, and a `LinkCheck Timeout` or
`GetBus fail` there occurs before the image is parsed, erased or written, so a
channel problem never leaves Flash half-written. A standalone `read` handshake is
still useful when diagnosing the channel itself without writing anything.

See `../packaging/README.md` for the full synchronized-workspace runbook.
