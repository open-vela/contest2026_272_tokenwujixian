/****************************************************************************
 * chips/bk7258/bk7258_psram.c
 *
 * PSRAM: the bodies of the authority's cp/middleware/driver/psram/
 * psram_driver.c (16 symbols) and cp/middleware/soc/common/hal/psram_hal.c
 * (13 symbols), plus bk7258_psram_initialize() -- the NuttX side, whose only
 * job is to put the detected region on the heap.
 *
 * Register access is psram_ll_macro_def.h's, and the SYS/clock bits go
 * through sys_drv_psram_* and sys_drv_dev_clk_pwr_up, so the block has one
 * program: the one psram_hal.c had.  PSRAM has no NuttX device framework;
 * its "standard face" is the heap region, which is why the entry point
 * reports a size and an ID rather than registering a driver.
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
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <nuttx/arch.h>

#include "arm_internal.h"
#include "bk7258_internal.h"
#include "include/bk7258_memorymap.h"
#include "include/bk7258_psram.h"

#include "sdkconfig.h"

/* psram_ll_macro_def.h (register layer), PSRAM_MODE*, PSRAM_*_ID and the
 * psram_hal_* register macros; <driver/psram.h> the bk_psram_* contract;
 * syslog for the driver's progress lines.
 */

#include "psram_hal.h"
#include <driver/psram.h>
#include <driver/psram_types.h>
#include "sys_driver.h"
#include "sys_hal.h"
#include <syslog.h>

#include "os/os.h"

/* The board's easyflash face, used for the remembered chip id. */

#include "bk_ef.h"

/* bk_system/delay.c */

extern void bk_delay_us(uint32_t us);

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* psram_driver.c:28-42 */

#define PSRAM_4M_SIZE              (0x00400000)
#define PSRAM_8M_SIZE              (0x00800000)
#define PSRAM_16M_SIZE             (0x01000000)

#define PSRAM_THREAD_STACK_SIZE    3072
#define PSRAM_THREAD_PRIORITY      4
#define PSRAM_SEMAPHORE_COUNT      1
#define PSRAM_ADDRESS_ALIGNMENT    4
#define PSRAM_BYTES_PER_WORD       4
#define PSRAM_MAX_STRING_LEN       1024

#define PSRAM_CHECK_FLAG           0x3CA5C3A5

#define PSRAM_INIT_WAIT_TIMEOUT_MS 100

/* psram_hal.c's short register delays are busy loops, not microseconds;
 * keep them as written.
 */

#define PSRAM_HAL_DELAY_AFTER_RESET   500
#define PSRAM_HAL_DELAY_AFTER_ENABLE  3000
#define PSRAM_HAL_CMD_RETRIES         5
#define PSRAM_HAL_CMD_WAIT_ROUNDS     10
#define PSRAM_HAL_CMD_WAIT_LOOPS      5000

/****************************************************************************
 * Private Types
 ****************************************************************************/

typedef struct
{
  uint32_t psram_id;
  uint32_t magic_code;
} psram_flash_t;

/****************************************************************************
 * Private Data
 ****************************************************************************/

static bool                  s_psram_init_done;
static bool                  s_psram_heap_is_init;
static uint8_t               s_psram_channelmap;
static psram_flash_t         s_psram_id;

#if CONFIG_PSRAM_AUTO_DETECT
static bool                  s_psram_id_need_write;
static beken_semaphore_t     s_psram_sem;
static beken_thread_t        s_psram_task;
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/* psram_hal.c:22 */

static void psram_hal_delay(volatile uint32_t times)
{
  while (times--)
    {
    }
}

/****************************************************************************
 * psram_hal.c face
 ****************************************************************************/

void psram_hal_set_sf_reset(uint32_t value)
{
  psram_ll_set_sf_reset_value(value);
}

void psram_hal_set_cmd_reset(void)
{
  psram_ll_set_reg8_value(0x4);
}

