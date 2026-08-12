# BK7258 CPU0 Bring-up Contract

**Status:** CPU0 L0 cold-booted on hardware; peripheral and AP acceptance remain incomplete.

## 1. Purpose and scope

This contract defines the first openvela/NuttX image for the BK7258 DevKit:

```text
CP / CPU0
  → ARMv8-M Cortex-M33
  → non-SMP NuttX image
  → polling UART0 diagnostics
  → minimal NSH
```

The first image must not start CPU1 or CPU2, load the AP image, enable NuttX
SMP, use mailbox IPC, depend on Wi-Fi/BLE, use PSRAM as system memory, or
introduce a TrustZone multi-image flow. Those are separate follow-on designs.

The official BK AI / BK AVDK SMP architecture description supplied for this
project assigns CPU0 to the CP domain and CPU1 plus CPU2 to the AP domain.
The implementation target of this contract is therefore CPU0, not an inferred
sum of SDK `CPU_CNT` settings.

## 2. Evidence status

### Confirmed for design

| Subject | Evidence | Contract use |
| --- | --- | --- |
| CPU target | Armino BK7258 CP configuration selects `cm33`; its CP/AP toolchain files use `arm-none-eabi-*`, `-mcpu=cortex-m33+nodsp`, `-mfpu=fpv5-sp-d16`, `-mfloat-abi=hard`, and `-mcmse`. | Start from NuttX `arch/arm` ARMv8-M/Cortex-M33 support. Keep FPU and CMSE policy explicit. |
| CPU topology | Official BK AI / BK AVDK SMP description supplied for this project: CP is CPU0; AP is CPU1 and CPU2. | L0 runs CPU0 only and does not start or configure CPU1/CPU2. |
| CP static startup model | The successful Armino CP ELF contains one `.vectors` table at `0x02010000` and `Reset_Handler` at `0x02092538`; the ELF entry is `0x02092539` (Thumb). Its map links `startup_bk7236.c.obj`, not the CPU1/CPU2 startup objects. | The NuttX CPU0 image needs one vector table and one reset entry. This is a layout reference, not a value to copy into a NuttX linker script. |
| CP package slot | The successful Armino package places CP `app.bin` at flash offset `0x00011000`, with a 1428K partition. | CPU0 image packaging must be researched against this partition contract before flashing. |
| Board debug UART nets | The DevKit schematic identifies `P11/DL_0TX` as `TX0` and `P10/DL_0RX` as `RX0`; its USB-to-UART bridge connects to the `TX0`/`RX0` nets. | Use UART0 / GPIO11 TX and GPIO10 RX as the early-console candidate. |
| SDK UART mux reference | Armino maps GPIO11 to UART0 TX and GPIO10 to UART0 RX. | Confirms that the board UART nets match the BK7258 UART0 mux candidate. |
| SWD pins | The schematic labels P21 with `SWDIO` and P20 with `SWCLK`. | Preserve these pins while bringing up the board; do not repurpose them in L0. |
| Downloader read handshake | On 2026-08-07, Linux `bk_loader` 2.1.11.8 successfully opened `/dev/ttyUSB0`, initially connected at 115200, identified the target as `BK7236`, switched to 1500000, and read Flash offset `0x0` for `0x100` bytes. The resulting 256-byte dump has SHA-256 `cd626fd84a6adb3e9a2f87b467a7b84e0ebdadc6b10d9633c07ed539d3649095`. | Confirms the board USB-to-UART path and Beken downloader protocol are usable from Linux. `BK7236` is the downloader's reported protocol-family result; it does not replace the BK7258 DevKit and Armino SDK identification. |
| Vendor firmware write and runtime log | On 2026-08-07, `bk_loader download` wrote the Armino `all-app.bin` (SHA-256 `1613a6018553de9ebb0564572b090844253688af53c9bcc3de7c5b40f06db89d`) to offset `0x0`. The tool reported last-block CRC verification, write, re-protection, reboot, and `Writing Flash OK`. At 115200 baud after reboot, the captured log reports Wi-Fi initialization, Bluetooth initialization, and concurrent `ap0:` and `ap1:` messages. | Confirms that the known vendor Bootloader + CP + AP package is accepted by this board and that the original firmware runs CP services plus AP multi-context activity. This is a runtime reference only: it does not authorize a NuttX image format, prove an individual AP physical-core mapping, or change the CPU0-only scope of NuttX L0. |
| Shared SRAM hardware fabric | The BK7258 SDK declares six secure-domain data banks at `0x28000000`, `0x28010000`, `0x28020000`, `0x28040000`, `0x28060000`, and `0x28080000`, sized 64K, 64K, then four 128K banks; the Datasheet describes 640 KiB shared SRAM. | Confirms physical SRAM capacity and bank granularity, not an allocation available to NuttX. |
| Vendor CP security/address profile | The locked `beken_genie` CP build enables `CONFIG_SPE=1`; its address macros consequently use secure Flash `0x02000000`, shared-SRAM data `0x28000000`, and UART0 `0x44820000`. Non-secure aliases add `0x10000000`. | Confirms the vendor profile's address interpretation. A NuttX security-domain choice remains a separate boot-handoff decision. |
| Locked vendor CP console inputs | The locked CP build uses UART print port 0, 115200 baud, and XTAL 26 MHz; GPIO11 maps to UART0 TX and GPIO10 to UART0 RX. UART0's TX-ready polling bit is word `0x06`, bit 20; its FIFO port is word `0x07`, low byte. | Sufficient semantic evidence for an early TX-only polling experiment after its security domain and clock/reset writes are validated. |
| Vendor vector alignment policy | The CP linker asserts 512-byte vector alignment; the partition packer rounds application code starts to 512 bytes. | Require 512-byte alignment for the first NuttX vector table and any future relocation, pending hardware cold-boot confirmation. |
| Vendor secure-image policy | The vendor CP and AP app configurations both select `CONFIG_TZ=y` and `CONFIG_SPE=1`. In this mode SDK aliases are unshifted secure addresses. The available secure jump helper follows MSP → `SCB->VTOR` → reset-vector semantics, while no source caller for either secure/non-secure helper is visible outside the prebuilt Bootloader. Generated PPC output describes SYS, UART0, TIMER0, FLASH, AON and all GPIO as secure. | L0 deliberately selects a secure/SPE CP replacement; a non-secure image, SAU/NSC split and `VTOR_NS` handoff are excluded. L0 does not reconfigure PRRO/PPC. T7 must verify the opaque Bootloader uses a compatible secure handoff and preserves the locked PPC state. |
| Locked-profile CP RAM reservation | The locked `beken_genie` generated map assigns CP `0x2806ec00..0x2809f6ff` (`0x30b00`), ending before its PWR_MNG/SWAP ranges. | Use exactly this secure RAM reservation for the first CP replacement. It is a replacement-profile reservation, not proof that any remaining shared SRAM is free. |
| Secure UART0/SYS policy | Vendor PPC inputs mark UART0 and SYS secure. UART0 uses secure `0x44820000`; UART clock/reset access uses secure SYS `0x44010000`. | L0 uses only secure aliases and does not change PPC/PRRO policy. |
| CPU1 launch policy | Vendor CP code can vote to boot CPU1; the secure launch code programs its boot address in 256-byte units, then releases reset. | L0 must not make the CP1 power vote, write CPU1/CPU2 boot offsets, or release either reset. T7 records that AP did not start. |
| Time-base policy | The locked vendor CP enables `CONFIG_SYSTICK_32K=y`, which selects CPU0's external 32 kHz SysTick source through secure SYS power/sleep control. BK7258 general timers have W1C/restart hazards. | Use the secure 32 kHz SysTick source for initial scheduler time. T7 must measure/confirm time-base behavior; defer general timer and RTC alarm integration. |
| OpenVela CPU0 cold boot | On 2026-08-11, a complete L2 image containing the staged normal Bootloader, OpenVela CP `app.bin`, and locked vendor AP payload was written at Flash offset `0x0`. UART0 at 115200 emitted `BK`, `NuttShell (NSH)` and `nsh>`. | Confirms the observed Bootloader handoff, CPU0 vector/start path, basic scheduler transition, 32 kHz SysTick selection, UART0 TX and minimal NSH bring-up for this image. It does not prove AP state, peripheral availability, or stability. |
| OpenVela CPU0 UART RX | On 2026-08-11, after console RX setup was moved before `/dev/console` registration, typed `?` reached NSH and printed built-in help. | Confirms GPIO10 UART0 RX, RX enable, ICU/NVIC routing and console input for the minimal CPU0 path. Host tests use 115200 8N1 with hardware and software flow control disabled. |

