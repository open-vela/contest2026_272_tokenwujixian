# Shared-nuttx overlays (BK7258 SMP)

Three fixes carried as patches for the shared nuttx tree
(`/work/project/openvela-ai/nuttx`, a repo-managed checkout; its
`.git` is a symlink into `.repo/projects/nuttx.git`).

The tree is normally kept in the **applied** state -- the BK7258
build compiles it directly.  These files are the versioned copy of
that state so the changes survive `git checkout --` and can be
re-applied on a pristine tree.

## Contents

| patch | file | purpose |
|---|---|---|
| 0001 | `include/nuttx/spinlock.h` | Re-entrant (recursive) spinlock acquire via full-word CAS with bounded seize retry.  Fixes the upstream lost-update bug where a plain 16-bit `count += 1` races a foreign core's 32-bit STREX on the same word, which surfaces as `rspinlock owner/count corrupted` (0x321) panic storms on BK7258 SMP. |
| 0002 | `sched/misc/assert.c` | rspinlock corruption detector hooked at the lock operations, per-CPU history ring, and SWAP forensic recorders (`rspin_busywait_record`, `rspin_rpw_record`) writing the tables the CP monitor dumps after a stall. |
| 0003 | `drivers/rpmsg/rpmsg_virtio.c` | `rpmsg_virtio_notify_wait` yields 1 tick on timeout instead of spinning unbounded, keeping the watchdog/console contract under AP load. |

## Apply / revert

```sh
cd /work/project/openvela-ai/nuttx

# apply (pristine tree -> fixed state)
git apply --check /work/project/openvela-ai/contest2026_272_tokenwujixian/patches/shared-nuttx/000[123]-*.patch \
  && git apply /work/project/openvela-ai/contest2026_272_tokenwujixian/patches/shared-nuttx/000[123]-*.patch

# revert (back to pristine)
git checkout -- include/nuttx/spinlock.h sched/misc/assert.c drivers/rpmsg/rpmsg_virtio.c
```

## Provenance

- Applied state validates on the BK7258 AP dual-core SMP image:
  30-round reboot soak 28/30 PASS with zero silent wedges (v9).
- The board-side auto-recovery monitor (chips/ + board/ of this
  worktree) is layered on top: a generation that never reaches the
  scheduler or loses its heartbeat is restarted from the CP.
- `nuttx/openamp/open-amp` is a separate nested git repo and stays
  pristine; its optional forensic recorders are not carried.