void psram_hal_cmd_write(uint32_t addr, uint32_t value)
{
  psram_ll_set_write_address(addr);
  psram_ll_set_write_data(value);
  psram_ll_set_reg8_value(0x1);
  while (psram_ll_get_reg8_value() & 0x1)
    {
    }
}

/* psram_hal.c:38: five attempts around the command, and between them a
 * soft reset plus a command reset.  A dead device therefore costs the full
 * wait budget once, not forever, and the caller keeps trying both parts.
 */

uint32_t psram_hal_cmd_read(uint32_t addr)
{
  uint8_t m = 10;
  uint8_t i;

  for (i = PSRAM_HAL_CMD_RETRIES; i > 0; i--)
    {
      psram_ll_set_write_address(addr);
      psram_ll_set_reg8_value(0x2);

      m = PSRAM_HAL_CMD_WAIT_ROUNDS;
      while (psram_ll_get_reg8_value() & 0x2)
        {
          int j;

          for (j = 0; j < PSRAM_HAL_CMD_WAIT_LOOPS; j++)
            {
            }

          m--;
          if (m == 0)
            {
              break;
            }
        }

      if (m != 0)
        {
          return psram_ll_get_regb_value();
        }

      psram_hal_set_sf_reset(0);
      psram_hal_set_sf_reset(1);
      psram_hal_set_cmd_reset();
    }

  return 0;
}

void psram_hal_set_clk(psram_clk_t clk)
{
  switch (clk)
    {
      case PSRAM_240M:
        sys_drv_psram_clk_sel(1);      /* 0: 320 MHz, 1: 480 MHz */
        sys_drv_psram_set_clkdiv(0);   /* F/(2 + (1 + div)) */
        break;

      case PSRAM_160M:
        sys_drv_psram_clk_sel(0);
        sys_drv_psram_set_clkdiv(0);
        break;

      case PSRAM_120M:
        sys_drv_psram_clk_sel(1);
        sys_drv_psram_set_clkdiv(1);
        break;

      case PSRAM_80M:
        sys_drv_psram_clk_sel(0);
        sys_drv_psram_set_clkdiv(1);
        break;

      default:
        break;
    }
}

/* psram_hal.c:113: the recommended default, 120 MHz. */

void psram_hal_set_default_clk(void)
{
  psram_hal_set_clk(PSRAM_120M);
}

void psram_hal_set_voltage(psram_voltage_t voltage)
{
  switch (voltage)
    {
      case PSRAM_OUT_3_0V:
        sys_drv_psram_psldo_vset(0, 0);
        break;

      case PSRAM_OUT_1_95V:
        sys_drv_psram_psldo_vset(1, 0);
        break;

      case PSRAM_OUT_1_90V:
        sys_drv_psram_psldo_vset(1, 1);
        break;

      case PSRAM_OUT_1_85V:
        sys_drv_psram_psldo_vset(1, 2);
        break;

      case PSRAM_OUT_1_80V:
        sys_drv_psram_psldo_vset(1, 3);
        break;

      default:
        break;
    }
}

#if (CONFIG_PSRAM_WRITE_THROUGH)

static inline bool is_psram_addr_out_of_range(uint32_t addr)
{
  return ((addr < SOC_PSRAM_DATA_BASE) ||
          (addr > (SOC_PSRAM_DATA_BASE + SOC_PSRAM_DATA_SIZE)));
}

#else

static inline bool is_psram_addr_out_of_range(uint32_t addr)
{
  return false;
}

#endif

static inline bool is_32bytes_aligned(uint32_t addr)
{
  return ((addr & 0x1f) == 0);
}

