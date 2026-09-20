/*
 * chips/bk7258/wifi/hal_port/hw_driver_shim.c
 *
 * NuttX implementation of the Armino sys_ctrl / sys_ll / gpio / ckmn / aon-rtc
 * callbacks the vendored glue registers into the Wi-Fi driver capability
 * table. Interrupt-enable and modem-clock callbacks map onto the team chip
 * layer register bits (bk7258_memorymap.h); power/OFDM/RC32k/GPIO remain
 * skeleton no-ops until the corresponding team drivers exist.
 */

#include <nuttx/config.h>
#include <nuttx/arch.h>
#include <nuttx/irq.h>

#include <stdint.h>
#include <stdbool.h>

#include <syslog.h>

#include <arm_internal.h>

#include <nuttx/spinlock.h>
#include <bk7258_memorymap.h>
#include <arch/chip/bk7258_sysctrl.h>

#include "sys_driver.h"
#include "sys_ll.h"
#include "gpio_driver.h"
#include "driver/ckmn.h"
#include "driver/aon_rtc.h"
#include "aon_pmu_hal.h"
#include "sys_types.h"
#include "driver/int.h"
#include <bk7258_irq.h>
#include "hal_port_sys_all.h"

struct bk7258_wifi_isr_slot
{
  int_group_isr_t callback;
  void *arg;
};

static struct bk7258_wifi_isr_slot g_wifi_isr[64];

/* Bring-up diagnostic: per-source invocation counters.  A zero count for
 * source 36 (MAC GEN) after a scan proves the GEN interrupt never reached
 * the host; a nonzero count moves the investigation into the handler. */
volatile uint32_t bk7258_wifi_isr_count[64];

static int bk7258_wifi_isr_trampoline(int irq, void *context, void *arg)
{
  struct bk7258_wifi_isr_slot *slot = arg;
  (void)irq;
  (void)context;
  if (slot != NULL && slot->callback != NULL)
    {
      unsigned idx = (unsigned)(slot - g_wifi_isr);

      bk7258_wifi_isr_count[idx]++;
      slot->callback(slot->arg);
    }
  return OK;
}

bk_err_t bk_int_isr_register(icu_int_src_t src, int_group_isr_t callback,
                             void *arg)
{
  int ret;

  if (src >= 64 || callback == NULL)
    {
      return BK_ERR_PARAM;
    }

  if (g_wifi_isr[src].callback != NULL)
    {
      bk7258_icu_disable(src);
      irq_detach(16 + src);
    }

  g_wifi_isr[src].callback = callback;
  g_wifi_isr[src].arg = arg;
  ret = bk7258_icu_attach(src, bk7258_wifi_isr_trampoline,
                          &g_wifi_isr[src]);
  if (ret < 0)
    {
      g_wifi_isr[src].callback = NULL;
      g_wifi_isr[src].arg = NULL;
      return BK_FAIL;
    }

  return bk7258_icu_enable(src) == OK ? BK_OK : BK_FAIL;
}

bk_err_t bk_int_isr_unregister(icu_int_src_t src)
{
  if (src >= 64)
    {
      return BK_ERR_PARAM;
    }

  if (g_wifi_isr[src].callback == NULL)
    {
      return BK_OK;
    }

  return bk7258_icu_vendor_unregister(src) == OK ?
         (g_wifi_isr[src].callback = NULL, g_wifi_isr[src].arg = NULL, BK_OK) :
         BK_FAIL;
}

/* Ported to hal_port/hal_port_sys_int_power.c, which drives each of the
 * seven authoritative per-interrupt enables into ITS OWN bank.  The mask
 * built here previously went to sys_drv_int_enable(), which writes only the
 * LOW bank, so the five HIGH-bank MAC bits (gen/prot/tx/rx/misc = sources
 * 32-36) instead lit five unrelated LOW-bank sources. */
extern bk_err_t hp_bk_wifi_interrupt_init(void);

bk_err_t bk_wifi_interrupt_init(void)
{
  return hp_bk_wifi_interrupt_init();
}

/****************************************************************************
 * Interrupt enable (SYS_CPU0_INT_EN / INT_EN_HI)
 ****************************************************************************/

