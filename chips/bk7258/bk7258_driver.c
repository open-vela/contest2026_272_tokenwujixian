/****************************************************************************
 * chips/bk7258/bk7258_driver.c
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Armino driver bring-up
 *
 * The two symbols of middleware/driver/common/driver.c:
 *
 *   driver_early_init()   interrupt controller, AON PMU, TRNG, eFuse
 *   driver_init()         every driver's init, in the authority's order
 *
 * Both are on the boot path, so the port is the authority's call list and
 * nothing else: same order, same #if gates, same comments about what must
 * happen before what (GPIO before the ATE detect and the UART; nothing logs
 * before the UART is up).  The one local change the baseline already carried
 * is kept -- PSRAM is brought up here rather than only from a test command.
 *
 * CONFIG_SPI is a name collision: NuttX's <nuttx/config.h> defines it for its
 * own SPI layer, while the Armino name means "the Armino SPI driver is
 * built", which it is not.  The baseline unit handles this through
 * compat/bk7258_armino_nuttx_config.h; a chip-layer TU has to do it itself,
 * hence the #undef below.
 *
 * Every branch carries the header that supplies the init it names, inside the
 * same gate, so flipping a CONFIG_* brings its dependency with it.
 *
 * CP only.
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>

#ifndef CONFIG_BK7258_COMPONENT_AP

#include "sdkconfig.h"

/* NuttX's SPI switch is not Armino's; see the note above. */

#undef CONFIG_SPI

#include <common/bk_err.h>
#include <components/ate.h>
#include <components/bk_uid.h>
#include <components/log.h>
#include <driver/adc.h>
#include <driver/aon_rtc.h>
#include <driver/aon_wdt.h>
#include <driver/ckmn.h>
#include <driver/dma.h>
#include <driver/efuse.h>
#include <driver/gpio.h>
#include <driver/int.h>
#include <driver/psram.h>
#include <driver/timer.h>
#include <driver/trng.h>
#include <driver/uart.h>
#include <driver/wdt.h>
#include <os/mem.h>

#include "bk_drv_model.h"
#include "dd_pub.h"
#include "interrupt_base.h"
#include "sys_driver.h"
#include "include/bk7258_clock.h"

#if CONFIG_AON_PMU
#include "aon_pmu_driver.h"
#endif

#if CONFIG_FLASH
#include <driver/flash.h>
#endif

#if CONFIG_EASY_FLASH
#include "bk_ef.h"
#endif

#if CONFIG_QSPI
#include <driver/qspi.h>
#endif

#if CONFIG_JPEGENC_HW
#include <driver/jpeg_enc.h>
#endif

#if CONFIG_H264
#include <driver/h264.h>
#endif

#if CONFIG_YUV_BUF
#include <driver/yuv_buf.h>
#endif

#if CONFIG_CALENDAR
#include <driver/calendar.h>
#endif

#if ((CONFIG_SDIO_HOST) || (CONFIG_SDCARD))
#include <driver/sdio_host.h>
#endif

#if CONFIG_TOUCH_PM_SUPPORT
#include <driver/touch.h>
#endif

#if CONFIG_SDMADC
#include <driver/sdmadc.h>
#endif

#if CONFIG_HW_ROTATE_PFC
#include <driver/rott_driver.h>
#endif

#if CONFIG_LIN
#include <driver/lin.h>
#endif

/****************************************************************************
 * Private Data
 ****************************************************************************/

#if CONFIG_POWER_CLOCK_RF
#define MODULES_POWER_OFF_ENABLE (1)
#define ROSC_DEBUG_EN            (0)
#define MODULES_CLK_ENABLE       (0)

extern void clock_dco_cali(uint32_t speed);

/****************************************************************************
 * Power, clock and RF bring-up
 *
 * Compiled out today (CONFIG_POWER_CLOCK_RF is undefined); carried whole so a
 * flip of that switch lands on the authority's sequence rather than on a stub.
 ****************************************************************************/

