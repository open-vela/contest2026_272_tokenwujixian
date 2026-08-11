# BK7258 DevKit AP component — blocked

`configs/ap/defconfig` reserves a configuration identity for the future
`app1.bin` component. It is intentionally not buildable at this stage.

Before enabling an AP build, freeze and verify all of the following:

- AP Flash/XIP, SRAM and vector-table contracts;
- CPU1/CPU2 topology, startup and SMP policy;
- CP-to-AP reset, boot-address and mailbox handoff;
- AP interrupt, clock, UART and timer ownership;
- an independent AP linker/startup implementation and `app1.bin` export;
- real-board evidence that the CP-launched AP component starts as intended.

The current L0 package uses a SHA-256-locked vendor `app1.bin` input. It does
not build, start or validate OpenVela AP firmware.
