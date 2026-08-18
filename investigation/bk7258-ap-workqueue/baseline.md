# BK7258 AP dynamic work queue baseline

## Contract

- Symptom: on the AP image, the first native `rpmsg_queue_work()` enters but
  does not return; the `rpmsg-cp-0` worker exists and waits on its semaphore.
- Reproduction: boot the native (no synchronous start-work wrapper) CP/AP
  image and observe `[AMP-W] q=1/INT_MIN reg=0 init=0` with AP `dev=0`.
- Known-good controls:
  - the CP native RPMsg path reaches RPMsg device creation;
  - synchronously executing only the AP RPMsg start work makes
    `queue/register/init` return zero and allows two successful `cmd=3` pings.
- Scope: team-owned BK7258 chip/board sources only. Do not modify upstream
  NuttX during diagnosis. If the defect is generic, prepare an upstream patch
  proposal separately.
- Stop: identify a cause with two-way reversal, remove all RPMsg wrappers and
  synchronous start work, then complete native `cmd=3` on hardware.

## Measured baseline

### Native AP path (failure)

```text
[AMP-C] s=10 ... v=04
[AMP-A] s=9 ... v=04 dev=0
[AMP-T] rptun=4/2/90 rpmsg=5/5/224
[AMP-W] q=1/-2147483648 reg=0/0 init=0/0
```

Interpretation: `rpmsg_queue_work()` entered but did not return. The dynamic
worker exists and waits on its semaphore. `rpmsg_register()` and
`rpmsg_init_vdev_with_config()` were never entered.

### Synchronous first-work path (functional control)

```text
[AMP-C] s=10 ... v=04
[AMP-A] s=10 ... v=04 dev=1
[AMP-W] q=1/0 reg=1/0 init=1/0
[AMP-R] raw=10/2,4/4 c=10/2,4/4 a=10/2,4/4
```

Hardware command completed twice:

```text
rpmsg ping /dev/rpmsg/ap 1 64 3 10
core info: cp <-> ap
ping times: 1
buffer_len: 496, send_len: 64
```

Historical successful image SHA-256:

```text
ba2df21e0e20a8bffe9037a26e0e1a1fdef6721f7371dbf074290eb66738239e
```

This control proves the shared-memory, RPTUN, VirtIO, RPMsg, Name Service,
payload-check and ACK path. It is not an acceptable final implementation.