void power_clk_rf_init(void)
{
  uint32_t param = 0;

  /* Power on all the modules for the bring-up test */

  module_name_t use_module = MODULE_NAME_WIFI;

  /* 1. Power on all the modules */

#if MODULES_POWER_OFF_ENABLE
  sys_drv_module_power_ctrl(POWER_MODULE_NAME_ENCP, POWER_MODULE_STATE_OFF);
  sys_drv_module_power_ctrl(POWER_MODULE_NAME_AUDP, POWER_MODULE_STATE_OFF);
  sys_drv_module_power_ctrl(POWER_MODULE_NAME_VIDP, POWER_MODULE_STATE_OFF);
  sys_drv_module_power_ctrl(POWER_MODULE_NAME_BTSP, POWER_MODULE_STATE_OFF);
  sys_drv_module_power_ctrl(POWER_MODULE_NAME_WIFIP_MAC,
                            POWER_MODULE_STATE_OFF);
  sys_drv_module_power_ctrl(POWER_MODULE_NAME_WIFI_PHY,
                            POWER_MODULE_STATE_OFF);
  sys_drv_module_power_ctrl(POWER_MODULE_NAME_CPU1, POWER_MODULE_STATE_OFF);
#else
  power_module_name_t module = POWER_MODULE_NAME_MEM1;

  for (module = POWER_MODULE_NAME_MEM1; module < POWER_MODULE_NAME_NONE;
       module++)
    {
      sys_drv_module_power_ctrl(module, POWER_MODULE_STATE_ON);
    }
#endif

  /* 2. Enable the analog clock */

  sys_drv_module_RF_power_ctrl(use_module, POWER_MODULE_STATE_ON);

  /* 3. Enable all the modules' clocks */

#if MODULES_CLK_ENABLE
  dev_clk_pwr_id_t devid = 0;

  for (devid = 0; devid < 32; devid++)
    {
      sys_drv_dev_clk_pwr_up(devid, CLK_PWR_CTRL_PWR_UP);
    }
#endif

  /* 4. cpu0 and matrix clock: 26m/26m, 120m/120m and 240m/120m are the three
   *    configurations the authority records; it ships with none of them
   *    applied (the sys_drv_core_bus_clock_ctrl calls are commented out) and
   *    leaves the CPU frequency vote to PM.  This whole block is behind
   *    CONFIG_POWER_CLOCK_RF, which is off here, so the 480 MHz vote lives in
   *    bk7258_armino_early_initialize() next to pm_hardware_init() instead.
   */

  /* 5. Rosc calibration: enable the temperature detect used for VIO, hand
   *    rosc to the BT/Wi-Fi IPs, then start the 1 s calibration interval.
   */

  param = 0;
  param = sys_drv_analog_get(ANALOG_REG6);
  param |= (0x1 << SYS_ANA_REG6_EN_TEMPDET_POS) |
           (0x7 << SYS_ANA_REG6_RXTAL_LP_POS) |
           (0x7 << SYS_ANA_REG6_RXTAL_HP_POS);
  param &= ~(0x1 << SYS_ANA_REG6_EN_SLEEP_POS);
  sys_drv_analog_set(ANALOG_REG6, param);

  param = 0;
  param = aon_pmu_drv_reg_get(PMU_REG0x41);
  param |= 0x1 << 24;
  aon_pmu_drv_reg_set(PMU_REG0x41, param);

  /* Rosc debug: tap CK_TST out of analog reg 5/11 and REG4. */

#if ROSC_DEBUG_EN
  param = 0;
  param = sys_drv_analog_get(ANALOG_REG5);
  param |= 0x1 << SYS_ANA_REG5_CK_TST_ENBALE_POS;
  sys_drv_analog_set(ANALOG_REG5, param);

  param = 0;
  param = sys_drv_analog_get(ANALOG_REG11);
  param |= 0x1 << SYS_ANA_REG11_TEST_EN_POS;
  sys_drv_analog_set(ANALOG_REG11, param);

  param = 0;
  param = sys_drv_analog_get(ANALOG_REG4);
  param |= (0x1 << SYS_ANA_REG4_ROSC_TSTEN_POS);
  sys_drv_analog_set(ANALOG_REG4, param);
#endif

  /* Rosc calibration: 1 s interval, 32 kHz, automatic (not manual) mode. */

  param = 0;
  param = sys_drv_analog_get(ANALOG_REG4);
  param &= ~(SYS_ANA_REG4_ROSC_CAL_INTVAL_MASK <<
             SYS_ANA_REG4_ROSC_CAL_INTVAL_POS);
  param |= 0x4 << SYS_ANA_REG4_ROSC_CAL_INTVAL_POS;
  param |= 0x1 << SYS_ANA_REG4_ROSC_CAL_MODE_POS;
  param |= 0x1 << SYS_ANA_REG4_ROSC_CAL_EN_POS;
  param &= ~(0x1 << SYS_ANA_REG4_ROSC_MANU_EN_POS);
  sys_drv_analog_set(ANALOG_REG4, param);

  /* Trigger the calibration: clear, then set. */

  param = 0;
  param = sys_drv_analog_get(ANALOG_REG4);
  param &= ~(0x1 << SYS_ANA_REG4_ROSC_CAL_TRIG_POS);
  sys_drv_analog_set(ANALOG_REG4, param);

  param = 0;
  param = sys_drv_analog_get(ANALOG_REG4);
  param |= (0x1 << SYS_ANA_REG4_ROSC_CAL_TRIG_POS);
  sys_drv_analog_set(ANALOG_REG4, param);
}
#endif /* CONFIG_POWER_CLOCK_RF */

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int driver_early_init(void)
{
  interrupt_init();

#if CONFIG_AON_PMU
  aon_pmu_drv_init();
#endif

#if CONFIG_POWER_CLOCK_RF
  power_clk_rf_init();
#endif

#if CONFIG_TRNG_SUPPORT
  bk_trng_driver_init();
#endif

#if CONFIG_EFUSE
  bk_efuse_driver_init();
#endif

  return 0;
}

