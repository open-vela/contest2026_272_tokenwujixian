# BK7258 host tools

- `package_bk7258.py` creates the L2 linear-CRC `all-app.bin` from the staged
  normal Bootloader, a CP raw `app.bin` and the profile-locked vendor AP raw
  `app1.bin`.
- `decode_bk7258_image.py` independently verifies section CRCs, physical
  offsets, padding, image hash and decoded CP/AP bytes.
- `validate_bk7258_profile.py` verifies local SDK/build inputs and stages the
  normal Bootloader. It does not flash hardware.
- `validate_bk7258_cp_image.py` validates the CP ELF/raw vector, section and
  capacity contract.
- `bk_loader` is the bundled Linux x86_64 Beken downloader, version 2.1.11.8.
  Its SHA-256 is
  `55221d83d5582c362aab27d8883d799acf5eaa7b735d51135e01b0a24156b9ab`.

The loader is used only after offline package/decode validation. Always run a
read-only `Read Flash OK` handshake before `download`; a `LinkCheck Timeout`
or `GetBus fail` occurs before the image is parsed, erased or written.

See `../packaging/README.md` for the full synchronized-workspace runbook.
