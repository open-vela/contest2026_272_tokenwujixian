---
feature: bk7258-armino-otp-mb-ipc
status: in-progress
updated: 2026-09-07
branch: bk7258-hal-alignment
commits: 112a116c2d6cb7ee666a2bd0e221173f7462ec3b..HEAD
---

# BK7258 Armino OTP and mailbox IPC authority import

## Report

## [S1] Problem

The present BK7258 port substitutes hand-written OTP and mailbox shims for parts of the complete Armino SDK. Its OTP implementation has only a read-side approximation and deliberately fails all update APIs. Its PMOP token exchange is a local START/END doorbell, not Armino mailbox channel or mb_ipc RPC. Consequently bk_phy_server.c, saradc_server.c, their client protocols, and the authority multi-core feature gates cannot be truthfully enabled.

The authority for this feature is exclusively /home/czp/armino/bk_avdk_smp/cp, including middleware/soc/bk7258/bk7258.defconfig, generated OTP maps from the BK7258 board CSVs, and corresponding driver/component sources. Generated iperf configuration is not an authority source.

## [S2] Authority-preserving OTP driver

Import the complete source closure headed by Armino middleware/driver/otp/otp_driver_v1_1.c into chips/bk7258/otp/ using authority module layout and provenance. The imported driver, generated OTP map, OTP HAL, and BK7258 OTP LL code own the OTP API; NuttX code may adapt only MMIO, locking, delay, allocation, and build-integration boundaries. No hand-written replacement implementation may claim the same OTP API.

The public behavior must include the authority read, read-by-offset, permission, mask, update, and permission/mask programming APIs. Update paths must retain authority validation: item range, effective permission, size, monotonic zero-to-one bit transition, hardware program operation, and readback verification. No local runtime refusal stub is permitted.

The normal BK7258 Wi-Fi profile must preserve authority configuration: CONFIG_OTP_V1=y and CONFIG_PHY_RFCALI_TO_OTP=n. Thus ordinary PHY initialization exposes the same update function pointers as authority but does not elect RF-calibration persistence. Explicit authority-equivalent CLI, ATE, or provisioning callers retain their authority behavior. This feature does not flash hardware or invoke an OTP write test.

## [S3] Armino mb_ipc over the BK7258 Vela transport

Import authority mailbox-channel and mb_ipc protocol definitions and implement their required semantics over the Vela CP/AP communication substrate. The implementation must preserve source/destination CPU and port identity, logical channel ownership, connect/disconnect lifecycle, user command, payload length, CRC validation, reply/error behavior, timeout, and per-connection state. RPMsg/RPTUN may be the transport, but it must not replace this protocol with an ad-hoc token scheme.

The implementation must preserve the distinct short mailbox-channel START/END transaction for PHY/SARADC: authority command layout, request/ACK progression, CPU1-powered-off exemption, and bounded 5 ms ACK condition. It is independent of the mb_ipc socket RPC protocol.

## [S4] PHY server/client service

Port authority bk_phy_ipc.h ABI and bk_phy_server.c behavior. CPU0 owns PHY_SERVER (CPU0/port 3); the CPU1 client uses PHY_CLIENT (CPU1/port 17). The service must implement GET_TEMP, GET_VOLT, GET_MAC_ADDR, GET_STA_MAC_ADDR, GET_AP_MAC_ADDR, and SET_MAC_ADDR, with a complete phy_cmd_t reply or an explicit error reply for malformed/unknown commands. Server lifecycle, two connection slots, and deinitialization must follow authority semantics.

## [S5] SARADC server/client service

Port authority saradc_ipc.h ABI and saradc_server.c behavior. CPU0 owns SARADC_SERVER (CPU0/port 2); CPU1 uses SARADC_CLIENT (CPU1/port 16). All authority SARADC commands must retain request/reply semantics. READ and READ_RAW must retain CRC calculation and the additional READ_DONE/READ_RAW_DONE two-way acknowledgement before final completion. Server request handling must hold/release the authority-equivalent SARADC PM sleep vote around the command.

## [S6] Configuration transition and evidence

Do not enable multi-core authority macros independently. Enable CONFIG_CPU_CNT, CONFIG_PHY_MB, and CONFIG_SARADC_MB together only after the OTP closure and both protocol services compile and host contract tests prove the required ABI and lifecycle. Bluetooth remains off for the Wi-Fi-only profile (CONFIG_BLUETOOTH=0); it is not a substitute for missing mailbox services.

Acceptance requires fresh CP and AP L1 build reports, host protocol tests, and a package made with the repository BK7258 package/decode flow. Package/decode evidence is not hardware evidence. Flashing, OTP programming, UART capture, and cold-boot Wi-Fi acceptance require separately authorized board work.

## [S7] Out of Scope

- Bluetooth/BLE/coexistence import and CONFIG_BLUETOOTH=1.
- CPU2 startup or claiming the Vela two-image topology equals the authority three-core runtime without separately importing/verifying CPU2 support.
- Flashing any device, exercising an irreversible OTP program operation, or claiming hardware Wi-Fi/ACK success from host builds.
- Refactoring unrelated existing dirty worktree changes.

## Tasks

- [ ] T1: Replace the local OTP read-only implementation with the complete Armino OTP driver/HAL/LL/generated-map closure and remove duplicate OTP API shims — acceptance: every authority public OTP API has authority validation/program/readback behavior in the imported closure; no duplicate bk_otp provider remains (covers: S2).
- [ ] T2: Import mailbox_channel and mb_ipc protocol definitions and implement their contract over the Vela CP/AP transport — acceptance: host protocol tests prove port IDs, connect/disconnect, CRC, length, reply/error, timeout, and independent START/END ACK semantics (covers: S3; depends: T1).
- [ ] T3: Import and integrate authority PHY IPC ABI and CPU0 server with CPU1 client coverage — acceptance: host integration test exchanges every phy_cmd_t command and verifies a full reply or required error reply (covers: S4; depends: T2).
- [ ] T4: Import and integrate authority SARADC IPC ABI/server/client — acceptance: host integration test verifies command replies, READ/READ_RAW CRC and DONE handshakes, and PM sleep-vote bracketing (covers: S5; depends: T2).
- [ ] T5: Atomically activate only authority macro paths whose providers are implemented, then build and package — acceptance: CP/AP L1 reports pass, host tests pass, and bk7258-package.sh --openvela-ap reports independent decode pass (covers: S6; depends: T1, T2, T3, T4).
