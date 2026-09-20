/****************************************************************************
 * chips/bk7258/bk7258_sysctrl.c
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include <nuttx/spinlock.h>
#include <syslog.h>

#include "arm_internal.h"
#include "include/bk7258_memorymap.h"
#include "include/bk7258_sysctrl.h"

int bk7258_analog_read(unsigned int reg, uint32_t *value);
int bk7258_analog_write(unsigned int reg, uint32_t value);

/* Analog registers are shared by PSRAM, SARADC and the Wi-Fi PHY.  A
 * field-level update must retain the lock until the SPI transaction completes,
 * otherwise another writer can overwrite a stale register image. */

static spinlock_t g_bk7258_analog_lock = SP_UNLOCKED;

static int bk7258_pmu_valid(unsigned int reg)
{
  return reg < 0x80 ? OK : -EINVAL;
}

static int bk7258_analog_write_locked(unsigned int reg, uint32_t value)
{
  unsigned int count;

  putreg32(value, BK7258_SYS_ANALOG_BASE + (reg << 2));
  for (count = 0; count < 1000; count++)
    {
      if ((getreg32(BK7258_SYS_ANALOG_STATE) &
           (UINT32_C(1) << (BK7258_SYS_ANALOG_STATE_SHIFT + reg))) == 0)
        {
          return OK;
        }
    }

  return -ETIMEDOUT;
}

int bk7258_pmu_read(unsigned int reg, uint32_t *value)
{
  if (value == NULL || bk7258_pmu_valid(reg) < 0)
    {
      return -EINVAL;
    }

  *value = getreg32(BK7258_AON_PMU_BASE + (reg << 2));
  return OK;
}

int bk7258_pmu_write(unsigned int reg, uint32_t value)
{
  if (bk7258_pmu_valid(reg) < 0)
    {
      return -EINVAL;
    }

  putreg32(value, BK7258_AON_PMU_BASE + (reg << 2));
  return OK;
}

int bk7258_analog_read(unsigned int reg, uint32_t *value)
{
  if (value == NULL || reg >= 28)
    {
      return -EINVAL;
    }

  *value = getreg32(BK7258_SYS_ANALOG_BASE + (reg << 2));
  return OK;
}

int bk7258_analog_write(unsigned int reg, uint32_t value)
{
  irqstate_t flags;
  int ret;

  if (reg >= 28)
    {
      return -EINVAL;
    }

  flags = spin_lock_irqsave(&g_bk7258_analog_lock);
  ret = bk7258_analog_write_locked(reg, value);
  spin_unlock_irqrestore(&g_bk7258_analog_lock, flags);
  return ret;
}

int bk7258_analog_update_bits(unsigned int reg, uint32_t mask,
                               uint32_t value)
{
  irqstate_t flags;
  uint32_t current;
  int ret;

  if (reg >= 28)
    {
      return -EINVAL;
    }

  flags = spin_lock_irqsave(&g_bk7258_analog_lock);
  current = getreg32(BK7258_SYS_ANALOG_BASE + (reg << 2));
  current = (current & ~mask) | (value & mask);
  ret = bk7258_analog_write_locked(reg, current);
  spin_unlock_irqrestore(&g_bk7258_analog_lock, flags);
  return ret;
}

int bk7258_pmu_get_chipid(uint32_t *value)
{
  uint32_t raw;
  int ret = bk7258_pmu_read(0x7c, &raw);
  if (ret == OK && value != NULL)
    {
      *value = raw;
    }
  return value == NULL ? -EINVAL : ret;
}

int bk7258_pmu_get_adc_cal(uint32_t *value)
{
  uint32_t raw;
  int ret = bk7258_pmu_read(0x7d, &raw);
  if (ret == OK && value != NULL)
    {
      *value = (raw >> 9) & UINT32_C(0x3f);
    }
  return value == NULL ? -EINVAL : ret;
}

int bk7258_pmu_get_bgcal(uint32_t *value)
{
  uint32_t raw;
  /* BK7258 SDK aon_pmu_hal_bias_cal_get() reads PMU R7E.cbcal[4:0].
   * R7D contains the ADC calibration fields and is not the bias trim. */
  int ret = bk7258_pmu_read(0x7e, &raw);
  if (ret == OK && value != NULL)
    {
      *value = raw & UINT32_C(0x1f);
    }
  return value == NULL ? -EINVAL : ret;
}

int bk7258_sys_get_bgcalm(uint32_t *value)
{
  uint32_t raw;
  int ret = bk7258_analog_read(8, &raw);
  if (ret == OK && value != NULL)
    {
      *value = (raw >> 22) & UINT32_C(0x3f);
    }
  return value == NULL ? -EINVAL : ret;
}