### Observed failures and corrective decisions

| Observation | Cause or status | Corrective action and evidence boundary |
| --- | --- | --- |
| Board CMake L1 export left `bk7258/app.bin` stale after a CP relink. | The export command had only order-only target dependencies, not file dependencies on `nuttx.bin`, `nuttx` and `nuttx.map`. | Declare all three generated files as normal `DEPENDS`; L2 packaging now verifies that exported `app.bin` has the same SHA-256 as root `nuttx.bin`. |
| Direct `cmake --build` failed with `ccache: not found`. | The project environment normally supplies the tool wrapper; direct CMake did not. | Use the parent-workspace `build.sh`/`bear -- ./build.sh` command for reproducible builds. This is a host environment caveat, not a target firmware failure. |
| `bk_loader` intermittently failed at `LinkCheck Timeout` / `GetBus fail`. | The failure happened before input-image parsing, Flash erase or write; CH340/download-mode availability was transient. | Re-enter downloader mode. `download` performs its own reset and bus handshake, so this failure still occurs before `Begin EraseFlash`; use a separate read-only `Read Flash OK` handshake only when diagnosing the channel. Do not attribute this failure to `all-app.bin`, CRC or image marker contents. |
| First CPU0 image hard-faulted at the initial scheduler transition. | Original panic output overwrote the exception context, but detailed fault logs recovered `SYS_switch_context` (`svc 0`) at first `sched_unlock()`. BK code had assigned SVC `0x80`, the same as NuttX `BASEPRI` critical-section threshold. | Set SVC to `NVIC_SYSH_SVCALL_PRIORITY` (`0x60`), set SysTick to normal `0x80`, and retain detailed fault diagnostics. Subsequent CPU0 boot reached NSH; repeated stability is still unverified. |
| `nsh>` printed but ignored typed input. | NuttX skips `setup()` for a console on first open; the prior early path initialized only UART TX, leaving GPIO10 RX and `RX_EN` unset. | Invoke the UART setup before console registration. One hardware `?` command reached NSH; disable host hardware and software flow control during testing. |
| Long NSH help output ended only after a second input event. | TX bit 0 (`tx_fifo_need_write`) was cleared immediately after the first FIFO fill and the ISR ignored TX-ready status. | Enable and dispatch TX-ready to drain the software TX buffer. The L1/L2 result is verified offline; on 2026-08-12 the operator confirmed one `?`/`help` command completed without another input event. No UART capture was retained for that run. |

