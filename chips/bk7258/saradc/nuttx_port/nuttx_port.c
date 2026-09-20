/*
 * NuttX/SoC boundary for directly imported Armino BK7258 SARADC sources.
 * It deliberately implements no adc_hal_* functions: authority adc_hal.c
 * and adc_ll.h own those operations.
 */

#include <nuttx/config.h>

#include <stdint.h>

#include <nuttx/arch.h>
#include <nuttx/clock.h>

#include <syslog.h>

#include <arm_internal.h>

#include <bk7258_memorymap.h>
#include <bk7258_sysctrl.h>

#include <bk_drv_model.h>
#include <bk_saradc.h>
#include <common/bk_err.h>
#include <sys_driver.h>

#include <os/os.h>

/* cp/include/driver/adc_types.h from the Armino authority.  The imported
 * SARADC driver headers do not expose this callback-specific result through
 * the current NuttX include set, but callers rely on its precise value. */
#ifndef BK_ERR_SARADC_WAIT_CB_NOT_REGISTER
#  define BK_ERR_SARADC_WAIT_CB_NOT_REGISTER (BK_ERR_ADC_BASE - 11)
#endif

/* This is the CONFIG_SARADC_MB=n authority fallback. The separate authority
 * mailbox service is activated only with its RPMsg client/server ABI. */

static void (*g_saradc_op_notify)(uint32_t param);

bk_err_t mb_saradc_register_op_notify(void *notify_cb)
{
  g_saradc_op_notify = (void (*)(uint32_t))notify_cb;
  return BK_OK;
}

bk_err_t mb_saradc_unregister_op_notify(void *notify_cb)
{
  if (g_saradc_op_notify != notify_cb)
    {
      return BK_ERR_SARADC_WAIT_CB_NOT_REGISTER;
    }

  g_saradc_op_notify = NULL;
  return BK_OK;
}

bk_err_t mb_saradc_ipc_init(void)
{
  return BK_OK;
}

/*
 * Armino's adc_block_read() waits for adc_isr() to give adc_read_sema with
 * xSemaphoreTake(timeout).  The imported driver and its IRQ completion model
 * remain intact here.  BK7258 CP's current NuttX port, however, cannot use a
 * finite nxsem_tickwait for this conversion: its SysTick timeout path has
 * demonstrated an invalid idle-TCB waiter (sem_waitirq.c:137).  Polling the
 * semaphore with NuttX's non-blocking primitive preserves the externally
 * visible contract -- completion consumes exactly one ISR post; no completion
 * returns failure at the original deadline -- without registering a task
 * waitdog against corrupted scheduler state.
 *
 * This is deliberately SARADC-local.  It does not change the generic Beken
 * RTOS semaphore adapter used by Wi-Fi request/confirmation paths.
 */

bk_err_t bk7258_saradc_wait_complete(beken_semaphore_t *semaphore,
                                     uint32_t timeout_ms)
{
  clock_t start;
  clock_t timeout;

  if (semaphore == NULL || *semaphore == NULL)
    {
      return BK_FAIL;
    }

  if (timeout_ms == 0)
    {
      return rtos_get_semaphore(semaphore, 0);
    }

  start = clock_systime_ticks();
  timeout = MSEC2TICK(timeout_ms);

  for (;;)
    {
      if (rtos_get_semaphore(semaphore, 0) == BK_OK)
        {
          return BK_OK;
        }

      if (clock_systime_ticks() - start >= timeout)
        {
          return BK_FAIL;
        }

      /* ADC conversion completion is interrupt-driven.  Keep interrupts
       * enabled and yield a short bounded hardware delay between polls. */

      up_udelay(10);
    }
}

/* sys_hal_set_ana_reg_spi_latch1v() removed 2026-09-09: the authority
 * definition (sys_hal.c:95) is now imported under pm/authority.  The local
 * copy restated ANA_REG9 bit 9 by hand; both reached the same register --
 * our analog index 2 equals authority word index 0x42 because ANALOG_BASE is
 * SYS_BASE + (0x40 << 2) -- but there is no reason to keep a second
 * definition of a field the generated LL header already describes.
 */

void sys_drv_sadc_int_enable(void)
{
  (void)sys_drv_int_enable(BK7258_SYS_SADC_INT_EN);
}

void sys_drv_sadc_int_disable(void)
{
  (void)sys_drv_int_disable(BK7258_SYS_SADC_INT_EN);
}

void sys_drv_sadc_pwr_up(void)
{
  modifyreg32(BK7258_SYS_DEV_CLK_EN, 0, BK7258_SYS_SADC_CLK_EN);
  (void)bk7258_analog_update_bits(2, UINT32_C(1) << 15,
                                  UINT32_C(1) << 15);
}

void sys_drv_sadc_pwr_down(void)
{
  (void)bk7258_analog_update_bits(2, UINT32_C(1) << 15, 0);
  modifyreg32(BK7258_SYS_DEV_CLK_EN, BK7258_SYS_SADC_CLK_EN, 0);
}

void sys_drv_analog_reg4_bits_or(uint32_t value)
{
  (void)bk7258_analog_update_bits(4, value, value);
}

UINT32 sddev_control(UINT32 device, UINT32 command, void *param)
{
  if (device != DD_DEV_TYPE_SCTRL || param == NULL)
    {
      return (UINT32)BK_FAIL;
    }

  if (command == CMD_SCTRL_BLK_ENABLE &&
      *(const uint32_t *)param == BLK_BIT_SARADC)
    {
      sys_drv_sadc_pwr_up();
    }

  return BK_OK;
}

/* sys_hal_set_saradc_config() removed 2026-09-09 for the authority definition
 * (sys_hal.c:1316).  The six fields were transcribed correctly -- verified
 * field by field against sys_ll.h -- but they were transcribed, and the
 * generated accessors now compile in this tree.
 */

bk_err_t mb_saradc_op_prepare(void)
{
  if (g_saradc_op_notify != NULL)
    {
      g_saradc_op_notify(0);
    }

  return BK_OK;
}

bk_err_t mb_saradc_op_finish(void)
{
  if (g_saradc_op_notify != NULL)
    {
      g_saradc_op_notify(1);
    }

  return BK_OK;
}
