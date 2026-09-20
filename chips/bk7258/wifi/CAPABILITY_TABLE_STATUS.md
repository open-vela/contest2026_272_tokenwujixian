# BK7258 Wi-Fi capability-table status

`wifi_os_funcs_t` and `wifi_os_variable_t` are ABI contracts with the pinned
`libwifi.a`. They use the generated Armino declarations unchanged. This file
tracks the current STA-only runtime bring-up boundary and whether the NuttX
implementation is allowed to enter `bk_wifi_init()`.

## Current status (2026-08-26)

- The capability owner is `bk7258_wifi_adapter.c`; the generated ABI layout is
  preserved and the OSAL/packet entries are populated.
- Both the CP and `runtime-probe` configurations now set
  `CONFIG_BK7258_WIFI_VENDOR_RUNTIME=y` and
  `CONFIG_BK7258_WIFI_RUNTIME_APP=y`. Wi-Fi still must not auto-start, and
  does not: the only caller of `bk7258_wifi_initialize()` is the
  `bk7258_wifi_runtime` application, invoked by hand from NSH. Board
  bring-up contains no call site.
- `CONFIG_BK7258_WIFI_RUNTIME_APP` gates the application in its CMakeLists
  and defaults to `n`, so every configuration that wants the command must
  set it explicitly; enabling `VENDOR_RUNTIME` alone is not sufficient.
- The functional target is STA only: scan, WPA/EAPOL association, connected
  state, disconnect, and later NuttX DHCP. SoftAP, P2P, WPS, monitor, and
  STA+AP concurrency are disabled and out of this milestone.
- Runtime-on source integration follows the Armino non-P2P source manifest,
  with explicit per-source wrappers for the STA WPA closure. The S1
  `runtime-probe` now completes final link and produces a validated CP raw
  image. This proves the selected init-path ABI/provider closure only; it is
  not evidence of `bk_wifi_init()` running on hardware.

## Rules

- Do not trim, reorder, or locally redefine either ABI structure.
- A `NULL` callback is allowed only where the generated table and the pinned
  vendor profile explicitly permit it. It is not a link-closure mechanism.
- Packet ownership crosses the NuttX boundary through a vendor-shaped packet
  container and an explicit RX/TX bridge. NuttX packet internals are not
  treated as Armino `struct pbuf`.
- The runtime remains disabled until every row required by the selected
  runtime path is callable.

## P0: kernel primitives

| ABI area | NuttX provider | State | Runtime restriction |
| --- | --- | --- | --- |
| malloc/free/realloc/zalloc | `bk7258_wifi_osal.c`, `os_shim.c` | callable | none |
| memory/string formatting | `os_shim.c`, NuttX libc | callable | none |
| logging/assertion | `system_shim.c`, NuttX syslog/assert | callable | none |
| critical section | `bk7258_wifi_osal.c` | callable | none |

## P1: scheduling primitives

| ABI area | NuttX provider | State | Runtime restriction |
| --- | --- | --- | --- |
| semaphore/mutex | `bk7258_wifi_osal.c` | callable | validate vendor timeout paths |
| queue | `bk7258_wifi_osal.c` | callable | validate message ownership |
| thread | `bk7258_wifi_osal.c` | callable | validate priority/stack contract |
| timer/workqueue/HISR | `rtos_compat_shim.c`, `rtos_ext_shim.c`, `bk_workqueue_nuttx.c` | callable in host build | live cancellation/ISR behavior still needs board validation |

## P2: verified hardware primitives

| ABI area | NuttX provider | State | Runtime restriction |
| --- | --- | --- | --- |
| MAC/PHY clock and power | `bk7258_wifi_hw.c`, `hw_driver_shim.c` | callable | reset remains unsupported |
| base MAC read | board `SYS_NET` read-only path | callable | no synthetic MAC |
| IRQ register/mask semantics | BK7258 IRQ + hw shim | callable in host build | live MAC IRQ path not validated |
| RTC/32K | `platform_shim.c` reads BK7258 AON RTC `counter_val[63:0]` and PMU R41 LPO source | partial | The callback uses the Armino stable low/high snapshot and returns the live 32K source/ticks-per-ms. It never initializes, resets, enables, or switches the shared RTC/mux; a verified PM owner is still required for those operations. |