int psram_hal_set_write_through(psram_write_through_area_t area,
                                uint32_t enable, uint32_t start,
                                uint32_t end)
{
#if (CONFIG_PSRAM_WRITE_THROUGH)
  if (area > PSRAM_WRITE_THROUGH_AREA_COUNT)
    {
      return BK_ERR_PSRAM_AREA;
    }

  if (enable)
    {
      if (start >= end)
        {
          return BK_ERR_PSRAM_ADDR_RELATION;
        }

      if ((!is_32bytes_aligned(start)) || (!is_32bytes_aligned(end)))
        {
          return BK_ERR_PSRAM_ADDR_ALIGN;
        }

      if (is_psram_addr_out_of_range(start) ||
          is_psram_addr_out_of_range(end))
        {
          return BK_ERR_PSRAM_ADDR_OUT_OF_RANGE;
        }

      psram_ll_set_cover_start(area, start >> 5);
      psram_ll_set_cover_stop_enable(area, BIT(31) | (end >> 5));
    }
  else
    {
      psram_ll_set_cover_stop_enable(area, 0);
    }

  return BK_OK;
#else
  return BK_FAIL;
#endif
}

static int psram_hal_w955d8mky_5j_init(uint32_t *id);

void psram_hal_set_transfer_mode(uint32_t value)
{
  psram_ll_set_reg4_wrap_config(value);
}

/* psram_hal.c:196: APS6408L. */

static int psram_hal_aps6408l_init(uint32_t *id)
{
  uint32_t val;

  psram_hal_set_mode_value(PSRAM_MODE6);
  psram_hal_set_reg5_value(0x380);
  psram_hal_set_cmd_reset();
  psram_hal_delay(PSRAM_HAL_DELAY_AFTER_RESET);

  val = psram_hal_cmd_read(0x00000000);
  if (val == 0 || val != *id)
    {
      return -1;
    }

  *id = val;

  val = psram_hal_get_regb_value();
  val = (val & ~(0x1F)) | (0x4 << 2) | 0x3;
  psram_hal_cmd_write(0x00000000, val);
  psram_hal_cmd_read(0x00000000);
  psram_hal_cmd_read(0x00000004);

  val = psram_hal_get_regb_value();
  val = (val & ~(0x7 << 5)) | (0x6 << 5);   /* write latency, 166 MHz */
  psram_hal_cmd_write(0x00000004, val);
  return 0;
}

/* psram_hal.c:222: W955D8MKY_5J.  Not populated on this board, but the
 * contract has it and the stored-id path can name it.
 */

static int psram_hal_w955d8mky_5j_init(uint32_t *id)
{
  uint32_t val;
  uint32_t io_drv = 0;   /* range [0, 3] */

  psram_hal_set_mode_value(PSRAM_MODE4);
  psram_hal_set_reg5_value(0x292);
  psram_hal_set_cmd_reset();
  psram_hal_delay(PSRAM_HAL_DELAY_AFTER_RESET);

  val = psram_hal_cmd_read(0x01000000);
  val = 0x1c8f | (io_drv << 4);
  psram_hal_cmd_write(0x01000000, val);
  psram_hal_cmd_read(0x01000000);
  return 0;
}

/* psram_hal.c:257: APS128XXO. */

static int psram_hal_aps128xxo_ob9_init(uint32_t *id)
{
  uint32_t val;

  psram_hal_set_mode_value(PSRAM_MODE7);
  psram_hal_set_reg5_value(0x380);
  psram_hal_set_cmd_reset();
  psram_hal_delay(PSRAM_HAL_DELAY_AFTER_RESET);

  val = psram_hal_cmd_read(0x00000000);
  if (val == 0 || val != *id)
    {
      return -1;
    }

  *id = val;

  val = psram_hal_get_regb_value();
  val = (val & ~(0x1F)) | (0x6 << 2) | 0x2;
  psram_hal_cmd_write(0x00000000, val);

  psram_hal_cmd_read(0x00000004);
  val = psram_hal_get_regb_value();
  val = (val & ~(0x7 << 5)) | (0x6 << 5);
  psram_hal_cmd_write(0x00000004, val);

  psram_hal_cmd_read(0x00000008);
  val = psram_hal_get_regb_value();
  val |= 0x40;
  psram_hal_cmd_write(0x00000008, val);
  return 0;
}

/* psram_hal.c:307: id == 0 means "try each part in turn".  Returns the id
 * that answered, or 0.
 */

