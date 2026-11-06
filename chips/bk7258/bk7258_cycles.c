/****************************************************************************
 * chips/bk7258/bk7258_cycles.c
 *
 * Core-cycle measurement, shared by both images.
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>

#include <nuttx/clock.h>

#include "arm_internal.h"
#include "include/bk7258_clock.h"

/* Measure the core clock from the CPU's own cycle counter.
 *
 * The clock-select register only says what software asked for; DWT_CYCCNT
 * counts what the core actually executed.  Cycles divided by wall-clock ticks
 * (NuttX ticks come from the 32 kHz SysTick source, so they are independent of
 * the core clock) is the real frequency.  Used before and after the vote, the
 * ratio is the proof that the frequency changed -- no register decode needed.
 */

#define BK7258_DWT_CTRL       UINT32_C(0xe0001000)
#define BK7258_DWT_CYCCNT     UINT32_C(0xe0001004)
#define BK7258_DEMCR          UINT32_C(0xe000edfc)
#define BK7258_DEMCR_TRCENA   (UINT32_C(1) << 24)
#define BK7258_DWT_CTRL_CYCCNTENA (UINT32_C(1) << 0)
#define BK7258_HZ_MEASURE_TICKS   50      /* wall window, ~50 ms at 1 kHz */

uint32_t bk7258_clock_cycles(void)
{
  return getreg32(BK7258_DWT_CYCCNT);
}

uint32_t bk7258_clock_measure_hz(void)
{
  clock_t t0;
  clock_t elapsed;
  uint32_t c0;
  uint32_t c1;

  /* Enable the trace unit and the free-running cycle counter. */

  modifyreg32(BK7258_DEMCR, 0, BK7258_DEMCR_TRCENA);
  modifyreg32(BK7258_DWT_CTRL, 0, BK7258_DWT_CTRL_CYCCNTENA);

  c0 = getreg32(BK7258_DWT_CYCCNT);
  t0 = clock_systime_ticks();

  /* Spin on an ALU-only loop: no memory traffic, so the cycle count reflects
   * the core clock rather than flash wait states. */

  do
    {
      elapsed = clock_systime_ticks() - t0;
    }
  while (elapsed < BK7258_HZ_MEASURE_TICKS);

  c1 = getreg32(BK7258_DWT_CYCCNT);

  if (elapsed == 0)
    {
      return 0;
    }

  return (uint32_t)(((uint64_t)(c1 - c0) * (uint64_t)USEC_PER_SEC) /
                    ((uint64_t)elapsed * (uint64_t)USEC_PER_TICK));
}

