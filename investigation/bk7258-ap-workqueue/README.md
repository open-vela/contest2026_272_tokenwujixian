# BK7258 AP dynamic work queue root-cause investigation

This directory contains the auditable evidence for removing the temporary AP
RPMsg synchronous start-work workaround.

- `baseline.md`: hardware-observed native failure and synchronous control.
- `hypotheses.tsv`: predictions, tests, results and status for every candidate.
- `experiments.tsv`: one row per build/hardware experiment, including failures.

The final acceptance condition is native, unwrapped RPMsg completing `cmd=3`
on hardware. A workaround that merely makes the command pass is not a
root-cause fix.