### Candidate only — do not encode yet

| Subject | Candidate evidence | Required confirmation |
| --- | --- | --- |
| CPU0 image virtual Flash base | Armino CP ELF vectors are at `0x02010000`; its package stores CP firmware at offset `0x00011000`. The locked profile further establishes `0x11000 physical → 0x10000 payload virtual → 0x02010000 CPU XIP`. | Confirm a NuttX-produced image through the locked linear-CRC package and real Flash cold boot; do not use the physical offset as a linker address. |
| Existing offset-zero image | The read-only Flash sample begins `00 00 03 28 c1 01 00 02`, which decodes as a candidate initial stack value `0x28030000` followed by a Thumb reset value `0x020001c1`. | Treat this only as the currently flashed bootloader's vector-like header. Do not use either value for the NuttX vector, stack, Flash mapping, or linker layout. |
| CP SRAM/IRAM/ITCM/DTCM regions | Armino build summary exposes CP regions at `0x0806ec00`, `0x2806ec00`, `0x00000020`, and `0x20000000`. | Derive the NuttX L0 linker layout from the BK7258 TRM and CPU0 image requirements; do not reuse the heavily allocated AI-firmware addresses. |
| FPU | Armino compiles with hard float and CMSE, but the Datasheet and SDK descriptions are not yet reconciled into a NuttX ABI contract. | L0 does not require FPU instructions. Do not select NuttX FPU support or hard-float ABI before a dedicated toolchain/exception validation. |
| Reset circuit and download mode | The schematic includes `RESET`, `SWD`, USB, and USB-to-UART nets. | Manually inspect the original schematic and vendor download guide to identify reset polarity, BootROM download strap/mode, required host tool sequence, and secondary-core reset policy. |
| General-timer/ICU candidates | TIMER0 channels 0–2 route via `INT_SRC_TIMER` at ICU group 0/bit 3; TIMER1 channels 3–5 route via `INT_SRC_TIMER1` at group 0/bit 13. Vendor timer interrupt clear is W1C with a retry loop; restarting an enabled timer requires disable plus delay. | Preserve for later timer/RTC work. SysTick avoids these hazards in initial L0. |

### Unknown and blocking implementation

- The raw CP component of the locked profile begins directly with a Cortex-M
  vector table, not a CP image header. Its physical Flash representation
  requires the locked Bootloader staging and 32-byte payload + CRC16-BE linear
  package contract. One NuttX CPU0 cold boot through that contract is recorded;
  raw `app.bin` must still never be directly written to physical CP offset
  `0x11000`.
- Repeated cold-boot, reset-cycle and duration testing of the secure/SPE
  replacement image. One CPU0 NSH cold boot is recorded, but it is not
  stability acceptance.
- Whether the existing Bootloader releases CPU0 in the secure or non-secure
  state, including whether it programs MPC/PPC/SAU policy and whether NuttX
  must use `SCB->VTOR` or a non-secure VTOR handoff.
- A CPU0-exclusive SRAM allocation. The Datasheet/SDK bank map confirms 640
  KiB physical shared SRAM, but all inspected vendor CP/AP allocations are
  generated project policies and may coexist with AP, spinlock, power, swap or
  accelerator users.
- Reset electrical polarity, boot strap behavior, the complete SWD header
  wiring, and whether CPU0 software must explicitly hold CPU1/CPU2 in reset.
- The UART0 clock gate, reset gate, register programming sequence, and the
  initial baud rate required by the hardware.
- ICU-to-NVIC programming details and the timer hardware selected for NuttX
  `arch_alarm`.

No unknown item may be filled with values copied from another SoC or guessed
from the Armino AI application layout.

### Native CP implementation reference map

The following is a behavior map extracted from the successfully built
`beken_genie` CP image and the Armino SMP source it uses. It defines what the
NuttX chip layer must reproduce semantically, not source code to copy.