uint32_t psram_hal_config_init(uint32_t id)
{
  uint32_t val;
  uint32_t type = id;

  psram_hal_set_sf_reset(1);

  /* Bypass: the controller passes commands through instead of decoding
   * them from the memory window.
   */

  val = psram_hal_get_reg2_value();
  val |= (0x1 << 1);
  psram_hal_set_reg2_value(val);

  if (id != 0)
    {
      if (id == PSRAM_APS6408L_ID)
        {
          psram_hal_aps6408l_init(&type);
          return type;
        }
      else if (id == PSRAM_APS128XXO_OB9_ID)
        {
          psram_hal_aps128xxo_ob9_init(&type);
          return type;
        }
      else
        {
          psram_hal_w955d8mky_5j_init(&type);
          return type;
        }
    }

  type = PSRAM_APS6408L_ID;
  if (psram_hal_aps6408l_init(&type) == 0)
    {
      return type;
    }

  type = PSRAM_APS128XXO_OB9_ID;
  if (psram_hal_aps128xxo_ob9_init(&type) == 0)
    {
      return type;
    }

  return 0;
}

void psram_hal_power_clk_enable(uint8_t enable)
{
  if (enable)
    {
      psram_hal_delay(PSRAM_HAL_DELAY_AFTER_RESET);
      sys_drv_psram_ldo_enable(1);
      bk_delay_us(1000);

      /* The AHBP domain owns the PSRAM controller.  The authority votes for
       * it through the PM layer; see psram_hw_init() for why this port calls
       * the HAL entry instead.
       */

      sys_hal_module_power_ctrl(5 /* POWER_MODULE_NAME_AHBP */,
                                0 /* POWER_MODULE_STATE_ON */);
      sys_drv_psram_psram_disckg(1);

      psram_hal_set_clk(PSRAM_80M);
      sys_drv_dev_clk_pwr_up(CLK_PWR_ID_PSRAM, CLK_PWR_CTRL_PWR_UP);
    }
  else
    {
      bk_psram_heap_init_flag_set(false);
      sys_hal_module_power_ctrl(5 /* AHBP */,
                                1 /* POWER_MODULE_STATE_OFF */);
      psram_hal_set_sf_reset(0);
      sys_drv_dev_clk_pwr_up(CLK_PWR_ID_PSRAM, CLK_PWR_CTRL_PWR_DOWN);
      sys_drv_psram_ldo_enable(0);
    }

  psram_hal_delay(PSRAM_HAL_DELAY_AFTER_ENABLE);
}

/* psram_hal.c:381 and :432 are inside #if (CONFIG_SOC_BK7256XX); on this SoC
 * both are empty, and they stay empty here so the contract still has them.
 */

void psram_hal_reset(void)
{
}

void psram_hal_config(void)
{
}

/****************************************************************************
 * psram_driver.c face
 ****************************************************************************/

bk_err_t bk_psram_set_clk(psram_clk_t clk)
{
  psram_hal_set_clk(clk);
  return BK_OK;
}

bk_err_t bk_psram_heap_init_flag_set(bool init)
{
  s_psram_heap_is_init = init;
  return BK_OK;
}

bool bk_psram_heap_init_flag_get(void)
{
  return s_psram_heap_is_init;
}

bk_err_t bk_psram_set_voltage(psram_voltage_t voltage)
{
  psram_hal_set_voltage(voltage);
  return BK_OK;
}

bk_err_t bk_psram_set_transfer_mode(psram_tansfer_mode_t transfer_mode)
{
  psram_hal_set_transfer_mode(transfer_mode);
  return BK_OK;
}

/* The channel map is a bitmap update.  The authority serialises it with a
 * mutex it creates in bk_psram_init(); a critical section gives the same
 * guarantee without an Armino mutex in this path.
 */

psram_write_through_area_t bk_psram_alloc_write_through_channel(void)
{
  uint8_t channel = 0;
  irqstate_t flags;

  flags = up_irq_save();
  for (channel = 0; channel < PSRAM_WRITE_THROUGH_AREA_COUNT; channel++)
    {
      if ((s_psram_channelmap & (0x1 << channel)) == 0)
        {
          s_psram_channelmap |= (0x1 << channel);
          break;
        }
    }

  up_irq_restore(flags);
  return channel;
}