int bk7258_sys_set_bgcalm(uint32_t value)
{
  int ret;

  if (value > UINT32_C(0x3f))
    {
      return -EINVAL;
    }

  ret = bk7258_analog_update_bits(9, UINT32_C(1) << 9,
                                  UINT32_C(1) << 9);
  if (ret < 0)
    {
      return ret;
    }

  ret = bk7258_analog_update_bits(8, UINT32_C(0x3f) << 22,
                                  value << 22);
  /* Always release the latch, including on a failed analog transaction. */
  if (bk7258_analog_update_bits(9, UINT32_C(1) << 9, 0) < 0 && ret == OK)
    {
      ret = -EIO;
    }
  return ret;
}

int bk7258_dpll_enable(bool enable)
{
  return bk7258_analog_update_bits(5, UINT32_C(1) << 5,
                                   enable ? UINT32_C(1) << 5 : 0);
}

int bk7258_cali_dpll(uint32_t param)
{
  irqstate_t flags;
  uint32_t value;
  volatile unsigned int delay;
  int ret;

  /* BK7258 Armino sys_drv_cali_dpll() keeps this exact four-write sequence
   * inside one critical section.  Hold the ANA lock across the RMWs and their
   * busy waits so another ANA writer cannot split the trigger/detect edges. */
  flags = spin_lock_irqsave(&g_bk7258_analog_lock);

  value = getreg32(BK7258_SYS_ANALOG_BASE);
  value &= ~(UINT32_C(1) << 19); /* ANA0 spitrig */
  ret = bk7258_analog_write_locked(0, value);
  if (ret < 0)
    {
      goto out;
    }

  for (delay = 0; delay < (param == 0 ? 120U : 60U); delay++)
    {
    }

  value = getreg32(BK7258_SYS_ANALOG_BASE);
  value |= UINT32_C(1) << 19; /* ANA0 spitrig */
  ret = bk7258_analog_write_locked(0, value);
  if (ret < 0)
    {
      goto out;
    }

  value = getreg32(BK7258_SYS_ANALOG_BASE);
  value &= ~(UINT32_C(1) << 4); /* ANA0 spideten */
  ret = bk7258_analog_write_locked(0, value);
  if (ret < 0)
    {
      goto out;
    }

  for (delay = 0; delay < (param == 0 ? 3400U : 340U); delay++)
    {
    }

  value = getreg32(BK7258_SYS_ANALOG_BASE);
  value |= UINT32_C(1) << 4; /* ANA0 spideten */
  ret = bk7258_analog_write_locked(0, value);

out:
  spin_unlock_irqrestore(&g_bk7258_analog_lock, flags);
  return ret;
}

static int bk7258_power_gate(uint32_t mask, bool enable)
{
  irqstate_t flags;
  int ret;

  /* The Armino register is low-active: 1 means power down. */

  flags = enter_critical_section();
  modifyreg32(BK7258_SYS_POWER_WAKEUP, mask, enable ? 0 : mask);
  ret = ((getreg32(BK7258_SYS_POWER_WAKEUP) & mask) == 0) == enable ?
        OK : -EIO;
  leave_critical_section(flags);

  /* Errors only: the read-back check above is the useful part, and the SARADC
   * sampler power-gates its domain on every cycle, so logging every success
   * drowned the capture. */

  if (ret < 0)
    {
      syslog(LOG_ERR,
             "[BK7258] power gate mask=0x%08lx enable=%d ret=%d reg=0x%08lx\n",
             (unsigned long)mask, enable, ret,
             (unsigned long)getreg32(BK7258_SYS_POWER_WAKEUP));
    }

  return ret;
}

int bk7258_mac_power(bool enable)
{
  return bk7258_power_gate(BK7258_SYS_WIFI_MAC_POWERDOWN, enable);
}

int bk7258_ofdm_power(bool enable)
{
  return bk7258_power_gate(BK7258_SYS_OFDM_POWERDOWN, enable);
}

int bk7258_bakp_power(bool enable)
{
  /* BK7258 SYS CPU_POWER_SLEEP_WAKEUP.pwd_bakp is bit 4.  The gate is
   * low-active, exactly like MAC/PHY/OFDM above. */
  return bk7258_power_gate(UINT32_C(1) << 4, enable);
}

int bk7258_phy_power(bool enable)
{
  return bk7258_power_gate(BK7258_SYS_WIFI_PHY_POWERDOWN, enable);
}

int bk7258_mac_reset(void)
{
  /* Armino sys_hal_mac_subsys_reset() is explicitly TODO on BK7258. */

  syslog(LOG_INFO, "[BK7258] MAC reset unsupported\n");
  return -ENOTSUP;
}

int bk7258_phy_reset(void)
{
  /* Armino sys_hal_modem_core/subsys_reset() are explicitly TODO on BK7258. */

  syslog(LOG_INFO, "[BK7258] PHY reset unsupported\n");
  return -ENOTSUP;
}