| Native CP behavior | Observed source / generated artifact | NuttX CPU0 implementation mapping | Status |
| --- | --- | --- | --- |
| Complete-image CRC packing | The selected `bk_packager_linear_crc` applies Beken CRC16 to each 32-byte data unit, pads the final unit with `0xff`, and appends a two-byte big-endian CRC. The CRC polynomial is `0x8005`; the all-image linker places the packed partition bytes at the configured physical offsets. | The BK7258 L2 wrapper invokes this locked SDK primitive, independently decodes its output and records hashes in a manifest. | Verified offline and used by the recorded CPU0 L0 cold boot; it is not OTA or release-signing evidence. |
| CP Flash address transform | Generated CP linker input transforms physical partition offset `0x11000` as `(offset / 34 * 32)` before adding the CPU Flash mapping base `0x02000000`; the resulting vector address is `0x02010000`. | Keep physical Flash offsets, packed bytes, and CPU-visible virtual Flash addresses as three separate concepts in the NuttX linker/packer design. | Verified for the recorded CPU0 L0 package and cold boot; raw CP writes remain forbidden. |
| CP memory partition | Generated `ram_regions.h` assigns the full vendor firmware a CP RAM range at `0x2806ec00` of size `0x30b00`, plus distinct AP, accelerator, swap, TCM, and PSRAM regions. | Treat this as a known-working allocation inside the vendor multi-image firmware, not as the maximal memory available to CPU0. NuttX L0 must choose its own verified SRAM/heap allocation and must not claim AP/PSRAM/accelerator regions. | Candidate only. |
| CPU0 console policy | The actual generated CP config enables UART printing at 115200. The CP printf path uses 8N1, flow control disabled, and XTAL 26M as the UART source. The board routes UART0 through P11/TX0 and P10/RX0. | `bk7258_lowputc.c` should first implement TX-only, polling UART0 output at 115200, with GPIO11/P11 TX selected. Receive, FIFOs, interrupts, and `/dev/console` come later. | Clock/reset register details remain to be independently verified. |
| UART0 initialization order | Armino's UART path powers UART0, selects its clock, configures UART GPIO, initializes state, optionally registers/enables IRQ, initializes UART registers, and starts the peripheral. | Split this into NuttX early polling initialization before `nx_start()` and later full `bk7258_uart.c` registration. Do not port the Armino order wholesale because its driver enables RTOS/IRQ/PM behavior not needed by early NuttX output. | Semantic reference only. |
| ICU interrupt routing | BK7258 CP `ICU_DEV_MAP` maps `INT_SRC_TIMER` to group 0 / bit 3 and `INT_SRC_UART0` to group 0 / bit 4. The timer driver registers `INT_SRC_TIMER`; UART0 registers `INT_SRC_UART0`. | Define BK7258 IRQ identifiers and route the mapped sources through NuttX `irq_attach`, enable/disable, and priority APIs. | Candidate mapping until confirmed against the BK7258 TRM and CPU0 vector behavior. |
| Timer behavior | Native driver initializes a timer HAL, registers `INT_SRC_TIMER` (and optionally `INT_SRC_TIMER1`), selects/powers a timer group, then starts channels. The generated CP config enables a microsecond-timer mode that reserves `TIMER_ID0`. | Do not choose `TIMER_ID0` for NuttX `arch_alarm` until its reservation and clock source are understood. Select the timer only after verifying its independent availability, frequency, reset state, and interrupt routing. | Blocked by TRM/targeted hardware test. |

### CPU0 BSP parameter ledger

The following ledger separates values safe to encode in profile tooling from
values safe to encode in a CPU0 linker/start file. A vendor build setting is
not automatically a NuttX ownership grant.

| Parameter | Current evidence | BSP disposition |
| --- | --- | --- |
| CP L2 physical slot / XIP vector base | Locked profile: physical `0x11000`, payload virtual `0x10000`, secure XIP `0x02010000` | Candidate linker Flash origin; use only in a secure-domain first experiment and validate by Flash cold boot. |
| Vector alignment | Vendor linker and packer both require 512 bytes | Enforce 512-byte vector alignment. |
| Secure address aliases | `CONFIG_SPE=1` vendor profile has zero address offset; non-secure alias difference is `0x10000000` | Do not hard-code peripheral/memory bases until secure versus non-secure handoff is closed. |
| CP RAM `0x2806ec00/0x30b00` | Current `beken_genie` vendor allocation | Do not put in `flash.ld` yet; a fresh ownership plan or reversible hardware experiment must approve it. |
| ITCM/DTCM | Datasheet gives 16 KiB each; SDK has aliases and vendor CPU-ID reservation | Exclude from initial text, stack and heap. |
| FPU, CMSE, SAU, cache | Datasheet/SDK capability and vendor compile flags exist, but no verified Bootloader handoff policy | Disable/defer initially; no TrustZone dual-image flow. |
| UART0 TX | Secure candidate base `0x44820000`; GPIO11; 26 MHz; 115200; TX FIFO-ready poll | Candidate only; first use follows explicit security-domain and clock/reset validation. |
| System timer | Timer0/Timer1 source and ICU candidates known | Remain blocked; no `arch_alarm` choice yet. |