bk_err_t bk_psram_free_write_through_channel(psram_write_through_area_t area)
{
  irqstate_t flags;

  if (area >= PSRAM_WRITE_THROUGH_AREA_COUNT)
    {
      syslog(LOG_ERR, "%s over range failed\r\n", __func__);
      return BK_ERR_PARAM;
    }

  flags = up_irq_save();
  if (s_psram_channelmap & (0x1 << area))
    {
      s_psram_channelmap &= ~(0x1 << area);
    }

  up_irq_restore(flags);
  return BK_OK;
}

bk_err_t bk_psram_enable_write_through(psram_write_through_area_t area,
                                       uint32_t start, uint32_t end)
{
  return psram_hal_set_write_through(area, 1, start, end);
}

bk_err_t bk_psram_disable_write_through(psram_write_through_area_t area)
{
  return psram_hal_set_write_through(area, 0, 0, 0);
}

bk_err_t bk_psram_calibrate(void)
{
  /* CONFIG_PSRAM_CALIBRATE is 1 and the authority's body is empty either
   * way; kept so the symbol and its return value stay the same.
   */

  return BK_OK;
}

/****************************************************************************
 * The chip id remembered in easyflash
 *
 * psram_driver.c:174-200 writes it from a short-lived thread so that the
 * sector erase does not sit inside PSRAM init.  Same here: the semaphore is
 * posted by bk_psram_init() when the detected id differs from the stored
 * one.
 */

#if CONFIG_PSRAM_AUTO_DETECT

static void psram_id_write(beken_thread_arg_t data)
{
  rtos_get_semaphore(&s_psram_sem, BEKEN_WAIT_FOREVER);

  if (s_psram_id_need_write)
    {
      bk_set_env_enhance(PSRAM_CHIP_ID, (const void *)&s_psram_id,
                         sizeof(psram_flash_t));
    }

  syslog(LOG_DEBUG, "psram id write to flash success\r\n");
  s_psram_id_need_write = false;
  rtos_deinit_semaphore(&s_psram_sem);
  s_psram_sem = NULL;
  s_psram_task = NULL;
  rtos_delete_thread(NULL);
}

#endif

bk_err_t bk_psram_id_auto_detect(void)
{
#if (CONFIG_PSRAM_AUTO_DETECT)
  int ret = bk_get_env_enhance(PSRAM_CHIP_ID, (void *)&s_psram_id,
                               sizeof(psram_flash_t));

  if (ret != 8)
    {
      syslog(LOG_DEBUG, "Auto detect:No PSRAM_CHIP_ID INFO, ret:%d\r\n", ret);
    }

  if (s_psram_id.magic_code == PSRAM_CHECK_FLAG)
    {
      return BK_OK;
    }

  if (s_psram_sem == NULL)
    {
      ret = rtos_init_semaphore(&s_psram_sem, PSRAM_SEMAPHORE_COUNT);
      if (ret != BK_OK)
        {
          syslog(LOG_ERR, "%s, init s_psram_sem error\r\n", __func__);
          return ret;
        }
    }

  ret = rtos_create_thread(&s_psram_task, PSRAM_THREAD_PRIORITY, "psram_task",
                           (beken_thread_function_t)psram_id_write,
                           PSRAM_THREAD_STACK_SIZE, NULL);
  if (BK_OK != ret)
    {
      syslog(LOG_ERR, "%s psram_task init failed, ret:%d\n", __func__, ret);
      rtos_deinit_semaphore(&s_psram_sem);
      s_psram_sem = NULL;
      return ret;
    }

#endif
  return BK_OK;
}

/****************************************************************************
 * Init / deinit
 ****************************************************************************/