## P3: packet and network boundary

| ABI area | NuttX provider | State | Runtime restriction |
| --- | --- | --- | --- |
| vendor packet layout/reserve | `bk7258_wifi_packet.c`, pbuf shim | implemented with 708-byte profile reserve | allocation behavior still needs closed-library validation |
| pbuf alloc/ref/free/header | `pbuf_shim.c` | implemented | no zero-copy claim |
| RX/TX bridge | lower-half + packet bridge | implemented by explicit copy | live descriptor/refill path not validated |
| EAPOL/WAI routing | vendor WPA/sk_intf path | source-integrated, not runtime-validated | blocks association until WPA closure |

## P4: hardware calibration and power management

| ABI area | NuttX provider | State | Runtime restriction |
| --- | --- | --- | --- |
| DMA/cache | BK7258 BSP | SRAM memcpy path present | coherency and live descriptor behavior not validated |
| reset/PM/RF sleep | BK7258 BSP | partial | reset remains unsupported; stable runtime blocked. The visible legacy reset magic targets another SYS address domain and is not used on CP's verified `0x44010000` secure alias. |
| crystal trim | `g_default_xtal` from BK7258 `vnd_cal` board table | factory-default fallback implemented (`0x3a`) | not per-unit calibration; OTP/SYS_RF tagged-record format remains unverified |
| SARADC | `glue/bk7258_saradc.c` | PHY-calibration lifecycle implemented with BK7258 MMIO and polling reads | No board measurement yet; raw bypass output is intentional and must not be double-calibrated |
| AON PMU calibration reads | `aon_pmu_drv_get_adc_cal/bias_cal_get` shim | implemented, one-time trim observability added | board/RF validation still required |

## P5: optional/system integration

| ABI area | NuttX provider | State | Runtime restriction |
| --- | --- | --- | --- |
| persisted net info | explicit board data contract | no-op by design for first STA milestone | fast-connect deferred |
| Wi-Fi/BT coexistence | selected vendor profile or real coex service | incomplete | no BT coexistence claim |
| netdev/IP/DHCP | NuttX lower-half registered; carrier/DHCP bridge incomplete | partial | blocks IP connectivity |
| WPA/hostapd | STA WPA sources integrated; AP/hostapd excluded | partial | runtime link closure and board validation remain |

## Current runtime boundary

The adapter now binds the verified PHY calibration, TPC, RF arbitration,
message transport, scan-result and RLK callbacks recorded in
`CAPABILITY_TABLE_GAP.csv`.  In particular, the PHY restore callback at ABI
offset `0x84` is bound to the linked
`rwnx_cal_recover_rcbeken_reg_val()` provider.  The legacy WIFIPLL hold ABI is
translated only for its two documented commands into the BK7258 RF arbitration
API; unsupported commands return `RF_ARBIT_RESULT_ERROR` rather than being
treated as successful operations.

`_rwnx_cal_set_channel` is now bound to the real PHY provider. The first
board observation invoked it with `2437`, and the BK7258 Armino channel table
defines that as 2.4 GHz channel 6. The linked provider accepts an MHz
frequency, converts it with `bk_phy_freq_to_channel()`, and programs the PHY
channel-control register. A local ABI-boundary wrapper accepts valid 2.4 GHz
center frequencies (including channel 14 at 2484 MHz) and prints then panics
on a channel index or other invalid value, because the provider itself accepts
arbitrary values above 2400. `_bk7011_update_by_rx` remains explicitly disabled:
the upstream adapter limits it to BK7236. Its diagnostic fail-fast wrapper
prints the exact ABI field and arguments, then calls `PANIC()`; it does not
claim RX sensitivity retuning before its BK7258 semantics are demonstrated.