/* Authority: sys_int_driver.c:19-56 wraps each call in
 * sys_drv_enter_critical/exit and forwards to sys_hal.c:745-793, which does a
 * read-modify-write on SYS 0x20<<2 (low bank, our BK7258_SYS_CPU0_INT_EN at
 * SYS_BASE+0x080) and 0x21<<2 (high bank, +0x084).  Base matches: authority
 * SOC_SYS_REG_BASE == 0x44010000 == BK7258_SYS_BASE.
 *
 * RETURN VALUE IS PART OF THE CONTRACT, not a status code.  The disable
 * variants return the ENTIRE enable register as it was BEFORE the clear, so a
 * caller can restore exactly what it interrupted:
 *
 *     int32 sys_hal_int_disable(uint32 param) {        // sys_hal.c:745
 *         reg = sys_ll_get_cpu0_int_0_31_en_value();
 *         value = reg;                                 // pre-change snapshot
 *         reg &= ~(param);
 *         sys_ll_set_cpu0_int_0_31_en_value(reg);
 *         return value;                                // <-- returned
 *     }
 *
 * That is what rw_task.c:78-85's WIFI_INT_DISABLE/WIFI_INT_RESTORE pair
 * consumes: it stashes the disable return in int_en_low32/int_en_high32 and
 * feeds it straight back to sys_drv_int_enable/_group2_enable.  Returning 0
 * (as this file did until 2026-09-02) turns that restore into enable(0), a
 * no-op -- every source masked by the disable would stay masked forever.
 * Those two macros happen to have no in-tree caller today, but all four
 * functions are reachable from the closed libwifi.a through the adapter table
 * (bk7258_wifi_adapter.c:985-988), where the same pairing is invisible to us.
 *
 * The enable variants return 0 in the authority too (sys_hal.c:758,784), so
 * their 0 here is correct and intentional, not the same bug.
 */

static spinlock_t g_int_en_lock = SP_UNLOCKED;

static int32_t bk7258_int_en_clear(uint32_t addr, uint32_t param)
{
  irqstate_t flags = spin_lock_irqsave(&g_int_en_lock);
  uint32_t before = getreg32(addr);

  putreg32(before & ~param, addr);
  spin_unlock_irqrestore(&g_int_en_lock, flags);

  return (int32_t)before;
}

static int32_t bk7258_int_en_set(uint32_t addr, uint32_t param)
{
  irqstate_t flags = spin_lock_irqsave(&g_int_en_lock);

  putreg32(getreg32(addr) | param, addr);
  spin_unlock_irqrestore(&g_int_en_lock, flags);

  return 0;
}

int32_t sys_drv_int_enable(uint32_t param)
{
  return bk7258_int_en_set(BK7258_SYS_CPU0_INT_EN, param);
}

int32_t sys_drv_int_disable(uint32_t param)
{
  return bk7258_int_en_clear(BK7258_SYS_CPU0_INT_EN, param);
}

int32_t sys_drv_int_group2_enable(uint32_t param)
{
  return bk7258_int_en_set(BK7258_SYS_CPU0_INT_EN_HI, param);
}

int32_t sys_drv_int_group2_disable(uint32_t param)
{
  return bk7258_int_en_clear(BK7258_SYS_CPU0_INT_EN_HI, param);
}

/****************************************************************************
 * Modem / MAC / PHY clock (SYS_DEV_CLK_EN)
 ****************************************************************************/

uint32_t sys_drv_modem_bus_clk_ctrl(bool clk_en)
{
  uint32_t v = clk_en ? BK7258_SYS_MAC_CKEN : 0;

  modifyreg32(BK7258_SYS_DEV_CLK_EN, BK7258_SYS_MAC_CKEN, v);
  return 0;
}

uint32_t sys_drv_modem_clk_ctrl(bool clk_en)
{
  uint32_t v = clk_en ? BK7258_SYS_PHY_CKEN : 0;

  modifyreg32(BK7258_SYS_DEV_CLK_EN, BK7258_SYS_PHY_CKEN, v);
  return 0;
}

/****************************************************************************
 * sys_ll clock-enable state
 ****************************************************************************/

/****************************************************************************
 * Module power state (skeleton)
 ****************************************************************************/

/* Real register read, ported from sys_hal.c:204-225 into
 * hal_port/hal_port_sys_int_power.c.  The former fixed STATE_ON return also
 * contradicted platform_shim's fixed STATE_NONE for the same register. */
extern int32_t hp_sys_drv_module_power_state_get(power_module_name_t module);

int32_t sys_drv_module_power_state_get(power_module_name_t module)
{
  return hp_sys_drv_module_power_state_get(module);
}