The source files are reference inputs only. Even where they carry an
open-source header, do not copy their implementation into this repository;
independently implement the NuttX equivalent and preserve the applicable
upstream ownership/PR boundary.

### Approved CPU0 replacement and vendor-component reuse boundary

The initial NuttX bring-up adopts this composition model:

```text
physical Flash 0x00000000
├── vendor BootROM                         (chip-resident; unchanged)
├── vendor normal Bootloader               (preserved and repackaged)
├── CP application partition / app.bin
│   └── replace with the independently built NuttX CPU0 image
└── AP application partition / app1.bin
    └── preserve the known-good vendor AP image during CPU0 L0 bring-up
```

This follows the vendor packaging model observed in the successful
`beken_genie` build:

- `bk7258` is classified as the CP application and copied as `app.bin`.
- `bk7258_ap` is classified as the AP application and copied as `app1.bin`.
- the project packer supplies the bootloader, CP image and AP image to the
  CRC-aware linear packer at their partition offsets.

In architecture terms, `app.bin` is the CP/CPU0 image replacement target and
`app1.bin` is the application-domain image intended for AP CPU1/CPU2 work.
For the specific captured `beken_genie` build, static inspection confirms AP
CPU0 and CPU1 startup objects, but does not show a linked CPU2 startup object.
Therefore the preserved `app1.bin` must be called a known-good vendor AP image,
not evidence that every AP core has started in every firmware configuration.

The complete image must be produced by the vendor packaging chain (or an
independently verified compatible implementation), not by concatenating files:

```text
raw NuttX CPU0 app.bin
  + preserved vendor app1.bin
  + vendor normal Bootloader
  + generated partition metadata
  → per-partition 32-byte data + 2-byte CRC16 transformation
  → physical-offset linear package
  → complete image for bk_loader download -s 0
```

The raw normal Bootloader binary is not copied verbatim into the final package:
the native pack flow appends generated partition/OTA metadata before the
CRC-aware final image step. In the verified vendor build, the source
Bootloader is 52352 bytes and the pre-packaged temporary Bootloader is 52608
bytes. Do not use `cat`, do not substitute only `app.bin` at offset `0x11000`,
and do not hand-copy a raw Bootloader plus NuttX image.

### Reuse policy

| Component | L0 policy | Reason |
| --- | --- | --- |
| BK7258 BootROM | Reuse unchanged | It is chip-resident. |
| Vendor normal Bootloader | Preserve and feed through the vendor pack flow | It is already accepted by the DevKit and carries generated partition metadata in the final package. |
| Beken CRC16, physical/virtual Flash conversion, partition generator and package flow | Reuse as an external build-time dependency, or independently reimplement only after byte-for-byte verification | They define the image the existing Bootloader accepts. |
| `bk_loader` | Reuse unchanged for flashing | It is verified on `/dev/ttyUSB0`. |
| Vendor `app1.bin` | Preserve for early CPU0 experiments | Keeps the known-good AP side intact while NuttX replaces only the CP image. |
| Vendor `all-app.bin` | Preserve as the recovery image | It restores the known-good native firmware. |
| NuttX CPU0 start, linker layout, heap, IRQ/ICU, timer and UART | Implement independently | They must satisfy NuttX kernel, VFS and scheduler interfaces. |
| Armino CP RTOS, UART, timer, ICU and PM libraries | Do not link into NuttX | They depend on Armino/FreeRTOS data structures, initialization, memory and IPC contracts. |

Before first NuttX flashing, the package step must produce a manifest recording
the NuttX raw image SHA-256, preserved Bootloader SHA-256, preserved `app1.bin`
SHA-256, partition JSON SHA-256, packer version/commit, output image SHA-256,
and exact flashing command. The first image must be recoverable by the
verified vendor complete-image procedure in section 9.

### UART0 register facts available from the native CP implementation

The native project exposes enough UART0 information to design an independent
NuttX polling implementation. These are implementation facts, not permission
to copy the native driver.