/* The sequence of psram_driver.c:300-330: set the rail, power the block,
 * read the chip id, then move to the operating clock.  Shared by the Armino
 * face and by bk7258_psram_initialize() so the hardware is programmed once.
 *
 * The AHBP power vote is the one difference from the authority: it calls
 * sys_hal_module_power_ctrl() rather than bk_pm_module_vote_power_ctrl(),
 * because this runs from arm_addregion(), before the PM module is up, and
 * because PSRAM is a permanent heap region whose claim never drops, so the
 * vote layer's reference counting buys nothing here.  When T12 brings the
 * authority's pm.c over, this becomes the AHBP_PSRAM submodule vote.
 */

static uint32_t psram_hw_init(uint32_t chip_id)
{
  uint32_t actual_id;

  bk_psram_set_voltage(PSRAM_OUT_1_95V);
  psram_hal_power_clk_enable(1);

  actual_id = psram_hal_config_init(chip_id);
  if (actual_id == 0)
    {
      return 0;
    }

  bk_delay_us(1000);
  psram_hal_set_default_clk();
  return actual_id;
}

static size_t psram_size_of(uint32_t id)
{
  switch (id)
    {
      case PSRAM_APS6408L_ID:
        return PSRAM_8M_SIZE;

      case PSRAM_APS128XXO_OB9_ID:
        return PSRAM_16M_SIZE;

      default:
        return 0;
    }
}

bk_err_t bk_psram_init(void)
{
  uint32_t chip_id = 0;
  uint32_t actual_id;

  /* The authority guards this with a mutex it creates on first use.  Here
   * there is no concurrent caller to guard against: the first call comes
   * from arm_addregion(), before nx_start(), and every later caller only
   * reads the done flag.
   */

  if (s_psram_init_done)
    {
      return BK_OK;
    }

  syslog(LOG_DEBUG, "Starting PSRAM init...\n");

  if (s_psram_id.magic_code == PSRAM_CHECK_FLAG)
    {
      chip_id = s_psram_id.psram_id;
    }

  syslog(LOG_DEBUG, "%s, chip_id:%lx\r\n", __func__,
             (unsigned long)chip_id);

  actual_id = psram_hw_init(chip_id);
  if (actual_id == 0)
    {
      syslog(LOG_ERR, "%s, fail!\r\n", __func__);
      return BK_FAIL;
    }

  syslog(LOG_DEBUG, "%s, %lx-%lx\r\n", __func__,
             (unsigned long)actual_id, (unsigned long)chip_id);

  /* psram_driver.c:331-344: warn when the part does not match the capacity
   * the build was configured for.
   */

  if (actual_id != chip_id)
    {
      s_psram_id.psram_id = actual_id;
      s_psram_id.magic_code = PSRAM_CHECK_FLAG;
#if (CONFIG_PSRAM_AUTO_DETECT)
      if (s_psram_sem)
        {
          s_psram_id_need_write = true;
          rtos_set_semaphore(&s_psram_sem);
        }
#endif
    }
  else
    {
#if (CONFIG_PSRAM_AUTO_DETECT)
      if (s_psram_sem)
        {
          rtos_set_semaphore(&s_psram_sem);
        }
#endif
    }

  s_psram_init_done = true;
  syslog(LOG_DEBUG, "PSRAM init success\n");
  return BK_OK;
}

bk_err_t bk_psram_deinit(void)
{
  if (!s_psram_init_done)
    {
      return BK_OK;
    }

  psram_hal_power_clk_enable(0);
  s_psram_init_done = false;
  syslog(LOG_DEBUG, "PSRAM deinit done\n");
  return BK_OK;
}

/****************************************************************************
 * Byte access to the PSRAM window
 ****************************************************************************/

#define PSRAM_RETURN_ON_SERVER_NOT_INIT() \
  do \
    { \
      if (!s_psram_init_done) \
        { \
          return BK_ERR_PSRAM_SERVER_NOT_INIT; \
        } \
    } \
  while (0)