/****************************************************************************
 * OFDM sleep/wakeup power
 *
 * Real register semantics: the OFDM module-power bit lives in
 * SYS_POWER_WAKEUP (bit 13, POWER_MODULE_NAME_OFDM).  The pinned libwifi
 * crm_mdm_reset() polls this getter before releasing the modem reset, so a
 * cached skeleton value here silently prevented the NX MAC core from ever
 * leaving reset.  1 = domain powered down, 0 = powered, matching the
 * low-active POWER_WAKEUP register and the module-power bit numbering.
 ****************************************************************************/

void bk7258_wifi_pwd_ofdm_override(uint32_t v)
{
  /* Bidirectional again as of 2026-09-01: this is now a faithful
   * sys_ll_set_cpu_power_sleep_wakeup_pwd_ofdm, honouring both directions.
   *
   * The refusal that used to live here (added 2026-08-31) dropped every
   * down-vote from the archive on the theory that OFDM power-down was what
   * parked the MAC at FSM 0.  The authority board disproves that theory:
   * crm_mdm_reset() reads THIS bit through funcs slot +0x128 and only sets
   * CRM activeclkforce=3 when it reads 0, and the authority reads NON-zero
   * there -- its OFDM is powered DOWN at that moment -- while its FSM runs
   * normally and its scan returns results (0x49850010 = 0x108).  Refusing
   * the down-vote therefore did not protect anything; it pinned pwd_ofdm at
   * 0 and made the library take a CRM branch the authority never takes
   * (ours 0x3108 vs authority 0x108).
   *
   * Honest note on expectations: our own 2026-08-31 board run already showed
   * that allowing the down-vote moves CRM to 0x0108 and FSM STAYS 0, so this
   * change is about removing a self-inflicted divergence, not a fix.
   *
   * Bit polarity: BK7258_SYS_OFDM_POWERDOWN (bit13) is power-DOWN, so v!=0
   * sets it and v==0 clears it. */

  static spinlock_t lock = SP_UNLOCKED;
  irqstate_t flags = spin_lock_irqsave(&lock);
  uint32_t reg = getreg32(BK7258_SYS_POWER_WAKEUP);

  if (v)
    {
      reg |= BK7258_SYS_OFDM_POWERDOWN;
    }
  else
    {
      reg &= ~BK7258_SYS_OFDM_POWERDOWN;
    }

  putreg32(reg, BK7258_SYS_POWER_WAKEUP);
  spin_unlock_irqrestore(&lock, flags);
}

uint32_t bk7258_wifi_pwd_ofdm_get_override(void)
{
  return (getreg32(BK7258_SYS_POWER_WAKEUP) & BK7258_SYS_OFDM_POWERDOWN) ? 1 : 0;
}

/****************************************************************************
 * GPIO device mapping
 *
 * gpio_dev_map()/gpio_dev_unmap() used to live here as `return BK_OK`
 * no-ops (removed 2026-09-09).  They are now the authority implementations
 * imported verbatim under chips/bk7258/gpio/authority
 * (cp/middleware/driver/bk7258/gpio_driver.c:65,94), reached through the
 * authority gpio_driver.h included above.
 *
 * The no-ops were not equivalent to upstream even for the RF pins.  On
 * BK7258 CONFIG_GPIO_DEFAULT_SET_SUPPORT is set, so upstream first looks the
 * pin up in GPIO_DEFAULT_DEV_CONFIG and returns BK_ERR_GPIO_INVALID_OPERATE
 * when it is absent -- GPIO_26/TXEN and GPIO_28/RXEN are both absent from
 * that table, so upstream reports failure there rather than muxing.  A stub
 * answering BK_OK claimed success for exactly the calls upstream refuses,
 * and answered nothing for the pins upstream does configure.  Both halves
 * are now upstream's.
 ****************************************************************************/

/****************************************************************************
 * CKMN (skeleton)
 ****************************************************************************/

bk_err_t bk_ckmn_driver_init(void)
{
  return BK_OK;
}

bk_err_t bk_ckmn_driver_deinit(void)
{
  return BK_OK;
}

bk_err_t bk_ckmn_driver_get_rc32k_ppm(void)
{
  return 0;
}

/****************************************************************************
 * AON RTC
 *
 * bk_aon_rtc_driver_init()/bk_aon_rtc_driver_deinit() used to be no-op success
 * stubs here.  They never enabled the AON RTC 32k clock (ctrl bit6) nor
 * released the counter (ctrl bit1 cnt_stop), so the counter never advanced.
 * The authority AON RTC module imported under chips/bk7258/aon_rtc/ now owns
 * both entry points (authority/middleware/driver/rtc/aon_rtc_driver_64bit.c).
 ****************************************************************************/