| Item | Native CP fact | NuttX lowputc consequence |
| --- | --- | --- |
| Address domain | The built vendor CP configuration has `CONFIG_SPE=1`, so its `SOC_ADDR_OFFSET` is zero. In that secure configuration UART0 is based at `0x44820000`. The native headers add `0x10000000` when `CONFIG_SPE` is disabled. | The NuttX CPU0 security/address-domain policy must be chosen before hard-coding a peripheral base. `0x44820000` is the verified secure-domain candidate, not an unconditional L0 constant. |
| UART format register | UART register word `0x04` has TX enable bit 0, RX enable bit 1, data-size bits 3–4 (`3` means 8 bits), parity-enable bit 5, stop-bit bit 7 (`0` means one stop bit), and a 16-bit clock divider in bits 8–23. | Configure TX-only or TX/RX as required, 8N1, no parity, and a validated divider. For earliest output, TX-only avoids depending on receive state. |
| FIFO polling | UART word `0x06` bit 20 is FIFO write-ready; UART word `0x07` low byte is the TX FIFO input. Native code waits for write-ready and writes the full FIFO-port word with the byte in bits 0–7. | `up_putc()` can poll bit 20 then write the character to word `0x07`; it does not require the UART interrupt path. |
| Clock and pinmux | Native UART0 initialization enables the UART0 clock/power gate, selects the 26 MHz XTAL source, maps GPIO11 to UART0 TX and GPIO10 to UART0 RX, and applies pull-ups. The generated CP config uses print port 0, 115200 baud, 8N1 and flow control disabled. | Initial `bk7258_lowputc.c` should target UART0 TX on GPIO11/P11 at 115200, with flow control and UART interrupts disabled. RX on GPIO10/P10 is deferred until the full UART driver. |
| Clock-register semantics | The native BK7258 system register description identifies UART0 clock-gate enable at CPU-device-clock-enable bit 2 and its 26 MHz/APLL selection at CPU-clock-div-mode1 bit 10 (`0` is XTAL, `1` is APLL). | Reimplement these writes only after the CPU0 security-domain address policy and system-register access have been verified on a minimal image. |
| 115200 divider candidate | With the native 26 MHz source and formula `clk_div = uart_clock / baud - 1`, integer arithmetic yields `224` for 115200 baud. | Use `224` only as the first UART0 test candidate; validate actual output timing on the DevKit before treating it as final. |

The native UART driver also registers `INT_SRC_UART0`; this belongs to the
later full UART driver. Early output must leave UART interrupt enables clear
and rely only on the FIFO-ready poll.

## 3. CPU0 image and startup contract

The eventual CPU0 chip layer will provide a BK7258-specific reset/start file
that uses NuttX ARMv8-M interfaces and ends at `nx_start()`.

Required order:

```text
CPU0 reset / BootROM handoff
  → establish initial stack without starting or configuring CPU1/CPU2
  → install CPU0 vector base / required ARM exception state
  → copy .data and clear .bss
  → apply only verified clock and reset-gate operations
  → initialize polling UART0 enough for fatal diagnostics
  → initialize NuttX heap boundaries
  → nx_start()
```

The design intentionally excludes these actions before minimal NSH succeeds:

- Starting CPU1 or CPU2, loading the AP image, or calling mailbox IPC.
- Enabling PSRAM, AP RAM allocations, media memory, or TCM code relocation.
- Enabling a TrustZone multi-image handoff or assuming the AP secure policy.
- Enabling UART, timer, GPIO, I2C, or SPI interrupts before the NuttX IRQ
  controller path is verified.

## 4. Linker and memory contract

The production linker script must be created only after the blocked items in
section 2 are resolved. Its initial responsibilities are:

1. Emit one CPU0 vector table with the alignment required by BK7258.
2. Set the NuttX reset/vector entry compatible with the verified CPU0 image
   handoff.
3. Place `.text`/`.rodata` in the verified executable CPU0 Flash region.
4. Copy `.data` into, and clear `.bss` in, verified CPU0 SRAM.
5. Reserve the idle stack, any required interrupt stack, then define one
   contiguous L0 heap from the remaining verified CPU0 SRAM.
6. Export only the symbols required by the NuttX ARM startup and heap code.

For L0, the linker script must not allocate PSRAM, AP regions, CPU1/CPU2
vectors, media slabs, or vendor-specific swap/CRC regions unless their use is
proven necessary for a CPU0 NuttX image.

The Armino CP package is useful evidence that a bootloader plus CP and AP
images coexist in the complete product firmware. It does **not** establish
that NuttX should replace the entire `all-app.bin`, use the same virtual
addresses, or reuse its partition/header format.

## 5. Early UART0 contract

The first observable output is a polling UART0 console.

```text
BK7258 P11 / DL_0TX → board net TX0 → USB-to-UART bridge receive input
BK7258 P10 / DL_0RX → board net RX0 ← USB-to-UART bridge transmit output
```

The chip layer owns UART0 register access, clock/reset control and polling
transmit. The board layer owns the DevKit pinmux selection and console policy.
The final baud rate, UART clock source, pull configuration and register
sequence remain blocked until the TRM/SDK behavior is independently verified.

The first implementation milestone is a reliable transmit marker before
`nx_start()`. CPU0 cold boot and interactive NSH input now have one hardware
observation. On 2026-08-12 the operator also confirmed that a single `?`/`help`
command finishes without an additional input event, validating the UART TX
FIFO-drain fix on hardware; no UART capture was retained for that run. DMA
remains deferred.

## 5A. L1 component debug and L2 Flash-boot boundary

The CPU0 build exports L1 artifacts (`nuttx`, `nuttx.bin`, `nuttx.map`, and
raw `app.bin`) for symbol inspection, section/vector checks and as the input
to the package step. Those artifacts are not direct physical-Flash images for
the locked profile: writing raw bytes continuously at physical `0x11000`
would omit CRC slots and cannot validate XIP or cold boot.

L2-dev is the first valid Flash-boot test image: staged vendor Bootloader +
OpenVela CP `app.bin` + locked vendor `app1.bin`, encoded by the linear-CRC
package contract as `all-app.bin`. Only this complete image may be given to
the verified `bk_loader download -s 0` workflow after offline package checks.

