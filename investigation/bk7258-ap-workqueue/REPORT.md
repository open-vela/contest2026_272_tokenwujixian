# BK7258 AP dynamic work queue root-cause report

## Contract

The investigation set out to remove the temporary synchronous RPMsg start-work
wrapper and restore the native Vela/NuttX path. The stopping criterion was a
two-way causal demonstration followed by a clean native image: no linker
wrapper, native dynamic work queue, native RPMsg/Name Service, and hardware
`cmd=3`.

## Root cause

The failure was caused by CPU1 entering the BK7258 AP XIP image without a
deterministically initialized private I-cache. Under that execution condition,
the timing-sensitive `CONFIG_TIMER_ARCH` `arch_timer.current_usec()` lockless
timebase/status consistency loop did not converge while SysTick was active, so
`clock()` never returned. `work_queue_wq()` calls `clock()` before taking its
queue lock, making the dynamic work queue and RPMsg appear broken even though
they were only downstream victims.

The cause was demonstrated in both directions:

1. Replacing only the AP timekeeping path with standard NuttX periodic SysTick
   made `clock()` return, the native dynamic worker run, native RPMsg/NS bind,
   and `cmd=3` pass.
2. Reintroducing AP `CONFIG_TIMER_ARCH` restored `clock()` livelock before queue
   creation and prevented AP RPMsg device creation.
3. Keeping `CONFIG_TIMER_ARCH` and adding only the NuttX
   `up_enable_icache()` initialization restored AP RPMsg/NS, endpoint binding,
   bidirectional vring progress, heartbeat, and native `cmd=3`.

The vendor CPU0 and CPU1 startup paths both explicitly enable I-cache. The team
AP startup previously handled inherited D-cache but omitted CPU1's private
I-cache initialization. CP can work through its Bootloader same-core handoff,
but that does not provide a deterministic contract. The formal fix therefore
initializes I-cache on both team images and retains `TIMER_ARCH` on both.

The SYS register address and source selection were checked independently:
`SYS_BASE + 0x40` is vendor register `0x10 << 2`, and bits 29/30 are the CPU0/CPU1
32 kHz ticktimer enables. Hardware showed `0x70000000`, so a missing CPU1 32 kHz
selection is ruled out. The old CP image did not record `CCR.IC`; consequently,
the evidence supports “CP had a sufficiently initialized/faster fetch path” but
does not justify claiming the exact inherited CP I-cache bit value. The final
code removes that ambiguity by initializing I-cache on both images.

## Symptom-to-cause chain

```text
CPU1 starts AP XIP without deterministic private I-cache initialization
  -> AP CONFIG_TIMER_ARCH
  -> clock_systime_ticks()
  -> up_timer_gettick()
  -> arch_timer.current_usec()
  -> timebase/status retry never converges
  -> clock() does not return
  -> work_queue_wq() appears stuck before queue insertion
  -> rpmsg_virtio_start_worker is never queued
  -> AP /dev/rpmsg/cp is absent
  -> Name Service endpoints remain RPMSG_ADDR_ANY
```

## Baseline versus final mechanism

| Condition | `clock()` | Minimal WQ | AP RPMsg | Name Service | `cmd=3` |
|---|---|---|---|---|---|
| AP `TIMER_ARCH`, no explicit I-cache init | `clk=1/0` | not reached / blocked | `s=2 dev=0` | not bound | unavailable |
| AP periodic SysTick | `clk=1/1/100` | create/submit/worker pass | `s=10 dev=1` | `1024/1025` | pass |
| AP `TIMER_ARCH` + `up_enable_icache()` | converges | native path runs | `s=10` | `1024/1025` | pass |

The positive hardware image used native RPMsg and had no synchronous wrapper.
Its vrings advanced from `12/4,4/4` to `13/5,5/5` during the ACK+payload test.

## What worked

- An RPMsg-independent dynamic work queue reproduced the failure, moving the
  fault domain below RPMsg/OpenAMP.
- Testing both delay 0 and delay 1 ruled out a zero-delay-only watchdog issue.
- A standalone `clock()` marker localized the block before work queue creation;
  BASEPRI, PRIMASK, and TCB lockcount were all zero.
- AP periodic SysTick restored the complete native chain without changing
  RPTUN, VirtIO, RPMsg, Name Service, or polling behavior.
- Reintroducing `TIMER_ARCH` reproduced the original failure byte-for-byte.
- Enabling only AP I-cache while retaining `TIMER_ARCH` restored the complete
  native chain, identifying the CPU1 startup-state trigger.

## Ruled out

- Dynamic work-queue thread creation failure.
- RPMsg/VirtIO initialization error after the worker starts.
- RPMsg endpoint implementation or Name Service responder absence.
- Work item delay 0 as the trigger.
- Priority starvation or inherited interrupt/scheduler lock state.
- Physical CPU1 indexing UP per-CPU storage.
- Shared-memory layout, cache visibility, VirtIO roles, or `DRIVER_OK` exchange.
- RPTUN polling and RPMsg locks as the original cause.

## Formal fix

- Keep `CONFIG_TIMER=y` and `CONFIG_TIMER_ARCH=y` on CP and AP.
- Call NuttX `up_enable_icache()` in both BK7258 startup paths before normal XIP
  execution, making private instruction-cache state explicit instead of
  relying on Bootloader/reset inheritance.
- Retain the existing A2-1 D-cache policy independently; I-cache initialization
  does not make shared data coherent.
- Remove all `--wrap=rpmsg_*` options and the synchronous RPMsg start worker.
- Remove the minimal work-queue, clock-boundary, and task-state probes.

The previous periodic cleaned build (`637c...`) was a validated safe alternate.
The final I-cache/TIMER_ARCH build evidence is recorded in `experiments.tsv` and
must receive its own final hardware `rpmsg dump` and `cmd=3` gate.

Previous periodic cleaned build evidence:

```text
CP app.bin: c3c167a1e59c545e9a3e629adfc48d63a40b78f561638cf80848166e209b3d41
AP app1.bin: ccad6fd09ed4dae2fde8e8175a40f4aa83ff0941616976ef89516afd1b3831ab
all-app.bin: 637cfd9ce638a35fbfbb9a07ed6cc43c649c7395177c185f734c1ae111f3f179
```

Final deterministic I-cache/TIMER_ARCH candidate:

```text
CP app.bin: 87ed94f1174a9f4b0893221a8c1b603b5960dbfb42f447dbe398bfaf363f6966
AP app1.bin: f28b9416db8a3af65378a6d08c703bd71c5d51b2b935fe01a916b7c476977107
all-app.bin: 1f1150eeee7c7c81ff2454e2807c0b7f925d09559876cfe2b536475bb9190532
```

The AP component is byte-identical to the E08 image that passed native
`rpmsg dump` and `cmd=3`. The final combined image additionally makes CP
I-cache initialization deterministic and still requires its final hardware
gate.

## Remaining work outside this root cause

- Replace the A2-1 10 ms polling transport with BK7258 Mailbox doorbells.
- Replace whole-core D-cache disable with an MPU non-cacheable shared region or
  fully verified cache maintenance.
- Initialize DWT with the real CPU frequency before accepting ping RTT or
  throughput numbers.

## Evidence

- `baseline.md`
- `hypotheses.tsv` — H03, H06, and H09 carry the causal chain; H09 is the
  two-way confirmation.
- `experiments.tsv` — E02 through E07 contain builds, failures, hardware results,
  and image hashes.