bk_err_t bk_psram_memcpy(uint8_t *start_addr, uint8_t *data_buf, uint32_t len)
{
  uint8_t *pb;
  uint8_t *pd;
  uint32_t val;
  int i;

  PSRAM_RETURN_ON_SERVER_NOT_INIT();

  if (((uint32_t)start_addr & (PSRAM_ADDRESS_ALIGNMENT - 1)) != 0 ||
      ((uint32_t)data_buf & (PSRAM_ADDRESS_ALIGNMENT - 1)) != 0)
    {
      syslog(LOG_ERR, "address not aligned to %d bytes\r\n",
                 PSRAM_ADDRESS_ALIGNMENT);
      return BK_FAIL;
    }

  while (len)
    {
      if (len < PSRAM_BYTES_PER_WORD)
        {
          /* Read-modify-write the last partial word. */

          val = *((uint32_t *)(start_addr));
          pb = (uint8_t *)&val;
          pd = (uint8_t *)data_buf;
          for (i = 0; i < len; i++)
            {
              *pb++ = *pd++;
            }

          *(uint32_t *)(start_addr) = val;
          len = 0;
        }
      else
        {
          val = *((uint32_t *)data_buf);
          *(uint32_t *)(start_addr) = val;
          data_buf += PSRAM_BYTES_PER_WORD;
          start_addr += PSRAM_BYTES_PER_WORD;
          len -= PSRAM_BYTES_PER_WORD;
        }
    }

  return BK_OK;
}

bk_err_t bk_psram_memread(uint8_t *start_addr, uint8_t *data_buf, uint32_t len)
{
  uint8_t *pb;
  uint8_t *pd;
  uint32_t val;
  int i;

  PSRAM_RETURN_ON_SERVER_NOT_INIT();

  if (((uint32_t)start_addr & (PSRAM_ADDRESS_ALIGNMENT - 1)) != 0 ||
      ((uint32_t)data_buf & (PSRAM_ADDRESS_ALIGNMENT - 1)) != 0)
    {
      syslog(LOG_ERR, "address not aligned to %d bytes\r\n",
                 PSRAM_ADDRESS_ALIGNMENT);
      return BK_FAIL;
    }

  while (len)
    {
      if (len < PSRAM_BYTES_PER_WORD)
        {
          val = *((uint32_t *)(start_addr));
          pb = (uint8_t *)&val;
          pd = (uint8_t *)data_buf;
          for (i = 0; i < len; i++)
            {
              *pd++ = *pb++;
            }

          len = 0;
        }
      else
        {
          val = *((uint32_t *)(start_addr));
          *((uint32_t *)data_buf) = val;
          data_buf += PSRAM_BYTES_PER_WORD;
          start_addr += PSRAM_BYTES_PER_WORD;
          len -= PSRAM_BYTES_PER_WORD;
        }
    }

  return BK_OK;
}

char *bk_psram_strcat(char *start_addr, const char *data_buf)
{
  uint8_t *pb;
  uint8_t *pd = (uint8_t *)data_buf;
  uint32_t max_iterations = PSRAM_MAX_STRING_LEN / PSRAM_BYTES_PER_WORD;
  uint32_t iteration_count = 0;
  uint32_t val;
  int i;
  int j;

  if (!s_psram_init_done)
    {
      return NULL;
    }

  if (start_addr == NULL || data_buf == NULL)
    {
      return NULL;
    }

  if (*pd == '\0')
    {
      return start_addr;
    }

  do
    {
      if (iteration_count++ > max_iterations)
        {
          syslog(LOG_ERR, "String too long or no null terminator found\r\n");
          return NULL;
        }

      val = *((uint32_t *)(start_addr));
      pb = (uint8_t *)&val;

      /* Find the end of the existing string inside this word, append the
       * source from there, and stop after the terminator.
       */

      for (i = 0; i < PSRAM_BYTES_PER_WORD; i++)
        {
          if (*(pb + i) == '\0')
            {
              for (j = i; j < PSRAM_BYTES_PER_WORD && *pd != '\0'; j++)
                {
                  *(pb + j) = *pd++;
                }

              if (*pd == '\0' && j < PSRAM_BYTES_PER_WORD)
                {
                  *(pb + j) = '\0';
                }

              *(uint32_t *)(start_addr) = val;

              /* The authority returns the address of the word it just
               * wrote, not the end of the string.
               */

              if (*pd == '\0')
                {
                  return start_addr;
                }

              break;
            }
        }

      /* No terminator in this word: move on. */

      if (i == PSRAM_BYTES_PER_WORD)
        {
          start_addr += PSRAM_BYTES_PER_WORD;
        }
    }
  while (*pd != '\0');

  return start_addr;
}