SWD is a possible future L1 RAM-debug aid because the board exposes SWD pins,
but no probe, security/debug-lock state, RAM-load procedure, reset behavior,
or CPU0 halt/step sequence has been verified. Treat SWD as optional and do
not use a successful RAM-load session as Flash boot or package acceptance.

## 6. Ownership and source boundaries

- `chips/bk7258/`: CPU0 startup, heap, IRQ, timer and UART controller support.
- `board/bk7258-devkit/`: DevKit-specific pinmux, link configuration,
  console choice, LED/buttons and later I2C/SPI bindings.
- `nuttx/arch/arm`: reused ARMv8-M infrastructure; any upstream change is
  prepared as a separate upstream PR, never copied into this team repository.
- Armino and generated build artifacts are reference inputs only. Do not copy
  their source files, generated headers, prebuilt libraries, keys or linker
  scripts into this repository.

### NuttX and OpenVela reference boundaries

Use NuttX `arch/arm/src/mps/` and the MPS2-AN521 board as the primary minimal
ARMv8-M/Cortex-M33 skeleton reference: startup, heap, NVIC, SysTick, Make and
CMake source lists. It avoids the dual-core, IPC and SPU baggage of nRF53.
Use STM32H5/U5 only for UART, GPIO and clock driver separation. Use nRF91
TrustZone/SPU ordering only if a verified BK7258 secure-handoff policy requires
it; it is not a default dependency.

Use `vendor/sifli/chips/sf32lb52/` only as the OpenVela custom-chip integration
reference. BK7258 must use `CONFIG_ARCH_CHIP_CUSTOM` pointing at
`../vendor/beken/chips/bk7258` and a corresponding custom-board path, rather
than claiming an in-tree NuttX `ARCH_CHIP_BK7258` symbol before an upstream PR
exists.

### CP minimum file-level delivery contract

After the parameter ledger is closed, the CP implementation must deliberately
add only the following initial files; optional peripherals remain conditional:

```text
chips/bk7258/
├── include/chip.h
├── include/bk7258_memorymap.h
├── include/bk7258_irq.h
├── bk7258_start.c
├── bk7258_clock.c
├── bk7258_allocateheap.c
├── bk7258_irq.c
├── bk7258_lowputc.c
├── bk7258_uart.c
├── bk7258_timer.c
├── bk7258_gpio.c
├── Kconfig
├── Make.defs
└── CMakeLists.txt

board/bk7258-devkit/
├── include/board.h
├── scripts/flash.ld
├── src/bk7258_boardinit.c
├── src/bk7258_bringup.c
└── configs/cp/defconfig
```

`bk7258_lowputc.c` owns scheduler-precondition polling output. The chip serial
driver owns `/dev/console`, RX and UART IRQ registration. Board bring-up owns
concrete LED/button/I2C/SPI instances and board policy; it must not absorb the
generic UART controller driver.

### Startup diagnostics policy

Before `nx_start()`, emit only bounded polling `lowputc` / `up_putc` markers;
do not use malloc, locking, UART IRQ or a syslog daemon. After `nx_start()`,
use NuttX syslog: ERROR for fatal startup/memory/CRC/IRQ errors, WARN for
disabled or degraded capability, INFO for phase markers and verified resource
summaries. Never log credentials, sensitive tool arguments, enterprise data or
absolute local paths.

### Storage boundary

L0 verifies only XIP execution from the packaged CP image. It does not deliver
NuttX MTD, `/dev/mtd*`, writable Flash, filesystem mount or a board
`partition_table.c`; the locked profile plus vendor JSON are the only L2
packaging partition source. MTD/filesystem work begins only after CP NSH is
stable and requires a separate Flash-controller, erase/protection and runtime
partition contract.

## 7. Remaining CPU0 acceptance

The initial CPU0 implementation is built and has one cold-boot/NSH observation.
The following items remain required before claiming a production-ready BSP:

- CPU0 image handoff rule: image format, load address/offset, reset entry and
  required integrity/security processing.
- Exact L0 Flash and SRAM ranges, including stack and heap placement.
- Vector table alignment and VTOR policy.
- L0 FPU, MPU, cache, CMSE/SAU and TCM policy.
- UART0 clock/reset/pinmux sequence and an agreed initial baud rate.
- Chosen system timer, ICU source and NVIC mapping for NuttX `arch_alarm`.
- Reproducible CPU0-only flashing procedure and expected UART observation.
- Clear separation of L1 static component checks, optional unverified L1-SWD
  RAM debugging, and the required L2-dev `all-app.bin` Flash cold-boot test.

The recorded evidence authorizes only continued experimental CPU0 L0 bring-up;
it does not authorize a production, AP-capable or stability claim.

## 8. Evidence index

The external asset roots below are machine-local and intentionally absent from
this repository. Each referenced artifact is identified by its locked SHA-256,
not by its path, so a different checkout location does not weaken the evidence:

- `<armino-sdk>` — Armino SDK checkout, revision
  `d2ded037798530175e5dc5cde6fa1878f5d5ef35` (`release/v3.1.1`). Set as
  `BK7258_ARMINO_SDK_ROOT` in the local-only locator described in
  `board/bk7258-devkit/packaging/README.md`.
- `<beken-genie-build>` — locked `beken_genie` BK7258 build output tree. Set as
  `BK7258_BEKEN_GENIE_BUILD_ROOT` in the same locator.
- `<vendor-docs>` — vendor-copyright DevKit documents, not redistributed here.

The locked SHA-256, raw length and relative path of every input consumed by the
packaging tools is recorded in
`board/bk7258-devkit/packaging/profiles/bk7258-devkit-l0-vendor-ap.json`.

- DevKit schematic: `<vendor-docs>/AIDK_AI玩具开发板_原理图.pdf`, pages 2–3.
  Title "BK7258 AI Demo", revision V1.0, dated 2025-03-17.
- Armino source reference: `<armino-sdk>/cp/middleware/soc/bk7258/`.
- Successful CP/AP package reference: `<beken-genie-build>/`.
  In particular: `package/build_summary.txt`, `partitions/bk_package.json`,
  `bk7258/app.elf`, `bk7258/app.map`, `bk7258_ap/app.elf`, and
  `bk7258_ap/app.map`.
- Native CP implementation references: `<armino-sdk>/tools/env_tools/bk_py_libs/bk_crc/bk_crc16.py`, `tools/env_tools/bk_py_libs/bk_packager/`, `tools/build_tools/build_process/`, `cp/middleware/driver/uart/uart_driver.c`, `cp/middleware/driver/timer/timer_driver.c`, `cp/middleware/driver/icu/`, `cp/middleware/driver/sys_ctrl/`, and `cp/middleware/soc/bk7258/`.

## 9. Verified vendor complete-image flash procedure

The following procedure was successfully used on 2026-08-07 to write and
boot the known-good Armino `beken_genie` complete image on this DevKit.

### Preconditions

- The board USB-to-UART interface is enumerated as `/dev/ttyUSB0`.
- Bundled `board/bk7258-devkit/tools/bk_loader` version is `2.1.11.8` and
  SHA-256 is `55221d83d5582c362aab27d8883d799acf5eaa7b735d51135e01b0a24156b9ab`.
- Loader port index `0` maps to `/dev/ttyUSB0` on the recorded DevKit host.
- The board is in the Beken downloader-accessible state. The verified read
  handshake initially connects at 115200 and then switches to 1500000.
- The input is the complete, known-good vendor package, not an individual CP
  or AP image:

  ```text
  <beken-genie-build>/package/all-app.bin
  SHA-256: 1613a6018553de9ebb0564572b090844253688af53c9bcc3de7c5b40f06db89d
  length: 4229792 bytes
  ```

  `<beken-genie-build>` is the machine-local root defined in section 8. The
  SHA-256 above, not the path, identifies the image that was actually written.

### Command

Run from the OpenVela workspace root. `BK7258_BUILD_ROOT` is the
`BK7258_BEKEN_GENIE_BUILD_ROOT` value from the local-only locator file:

```bash
BK7258_BUILD_ROOT=<path-from-local-input-file>

sudo contest2026_272_tokenwujixian/board/bk7258-devkit/tools/bk_loader download \
  -p 0 \
  -b 1500000 \
  -s 0 \
  -i "$BK7258_BUILD_ROOT/package/all-app.bin"
```

`-s 0` is required because `all-app.bin` is a complete package containing the
bootloader plus the CP and AP images. The CP-only `app.bin` offset `0x11000`
must **not** be used as the start address for this complete image.

The downloader automatically erases the required Flash blocks before writing.
The tested command deliberately does not request chip erase, Efuse, OTP,
SecureBoot, Device ID, or DeviceName operations.

### Successful result criteria

The recorded run reported all of the following:

```text
Current port : /dev/ttyUSB0 + BaudRate : 115200 connect success
Current Chip is : BK7236
Current baudrate : 1500000 success
Last 4K CRC_Verify ->pass
WriteFlash ->pass
Enprotect pass
Boot_Reboot
Writing Flash OK
```

After the downloader reboot, the same `/dev/ttyUSB0` interface produced the
vendor runtime log at 115200 baud, including CP Wi-Fi/Bluetooth initialization
and `ap0:`/`ap1:` activity.

### Scope and future use

This is a recoverable, verified procedure for restoring the known-good
**vendor complete image**. It remains the control and recovery path whenever
hardware bring-up fails.

The same `bk_loader download -s 0` transport has now been used for an
independently decoded experimental NuttX CPU0 L2 image. It must only be used
with a complete `all-app.bin` generated by the locked profile and accompanied
by its manifest/decode report; it must never be used with raw `app.bin` at
`0x11000`. Before every experiment retain this exact vendor recovery image and
SHA-256. This does not validate BootROM behavior beyond the recorded CPU0 L0
configuration or authorize a production image.
