# BK7258 DevKit OpenVela AP MVP

`configs/ap/defconfig` builds the dual-core OpenVela AP component: one NuttX
SMP image that runs physical CPU1 (logical CPU0) and physical CPU2 (logical
CPU1). It has no physical UART and keeps its standard NSH console behind the
RPMsg UART to CP.

The frozen MVP contract is:

- XIP/vector base `0x02160000`, raw limit `0x002a0000`;
- all-OpenVela AP RAM `0x28010000..0x28064000` (336 KiB);
- RPMsg shared memory is permanently reserved at
  `0x28064000..0x2806ec00` (43 KiB), even before RPTUN is enabled;
- AP-internal SMP: physical CPU1 + CPU2 run one NuttX kernel with
  `CONFIG_SMP=y` and `CONFIG_SMP_NCPUS=2`; CP↔AP remains AMP;
- UART0 remains owned by CP;
- SWAP `0x2809f800..0x280a0000` carries a versioned 64-byte record;
- reset handler writes `RESET_ENTERED`; board late initialization writes
  `SCHEDULER_RUNNING`, starts the RPTUN and health workers, and the health worker
  toggles the red LED on GPIO40 and increments heartbeat once per second;
- the standard `nsh_main` init entry owns the RPMsg-backed `/dev/console`.

The LED is also a diagnostic for the first sleep: solid red means the AP init
task reached `nxsig_usleep()` but has not been woken by SysTick; periodic red
blinking means the task and one-second tick path both make forward progress.

Building and validating `bk7258_ap/app1.bin` is only host evidence. AP bring-up
is complete only after a CP-launched hardware run records a changing scheduler
heartbeat without disrupting CP UART0.

Two consequences of that state are easy to misread, so they are stated here
explicitly:

- **The physical-UART `nsh>` belongs to CP / physical CPU0.** CP registers the
  RPMsg UART as `/dev/ttyAP`; AP registers the matching endpoint as
  `/dev/console` and runs standard `nsh_main`. From CP, `cu -l /dev/ttyAP`
  enters the AP shell and `~.` returns to CP.
- **Building and packaging `app1.bin` is not AP bring-up evidence.** Each stage
  needs one piece of evidence that does not depend on the next stage: a SWAP boot
  record with an increasing sequence for CPU1 reset-vector execution, a
  scheduler-updated heartbeat for AP kernel bring-up, a bidirectional
  `uart_rpmsg` AP NSH session for AP AMP interaction, and per-core state plus
  pinned test threads for AP SMP.

The source-backed comparison of Armino's BK7258 multicore model with NuttX/
OpenVela SMP and RPTUN is recorded in
[`BK7258_MULTICORE_RESEARCH.md`](BK7258_MULTICORE_RESEARCH.md). It also covers
Armino's CP-console-plus-`ap_cmd` model, the hardware-Mailbox versus
software-managed-shared-memory split, the Mailbox adaptation scope required by
RPTUN, and the `CONFIG_RPMSG_UART` constraints and failure modes. Its evidence
index is [`sources.tsv`](sources.tsv).

## A3 AP-internal dual-core SMP

A3 brings physical CPU2 online as the second AP NuttX logical CPU while CP↔AP
stays AMP. The mapping is fixed as:

```text
AP NuttX logical CPU0 -> BK7258 physical CPU1
AP NuttX logical CPU1 -> BK7258 physical CPU2
```

The chip port provides the SoC side of NuttX SMP:

- `up_cpu_index()` maps the running core to a NuttX logical index. BK7258 has
  no readable hardware core-id register, so CPU2 is identified by whether its
  stack pointer lies in its dedicated boot-stack or interrupt-stack range. A
  range test is required because a C-function prologue consumes stack before
  the first core-index query. The ranges are link-time constants and need no
  shared mutable state.
- `up_cpu_idlestack()` allocates the CPU1 idle task stack from the AP heap.
- `up_cpu_start(1)` releases physical CPU2 at the in-image `_vectors_core1`
  table (512-byte aligned, AP XIP), using the same CPU1 lifecycle register
  order, then handshakes on a dedicated SWAP flag word (word 44): CPU1 clears
  it before the release and polls it with a bounded loop (about one second)
  while CPU2 sets it after its early boot. The flag word sits behind the two
  generation slots (words 16..23), so a handshake write can never destroy the
  boot-sequence history. The boot path deliberately avoids cross-core
  spinlocks; the exclusive-monitor semantics of the BK7258 SRAM remain an
  unverified A3 item for the runtime scheduler spinlocks.
- a handshake timeout writes a `C2T` fault record (low byte = raw CPU
  status) before returning `-ETIMEDOUT`, so `DEBUGVERIFY(nx_smp_start())`
  turns into a recorded panic instead of a silent stall;
- `up_send_smp_sched()` / `up_send_smp_call()` drive the SMP IPI through the
  same hardware Mailbox that RPTUN uses as a doorbell. The Mailbox ISR demuxes
  by message type: `BK7258_MBOX_SMP_MAGIC` goes to the SMP handler, everything
  else to RPTUN. A shared per-direction pending latch lets kicks coalesce
  without ever dropping a wakeup.