/****************************************************************************
 * NuttX entry
 *
 * arm_addregion() is the caller: PSRAM has no /dev node, so the region it
 * reports IS its standard face.  The hardware sequence is psram_hw_init(),
 * the same one bk_psram_init() uses; once this has run, a later
 * bk_psram_init() from driver_init sees s_psram_init_done and returns.
 ****************************************************************************/

/* Not from the authority: the id handshake proves the command interface
 * works, not that the array holds data.  Writes a pattern at both ends of
 * the region and puts the original words back.
 */

static int bk7258_psram_probe(size_t size)
{
  volatile uint32_t *memory = (volatile uint32_t *)BK7258_PSRAM_BASE;
  size_t words = CONFIG_BK7258_PSRAM_PROBE_SIZE / sizeof(*memory);
  size_t i;
  uint32_t saved0;
  uint32_t savedlast;
  const uint32_t patterns[] =
  {
    UINT32_C(0x00000000), UINT32_C(0xffffffff), UINT32_C(0x5aa55aa5)
  };

  if (words == 0 || CONFIG_BK7258_PSRAM_PROBE_SIZE > size)
    {
      return -EINVAL;
    }

  saved0 = memory[0];
  savedlast = memory[words - 1];

  for (i = 0; i < sizeof(patterns) / sizeof(patterns[0]); i++)
    {
      memory[0] = patterns[i];
      memory[words - 1] = ~patterns[i];
      __asm__ volatile ("dsb" : : : "memory");

      if (memory[0] != patterns[i] || memory[words - 1] != ~patterns[i])
        {
          memory[0] = saved0;
          memory[words - 1] = savedlast;
          return -EIO;
        }
    }

  memory[0] = saved0;
  memory[words - 1] = savedlast;
  __asm__ volatile ("dsb" : : : "memory");
  return OK;
}

int bk7258_psram_initialize(size_t *size, uint16_t *device_id)
{
  uint32_t chip_id = 0;
  uint32_t actual_id;
  size_t detected_size;
  int ret;

  if (size == NULL || device_id == NULL)
    {
      return -EINVAL;
    }

  if (s_psram_init_done)
    {
      detected_size = psram_size_of(s_psram_id.psram_id);
      if (detected_size == 0)
        {
          return -EIO;
        }

      *size = detected_size;
      *device_id = (uint16_t)s_psram_id.psram_id;
      return OK;
    }

  if (s_psram_id.magic_code == PSRAM_CHECK_FLAG)
    {
      chip_id = s_psram_id.psram_id;
    }

  BK7258_BOOT_MARK('1');
  actual_id = psram_hw_init(chip_id);
  BK7258_BOOT_MARK('2');
  if (actual_id == 0)
    {
      return -ENODEV;
    }

  detected_size = psram_size_of(actual_id);
  if (detected_size == 0)
    {
      return -ENODEV;
    }

  /* Pattern check before the region goes on the heap: a part that answers
   * its ID but cannot hold data would otherwise corrupt the allocator.
   */

  ret = bk7258_psram_probe(detected_size);
  if (ret < 0)
    {
      return ret;
    }

  s_psram_id.psram_id = actual_id;
  s_psram_id.magic_code = PSRAM_CHECK_FLAG;
  s_psram_init_done = true;
  s_psram_heap_is_init = true;

  *size = detected_size;
  *device_id = (uint16_t)actual_id;
  return OK;
}