int driver_init(void)
{
  sys_drv_init();

  bk_gpio_driver_init();

  /* ATE uses the UART TX pin as its detect pin, so it has to run after GPIO
   * init and before UART init -- otherwise either ATE or the UART breaks.
   */

#if CONFIG_ATE
  bk_ate_init();
#endif

  /* Before the UART is initialised, any BK_LOG_RAW / os_print / BK_LOGx call
   * may crash.  This is the point where logging becomes safe.
   */

  bk_uart_driver_init();

#if CONFIG_CHIP_SUPPORT
  if (!bk_is_chip_supported())
    {
      return BK_FAIL;
    }
#endif

  drv_model_init();

  g_dd_init();

#if CONFIG_TIMER
  bk_timer_driver_init();
#endif

#if CONFIG_GENERAL_DMA
  bk_dma_driver_init();
#endif

  bk_wdt_driver_init();

#if CONFIG_AON_WDT && !CONFIG_INT_AON_WDT
  bk_aon_wdt_stop();
#endif

#if CONFIG_MAILBOX
  /* bk_ipc_init() is declared in cp/middleware/driver/mailbox/bk_api_ipc.h,
   * which cannot be included here: that directory's sibling headers redeclare
   * the mailbox types mb_ipc/mailbox_driver_base.h already owns.
   */

  extern int bk_ipc_init(void);
  extern bk_err_t ipc_init(void);
  extern bk_err_t mb_ipc_init(void);

  ipc_init();
#if CONFIG_MAILBOX_IPC
  mb_ipc_init();
#endif
  bk_ipc_init();
#endif

  os_show_memory_config_info();

#if CONFIG_FLASH
  bk_flash_driver_init();
#if CONFIG_FLASH_ORIGIN_API
  extern int hal_flash_init(void);
  hal_flash_init();
#endif
#endif

#if CONFIG_EASY_FLASH
  easyflash_init();
#endif

#if CONFIG_SARADC
  bk_adc_driver_init();
#endif

#if CONFIG_SPI
  /* The authority calls this without a declaration: the SPI driver unit does
   * not ship in the baseline tree, so neither does its header.
   */

  bk_spi_driver_init();
#endif

#if CONFIG_I2C
  bk_i2c_driver_init();
#endif

#if CONFIG_QSPI
  bk_qspi_driver_init();
#endif

#if CONFIG_YUV_BUF
  bk_yuv_buf_driver_init();
#endif

#if CONFIG_JPEGENC_HW
  bk_jpeg_enc_driver_init();
#endif

#if CONFIG_H264
  bk_h264_driver_init();
#endif

#if CONFIG_AON_RTC
  bk_aon_rtc_driver_init();
#endif

#if ((CONFIG_SDIO_HOST) || (CONFIG_SDCARD))
  bk_sdio_host_driver_init();
#endif

#if CONFIG_CALENDAR
  bk_calendar_driver_init();
#endif

  /* Call it after the log is valid. */

#if CONFIG_ATE
  BK_LOGD(NULL, "ate enabled is %d\r\n", ate_is_enabled());
#endif

#if CONFIG_TOUCH_PM_SUPPORT
  bk_touch_pm_init();
#endif

#if CONFIG_SDMADC
  /* bk_sdmadc_driver_init(); -- the authority does not call it either. */
#endif

#if CONFIG_HW_ROTATE_PFC
  /* bk_rott_driver_init(); -- nor this one. */
#endif

#if CONFIG_CKMN
  bk_ckmn_driver_init();
#endif

#if CONFIG_LIN
  bk_lin_driver_init();
#endif

#if (CONFIG_TRUSTENGINE)
  extern int dubhe_driver_init(unsigned long dbh_base_addr);
  dubhe_driver_init(SOC_SHANHAI_BASE);
#endif

#if CONFIG_GET_UID_ENABLE
  bk_uid_driver_init();
#endif

#if CONFIG_PSRAM
  /* Bring PSRAM up by default at boot, instead of only when a test command
   * runs.  bk_psram_init is idempotent, so repeated calls are safe.
   */

  {
    bk_err_t psram_ret = bk_psram_init();

    if (psram_ret != BK_OK)
      {
        BK_LOGE(NULL, "driver_init: bk_psram_init fail:%d\r\n", psram_ret);
      }
    else
      {
        BK_LOGI(NULL, "driver_init: psram init ok\r\n");
      }
  }
#endif

  BK_LOGD(NULL, "driver_init end\r\n");

  return 0;
}

#endif /* !CONFIG_BK7258_COMPONENT_AP */