- per-CPU interrupt stacks and `up_get_intstackbase()` replace the single-core
  `g_intstackalloc` when `CONFIG_SMP` is enabled.

Secondary-core startup (`bk7258_cpu2_boot`) reuses the primary NVIC exception
setup (`up_irqinitialize()`), enables the Mailbox line on its own NVIC, and
hands off to `nx_idle_trampoline()`. The single system tick is provided by
CPU1's SysTick, so CPU2 needs no timer of its own.

**Hardware validation note.** Armino's BK7258 source confirms that CPU2 uses
`SYS_CPU2_CTRL` (`0x44010018`), with its boot-address field encoded as
`vector_address >> 8`; the port follows that encoding and enables the Mailbox
IRQ via `SYS_CPU2_INT_EN_HI`. The release path fails closed (returns an error
and leaves CPU2 in reset) if programmed control bits do not read back, and
`up_cpu_start(1)` times out with diagnostics if the boot handshake is not
reached. Record the register readback and CPU2 heartbeat on real hardware
before treating A3 as passed.

The board exposes per-core state through the existing `bk7258_ap status`
record path (heartbeat still proves the whole AP SMP kernel is alive), and the
AP panic notifier encodes the assertion file hash and line number into the
fault word so the CP-side monitor can point at the failing source. A3
completeness is hardware-pass only when both idle tasks run, per-CPU state
shows logical CPU0/CPU1 online, pinned test threads count up on each AP CPU,
IPI/smp-call/spinlock paths hold under load, and the CP↔AP RPMsg path survives
AP SMP traffic. The AP still exposes exactly one NSH.

## A2-2 RPMsg UART over Mailbox IRQ

The first AMP transport keeps CPU2 in reset and uses the fixed all-OpenVela
memory tuple:

```text
AP_RAM    0x28010000..0x28064000
RPMSG_SHM 0x28064000..0x2806ec00
CP_RAM    0x2806ec00..0x2809f700
```

CP is the RPTUN master and AP is the remote. RPMsg payloads stay in shared SRAM;
BK7258 Mailbox v2 IRQ79 is the only runtime doorbell. There is no periodic vring
poll or fallback thread. Each direction keeps one shared outstanding latch so
equivalent all-virtqueue notifications can coalesce without filling the FIFO.

The CP enables the RPTUN reboot notifier, so a CP-side NSH `reboot` publishes
the reset reason and the AP enters its own `BOARDIOC_RESET` path before the CP
continues to its local Cortex-M `SYSRESETREQ`. The AP deliberately disables
the automatic reboot notifier: an AP-side `reboot` resets only AP and does not
ask CP to treat a normal remote reset as a fatal CP event. This asymmetric
policy matches the AMP ownership model, where CP owns AP lifecycle control.

`drivers_initialize()` registers the two symmetric RPMsg UART endpoints before
the RPMsg device exists. `board_late_initialize()` then starts RPTUN; endpoint
creation follows automatically when the VirtIO RPMsg device appears. AP uses
the standard `nsh_main -> nsh_initialize -> nsh_consolemain` path rather than a
board-specific shell entry.

```text
nsh> ls /dev/rptun
ap
nsh> ls /dev/rpmsg
ap
nsh> rpmsg ping /dev/rpmsg/ap 10 128 3 100
nsh> ls /dev/ttyAP
/dev/ttyAP
nsh> cu -l /dev/ttyAP
nsh-ap> uname -a
nsh-ap> ~.
```

Ping command `3` requests both ACK and payload checking. A2-2 is hardware-pass
only when the ping completes, the `rpmsg-ttyAP` endpoint binds, an interactive
AP NSH command succeeds, AP heartbeat continues increasing, and the red GPIO40
LED keeps blinking. Build success alone is not RPMsg UART evidence.

`/dev/rpmsg/ap` only proves that the local RPMsg VirtIO device exists. Before an
ACK ping or opening the terminal, `rpmsg dump /dev/rpmsg/ap` must show
non-`0xffffffff` destination addresses for the ping endpoints and a bound
`rpmsg-ttyAP` endpoint. The upstream ping ioctl waits without a timeout and does
not perform this readiness check itself.

The locked vendor CP/AP tuple enables D-cache. A2-2 explicitly clean-disables
any inherited D-cache in both reset paths before C runtime initialization, so
the transport has deterministic uncached visibility for SWAP, resource-table,
vrings and RPMsg buffers. Mailbox is a doorbell only and carries no RPMsg
payload.

The CP NSH is the NuttX init task, so its stack is controlled by
`CONFIG_INIT_STACKSIZE`, not `CONFIG_SYSTEM_NSH_STACKSIZE`. A 2 KiB init stack
overflowed in the NSH → ioctl → RPMsg ping → OpenAMP/VirtIO call chain; the A2
configuration fixes it at 4 KiB and the CP L1 validator enforces that value.

The upstream `uart_rpmsg` driver recreates its endpoint when an RPMsg device is
rebuilt, but currently does not propagate endpoint destroy/create through
`uart_connected()`. If AP restarts, exit the old `cu` session and open
`/dev/ttyAP` again after `rpmsg-ttyAP` is bound; transparent recovery of an
already blocked terminal session is outside this milestone.
