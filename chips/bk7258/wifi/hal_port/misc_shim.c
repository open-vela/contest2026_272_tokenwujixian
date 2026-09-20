/*
 * chips/bk7258/wifi/hal_port/misc_shim.c
 *
 * Busy-delay helpers declared in hal_port/include/bk_misc.h. bk_wifi_adapter.c
 * wraps delay() and bk_delay_us() into g_wifi_funcs, so libwifi.a calls them.
 *
 * delay() reproduces upstream's nested busy loop verbatim
 * (bk_system/delay.c:13) rather than being redirected to usleep()/up_udelay():
 * its argument is not a time unit, and the vendor runtime was tuned against
 * whatever wall time that loop takes on this core. Turning it into a real sleep
 * would both change the duration and introduce a scheduling point where the
 * vendor code expects none.
 */

#include <nuttx/config.h>
#include <nuttx/arch.h>
#include <nuttx/clock.h>
#include <nuttx/signal.h>

#include <string.h>

#include <common/bk_typedef.h>
#include <common/bk_err.h>

#include "bk_misc.h"
#include "bk_general_dma.h"
#include "stack_base.h"

/****************************************************************************
 * Public functions
 ****************************************************************************/

void delay(INT32 num)
{
  volatile INT32 i;
  volatile INT32 j;

  for (i = 0; i < num; i++)
    {
      for (j = 0; j < 100; j++)
        {
        }
    }
}

void bk_delay_us(UINT32 us)
{
  /* Upstream routes this to bk_timer_delay_us()/arch_delay_us(); up_udelay()
   * is the NuttX equivalent (busy-wait, no scheduling point).
   */

  up_udelay(us);
}

void delay_ms(UINT32 ms)
{
  /* Upstream is arch_delay_us(1000 * ms), i.e. a busy wait rather than a
   * sleep, so keep it busy here too.
   */

  up_udelay(ms * 1000);
}

void delay_sec(UINT32 sec_count)
{
  /* Upstream ignores its argument and always waits a single second
   * (bk_system/delay.c:34, `if (t1 - t0 >= 1) break`). Nothing in the compiled
   * vendored set calls this, so honour the count instead of copying that quirk
   * -- and note the difference here in case a future caller depended on it.
   */

  clock_t start = clock_systime_ticks();
  clock_t want  = (clock_t)sec_count * TICK_PER_SEC;

  while ((clock_t)(clock_systime_ticks() - start) < want)
    {
      nxsig_usleep(1000);
    }
}

void delay_tick(UINT32 tick_count)
{
  /* Same upstream quirk as delay_sec(): the argument is ignored and it waits
   * one tick. Unused in the compiled set; honour the count.
   */

  clock_t start = clock_systime_ticks();

  while ((clock_t)(clock_systime_ticks() - start) < (clock_t)tick_count)
    {
      nxsig_usleep(USEC_PER_TICK);
    }
}

/****************************************************************************
 * DMA copy (bk_general_dma.h)
 ****************************************************************************/

bk_err_t dma_memcpy(void *out, const void *in, uint32_t len)
{
  /* Live on the vendored TX path (rwnx_tx.c:247 and :256) now that
   * CONFIG_GENERAL_DMA is 1 per the prebuilt baseline. memcpy() is
   * functionally equivalent: upstream uses the DMA engine to offload CPU
   * cycles, not for different copy semantics.
   *
   * Caveat for whoever ports the real DMA driver: upstream's DMA writes bypass
   * the data cache. That difference is masked today only because
   * CONFIG_CACHE_ENABLE is off, so the two must be revisited together.
   */

  memcpy(out, in, len);
  return BK_OK;
}

/****************************************************************************
 * Crash-dump hook (stack_base.h)
 ****************************************************************************/

static hook_func g_bk7258_wifi_dump_hook;

void rtos_regist_wifi_dump_hook(hook_func wifi_func)
{
  /* Really called, from bk_wifi_adapter.c:1147 inside its
   * register_wifi_dump_hook wrapper.
   *
   * The pointer is recorded but deliberately not wired into NuttX's crash path:
   * NuttX dumps via its own assert/panic handler, and invoking vendor dump code
   * from there first needs its context requirements checked (it may log, take
   * locks, or touch MAC registers from a fault context). Keeping the pointer
   * lets a future BK7258 fault handler call it on purpose.
   */

  g_bk7258_wifi_dump_hook = wifi_func;
}
