#ifndef __VENDOR_BEKEN_CHIP_BK7258_CLOCK_H
#define __VENDOR_BEKEN_CHIP_BK7258_CLOCK_H

#include <stdbool.h>

int bk7258_mac_clock(bool enable);
int bk7258_phy_clock(bool enable);

/* Raise the shared CPU/bus clock to Armino's highest step (CPU0 240 MHz,
 * CPU1/CPU2 480 MHz, BUS 240 MHz) through the PM frequency vote, so the CP
 * establishes it before it releases physical CPU1.  CP-only: the AP image
 * must not touch the shared clock registers. */

int bk7258_clock_cpu_boost(void);

/* Core clock measured from the CPU's own DWT cycle counter against the
 * 32 kHz-sourced system tick: independent of what the clock-select register
 * claims.  Available on both images. */

uint32_t bk7258_clock_measure_hz(void);

/* Raw DWT cycle counter, for timing a region of code.  Wraps at 2^32. */

uint32_t bk7258_clock_cycles(void);

#endif /* __VENDOR_BEKEN_CHIP_BK7258_CLOCK_H */