The table also has explicit diagnostics for the two currently observable
Wi-Fi power-transition calls that were otherwise NULL.  Calling
`_bk_restore_all_regs_for_mac` prints its field name then stops at `PANIC()`,
because continuing without a verified register-restore contract would be
unsafe.  `_wifi_notify_state_to_bt` prints the active/inactive state and drops
the notification only in this BT-less profile; it does not claim that BT/PTA
coexistence has been implemented.

The separate `phy_os_funcs_t` ABI has its own BK7258 closure requirement. Its
`+0x84` `_aon_pmu_hal_get_chipid` entry is now explicitly bound for BK7258 to
the local provider that reads AON PMU R7C. This corrects a local
`CONFIG_SOC_BK7236XX=0` compatibility-condition omission without changing the
global SoC compatibility macro or broadening other BK7236-only PHY entries.
Its immediately following, verified ANA field-accessor group is likewise
bound for BK7258: ANA5 `adc_div[11:10]` and the existing ANA8–12 field
setters. These use the local locked, latched analog transaction and are
distinct from unported DPLL/bandgap/temperature calibration controls, which
remain explicitly diagnostic rather than silently successful.

BK7258-authoritative calibration semantics have now been added for the
previously diagnostic ANA5 bandgap fields: `bcal_en[23]`, `bcal_start[22]`, and
`vbias[31:27]`. The DPLL callback uses the upstream ANA0 trigger/detect
sequence as one locked analog transaction, preserving its four writes and
mode-dependent busy delays. Temperature acquisition uses the BK7258 SARADC
channel-7 sequence with ANA5 `en_temp[4]`, ten samples, and the vendor raw-code
filter/range contract. That returned value is a PHY temperature-sensor code,
not a calibrated Celsius reading; board measurement still gates any claim
about RF-calibration accuracy.

The remaining init warnings were separated by data provenance. The crystal
trim callback now applies its supplied 8-bit value to BK7258 ANA2
`xtalh_ctune[7:0]`. The temperature lifecycle now schedules the vendor-shaped
channel-7 SARADC raw-code measurement and calls PHY temperature compensation at
the documented one-second/then-fifteen-second cadence. AON bias trim was
corrected to SDK PMU R7E `cbcal[4:0]` rather than the unrelated R7D field. In
contrast, the SYS_RF polar-power blob remains read-only: a valid blob is a
board-specific 56-byte measurement record (magic plus CRC), so default-table
fallback is retained and no synthetic calibration record is written.

The CSV-driven Wi-Fi-table audit also closed the source-backed, side-effect
safe compatibility rows in one batch: MAC debug encoding, station status,
assert/log output, hostname, byte order, and the NuttX-owned ARP indication.
Persisted `save_net_info` / `get_net_info` deliberately remain unbound: the
vendor provider writes SYS_NET Flash, contrary to the selected no-persistence
STA profile. RF votes, PM/clock transitions, AP lifecycle, DHCP lease lookup,
and unverified hardware constants likewise remain separate real-implementation
work rather than success stubs.

The S1 link closure uses NuttX-backed OSAL, PM callback registration, event
registration/dispatch, memory/thread identity helpers, and the vendor's
no-op signal compatibility behavior. The pinned Wi-Fi archive also requires
coexistence report symbols; in the BT-less profile these are report-only ABI
endpoints and do not create a BT/PTA policy.

The resulting image still has no board evidence for MAC/PHY reset, RF power-up
and calibration. S2 now supplies a nonzero BK7258 factory crystal trim and
reads the AON ADC/bias trim from PMU register `0x7d`; it does not claim a
per-unit crystal record or a raw-count-to-voltage formula. The hard gate before
flashing or invoking `bk7258_wifi_runtime init` remains a reset contract in
CP's verified secure SYS address domain (or controlled cold-start evidence),
followed by SARADC/AON and RF/PHY live observations. Scan/connect/carrier/DHCP
remain later, separate milestones.
