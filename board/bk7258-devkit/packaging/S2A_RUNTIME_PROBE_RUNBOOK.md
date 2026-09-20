# BK7258 S2A cold-start runtime probe

This procedure tests only the cold-start vendor init and RF-calibration path.
It must not start STA mode, scan, connect, or DHCP.

## Prepared image

Use only this retained L2 package:

```text
/home/czp/openvela_contest/cmake_out/bk7258-l2-s2a-runtime-probe-20260826/all-app.bin
```

Its SHA-256 is:

```text
f608988e6a99782b95e1363203481034bd90c84dd65fe872acfbe61b7561525d
```

The package contains the runtime-probe CP image (SHA-256
`4cd3a8856c8fa32e877e51a2a23e0c163a9bc8135bc3ace575bed6ea67e3335c`)
independent linear-CRC and component-byte validation.

## Pre-flash checks

1. Close minicom or every program holding the serial device.
2. Put the DevKit in Beken downloader mode.
3. Verify the package without touching hardware:

   ```bash
   board/bk7258-devkit/tools/bk7258-flash.sh --verify-only \
     --image /home/czp/openvela_contest/cmake_out/bk7258-l2-s2a-runtime-probe-20260826/all-app.bin
   ```

   Expected result:

   ```text
   package integrity gate: pass
   ```

## Flash (requires explicit operator approval)

This overwrites Flash from physical offset `0x0`, including Bootloader, CP,
and AP partitions.

```bash
sudo board/bk7258-devkit/tools/bk7258-flash.sh \
  --image /home/czp/openvela_contest/cmake_out/bk7258-l2-s2a-runtime-probe-20260826/all-app.bin
```

The loader log is moved next to the image. Require `Writing Flash OK` before
power-cycling. A `LinkCheck Timeout` or `GetBus fail` occurs before erase/write;
it indicates downloader transport or boot-mode trouble rather than a bad image.

## UART capture and cold boot

Power-cycle after a successful flash, then record the UART0 session. The
DevKit uses 115200 8N1; hardware and software flow control must be off.

```bash
sudo minicom -o -D /dev/ttyUSB0 -b 115200 \
  -C /home/czp/openvela_contest/cmake_out/bk7258-l2-s2a-runtime-probe-20260826/uart-s2a.log
```

At the NSH prompt, run exactly once:

```text
nsh> bk7258_wifi_runtime init
```

Do not run `ifup`, scan, ESSID/password operations, or any connect command in
this S2A experiment. Exit minicom with `Ctrl-A X` before another flash.

## Acceptance signals

Capture the whole UART log and classify the result using these markers:

| Signal | Interpretation |
| --- | --- |
| `[bk7258_wifi_runtime] init: begin` | Manual app invoked; no auto-start claim. |
| `factory xtal trim=58` | BK7258 vendor-cal factory trim reached the PHY ABI. |
| `AON ADC trim=<0..63>` and `AON bias trim=<0..63>` | Per-chip AON calibration fields were read. Record the values; zero is data, not automatic proof of failure. |
| `BK7258 SARADC] acquired`, `initialized`, `config`, `calibration bypass enabled`, `started` | PHY SARADC lifecycle reached hardware. |
| `BK7258 SARADC] read timeout` or `analog setup failed` | S2A fails: preserve the log; do not proceed to scan. |
| `runtime: bk_wifi_init complete` and `init: ret=0` | S2A init succeeds for this cold boot. It is not scan/association/DHCP evidence. |
| `runtime: bk_wifi_init failed=<n>`, HardFault/assert, or console loss | S2A fails. Preserve image SHA, loader log, and UART capture; do not retry init in the same boot. |

## Boundary after success

Even with a successful S2A run, the implementation has no validated in-place
MAC/PHY reset contract in CP's confirmed `0x44010000` secure SYS domain.
Repeat initialization, hot reset recovery, scan, association, carrier, and
DHCP remain out of scope until their own stage gates are completed.
