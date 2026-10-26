/****************************************************************************
 * chips/bk7258/bk7258_gpio.c
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to you under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stdint.h>

#include <nuttx/irq.h>
#include <nuttx/spinlock.h>
#include <syslog.h>

#include "arm_internal.h"
#include "bk7258_internal.h"
#include "include/bk7258_gpio.h"
#include "include/bk7258_memorymap.h"
#include "include/irq.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* The BK7258 GPIO interrupt control is part of each per-pin register in the
 * AON GPIO block.  ICU source 55 and both aggregate pending registers are
 * owned exclusively by the AP image; CP builds retain only non-IRQ GPIO and
 * UART/pin-mux support.
 */

#define BK7258_GPIO_INT_STATUS0  (BK7258_AON_GPIO_BASE + UINT32_C(0x100))
#define BK7258_GPIO_INT_STATUS1  (BK7258_AON_GPIO_BASE + UINT32_C(0x104))
#define BK7258_GPIO_STATUS1_MASK UINT32_C(0x00ffffff)
#define BK7258_GPIO_STABLE_LOOPS 1000

#define BK7258_GPIO_COUNT        56

#define GPIO_IRQ_SLOTS           BK7258_GPIO_MAX_PIN

/****************************************************************************
 * Private Types
 ****************************************************************************/

#ifdef CONFIG_BK7258_COMPONENT_AP
struct bk7258_gpio_irq_s
{
  xcpt_t handler;
  void  *arg;
  bool   rising;
  bool   falling;
};
#endif

/****************************************************************************
 * Private Data
 ****************************************************************************/

#ifdef CONFIG_BK7258_COMPONENT_AP
static struct bk7258_gpio_irq_s g_gpio_irqs[GPIO_IRQ_SLOTS];
static bool g_gpio_irq_initialized;
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void bk7258_gpio_select_func_locked(unsigned int pin)
{
  uint32_t reg;
  uint32_t regval;

  reg     = BK7258_SYS_GPIO_FUNC(pin);
  regval  = getreg32(reg);
  regval &= ~BK7258_GPIO_FUNC_MASK(pin);
  putreg32(regval, reg);
}

static inline bool bk7258_gpio_valid(unsigned int pin)
{
  return pin < BK7258_GPIO_MAX_PIN;
}

#ifdef CONFIG_BK7258_COMPONENT_AP
static void bk7258_gpio_set_opposite_level_irq_locked(unsigned int pin,
                                                       bool high)
{
  uint32_t regval;

  regval  = getreg32(BK7258_GPIO_CFG(pin));
  regval &= ~BK7258_GPIO_INT_TYPE_MASK;
  regval |= high ? BK7258_GPIO_INT_LOW_LEVEL :
                   BK7258_GPIO_INT_HIGH_LEVEL;
  putreg32(regval, BK7258_GPIO_CFG(pin));
}

static void bk7258_gpio_set_irq_type_locked(unsigned int pin, bool rising)
{
  uint32_t regval;
  uint32_t type;

  regval = getreg32(BK7258_GPIO_CFG(pin));
  type    = rising ? BK7258_GPIO_INT_RISING_EDGE :
                     BK7258_GPIO_INT_FALLING_EDGE;
  regval &= ~BK7258_GPIO_INT_TYPE_MASK;
  regval |= type;
  putreg32(regval, BK7258_GPIO_CFG(pin));
}

static void bk7258_gpio_clear_irq_locked(unsigned int pin)
{
  uint32_t regval;
  uint32_t bit;

  regval = getreg32(BK7258_GPIO_CFG(pin));
  regval |= BK7258_GPIO_INT_CLEAR;
  putreg32(regval, BK7258_GPIO_CFG(pin));

  bit = UINT32_C(1) << (pin & 31);
  putreg32(bit, pin < 32 ? BK7258_GPIO_INT_STATUS0 :
                            BK7258_GPIO_INT_STATUS1);
}

static void bk7258_gpio_enable_irq_locked(unsigned int pin)
{
  uint32_t regval;

  regval  = getreg32(BK7258_GPIO_CFG(pin));
  regval &= ~BK7258_GPIO_INT_CLEAR;
  regval |= BK7258_GPIO_INT_ENABLE;
  putreg32(regval, BK7258_GPIO_CFG(pin));
}

static void bk7258_gpio_rearm_opposite_level_locked(unsigned int pin)
{
  bool high;
  uint32_t regval;

  regval  = getreg32(BK7258_GPIO_CFG(pin));
  high    = (regval & BK7258_GPIO_CFG_INPUT) != 0;
  regval &= ~BK7258_GPIO_INT_ENABLE;
  putreg32(regval, BK7258_GPIO_CFG(pin));

  /* Arm the level opposite the sampled state.  If the input changes while
   * the interrupt is disabled, cleared, or reconfigured, the new level
   * remains asserted after re-enable and the hardware raises source 55
   * again.  A transition that returns to the sampled level is a net no-op.
   */

  bk7258_gpio_clear_irq_locked(pin);
  bk7258_gpio_set_opposite_level_irq_locked(pin, high);
  bk7258_gpio_enable_irq_locked(pin);
}

static void bk7258_gpio_route_irq(void)
{
  modifyreg32(BK7258_SYS_CPU1_INT_EN_HI, 0, BK7258_SYS_GPIO_INT_EN);
}

static int bk7258_gpio_interrupt(int irq, void *context, void *arg)
{
  (void)irq;
  (void)context;
  (void)arg;

  bk7258_gpio_dispatch(context);
  return OK;
}
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int bk7258_gpio_config_input(unsigned int pin,
                             enum bk7258_gpio_pull_e pull)
{
  irqstate_t flags;
  uint32_t regval;

  if (!bk7258_gpio_valid(pin) || pull < BK7258_GPIO_PULL_NONE ||
      pull > BK7258_GPIO_PULL_DOWN)
    {
      return -EINVAL;
    }

  flags = enter_critical_section();
  bk7258_gpio_select_func_locked(pin);

  regval  = getreg32(BK7258_GPIO_CFG(pin));
  regval &= ~(BK7258_GPIO_CFG_MODE_MASK |
              BK7258_GPIO_CFG_PULL_MASK |
              BK7258_GPIO_CFG_SECOND_FUNC);
  regval |= BK7258_GPIO_CFG_MODE_INPUT;

  switch (pull)
    {
      case BK7258_GPIO_PULL_UP:
        regval |= BK7258_GPIO_CFG_PULL_UP;
        break;

      case BK7258_GPIO_PULL_DOWN:
        regval |= BK7258_GPIO_CFG_PULL_DOWN;
        break;

      default:
        break;
    }

  putreg32(regval, BK7258_GPIO_CFG(pin));
  leave_critical_section(flags);
  return OK;
}

int bk7258_gpio_config_output(unsigned int pin, bool initial)
{
  irqstate_t flags;
  uint32_t regval;

  if (!bk7258_gpio_valid(pin))
    {
      return -EINVAL;
    }

  flags = enter_critical_section();
  bk7258_gpio_select_func_locked(pin);

  /* Program the output latch while the output driver is still disabled, then
   * switch the pin to output mode.  Keeping both writes in one critical
   * section prevents another register update from introducing a glitch.
   */

  regval = getreg32(BK7258_GPIO_CFG(pin));
  if (initial)
    {
      regval |= BK7258_GPIO_CFG_OUTPUT;
    }
  else
    {
      regval &= ~BK7258_GPIO_CFG_OUTPUT;
    }

  putreg32(regval, BK7258_GPIO_CFG(pin));

  regval &= ~(BK7258_GPIO_CFG_MODE_MASK |
              BK7258_GPIO_CFG_PULL_MASK |
              BK7258_GPIO_CFG_SECOND_FUNC);
  regval |= BK7258_GPIO_CFG_MODE_OUTPUT;
  putreg32(regval, BK7258_GPIO_CFG(pin));
  leave_critical_section(flags);
  return OK;
}

void bk7258_gpio_write(unsigned int pin, bool high)
{
  irqstate_t flags;
  uint32_t regval;

  if (!bk7258_gpio_valid(pin))
    {
      return;
    }

  flags  = enter_critical_section();
  regval = getreg32(BK7258_GPIO_CFG(pin));
  if (high)
    {
      regval |= BK7258_GPIO_CFG_OUTPUT;
    }
  else
    {
      regval &= ~BK7258_GPIO_CFG_OUTPUT;
    }

  putreg32(regval, BK7258_GPIO_CFG(pin));
  leave_critical_section(flags);
}

bool bk7258_gpio_read(unsigned int pin)
{
  if (!bk7258_gpio_valid(pin))
    {
      return false;
    }

  return (getreg32(BK7258_GPIO_CFG(pin)) & BK7258_GPIO_CFG_INPUT) != 0;
}

int bk7258_gpio_irq_attach(unsigned int pin, xcpt_t handler, void *arg)
{
#ifndef CONFIG_BK7258_COMPONENT_AP
  (void)pin;
  (void)handler;
  (void)arg;
  return -ENOSYS;
#else
  irqstate_t flags;

  if (!bk7258_gpio_valid(pin))
    {
      return -EINVAL;
    }

  flags = enter_critical_section();
  g_gpio_irqs[pin].handler = handler;
  g_gpio_irqs[pin].arg     = arg;
  leave_critical_section(flags);
  return OK;
#endif
}

int bk7258_gpio_irq_enable(unsigned int pin, bool rising, bool falling)
{
#ifndef CONFIG_BK7258_COMPONENT_AP
  (void)pin;
  (void)rising;
  (void)falling;
  return -ENOSYS;
#else
  volatile unsigned int delay;
  irqstate_t flags;
  uint32_t regval;
  int ret;

  if (!bk7258_gpio_valid(pin) || (!rising && !falling))
    {
      return -EINVAL;
    }

  /* Keep first-time source routing and pin setup in one interrupt-disabled
   * interval.  A retained AON pending bit cannot run the shared ISR until the
   * selected pin has been cleared and fully configured.
   */

  flags = enter_critical_section();
  ret = bk7258_gpio_irq_initialize();
  if (ret < 0)
    {
      leave_critical_section(flags);
      return ret;
    }

  regval  = getreg32(BK7258_GPIO_CFG(pin));
  regval &= ~BK7258_GPIO_INT_ENABLE;
  putreg32(regval, BK7258_GPIO_CFG(pin));

  g_gpio_irqs[pin].rising  = rising;
  g_gpio_irqs[pin].falling = falling;

  /* Match the vendor driver's input-stability delay before reporting the pin
   * to the interrupt controller.  Clear stale state after the delay.  A
   * dual-edge request is then armed as the level opposite the final sample.
   */

  for (delay = 0; delay < BK7258_GPIO_STABLE_LOOPS; delay++)
    {
    }

  if (rising && falling)
    {
      bk7258_gpio_rearm_opposite_level_locked(pin);
    }
  else
    {
      bk7258_gpio_clear_irq_locked(pin);
      bk7258_gpio_set_irq_type_locked(pin, rising);
      bk7258_gpio_enable_irq_locked(pin);
    }

  leave_critical_section(flags);
  return OK;
#endif
}

void bk7258_gpio_irq_disable(unsigned int pin)
{
#ifdef CONFIG_BK7258_COMPONENT_AP
  irqstate_t flags;
  uint32_t regval;

  if (!bk7258_gpio_valid(pin))
    {
      return;
    }

  flags = enter_critical_section();
  regval  = getreg32(BK7258_GPIO_CFG(pin));
  regval &= ~BK7258_GPIO_INT_ENABLE;
  putreg32(regval, BK7258_GPIO_CFG(pin));
  bk7258_gpio_clear_irq_locked(pin);
  g_gpio_irqs[pin].rising  = false;
  g_gpio_irqs[pin].falling = false;
  leave_critical_section(flags);
#else
  (void)pin;
#endif
}

static void bk7258_gpio_uart0(unsigned int pin)
{
  irqstate_t flags;
  uint32_t regval;

  flags  = enter_critical_section();
  regval = getreg32(BK7258_GPIO_CFG(pin));
  regval &= ~(BK7258_GPIO_CFG_MODE_MASK | BK7258_GPIO_CFG_PULL_MASK);
  regval |= BK7258_GPIO_CFG_MODE_DIS | BK7258_GPIO_CFG_PULL_UP |
            BK7258_GPIO_CFG_SECOND_FUNC;
  putreg32(regval, BK7258_GPIO_CFG(pin));

  regval  = getreg32(BK7258_SYS_GPIO_FUNC(pin));
  regval &= ~BK7258_GPIO_FUNC_MASK(pin);
  putreg32(regval, BK7258_SYS_GPIO_FUNC(pin));
  leave_critical_section(flags);
}

void bk7258_gpio_uart0_tx(void)
{
  bk7258_gpio_uart0(BK7258_GPIO11);
}

void bk7258_gpio_uart0_rx(void)
{
  bk7258_gpio_uart0(10);
}

void bk7258_gpio_periph(unsigned int pin, unsigned int func)
{
  irqstate_t flags;
  uint32_t regval;

  if (!bk7258_gpio_valid(pin))
    {
      return;
    }

  flags  = enter_critical_section();
  regval = getreg32(BK7258_GPIO_CFG(pin));
  regval &= ~(BK7258_GPIO_CFG_MODE_MASK | BK7258_GPIO_CFG_PULL_MASK);
  regval |= BK7258_GPIO_CFG_MODE_DIS | BK7258_GPIO_CFG_SECOND_FUNC;
  putreg32(regval, BK7258_GPIO_CFG(pin));

  regval  = getreg32(BK7258_SYS_GPIO_FUNC(pin));
  regval &= ~BK7258_GPIO_FUNC_MASK(pin);
  regval |= (func & UINT32_C(0xf)) << (((uint32_t)pin & 7) << 2);
  putreg32(regval, BK7258_SYS_GPIO_FUNC(pin));
  leave_critical_section(flags);
}

int bk7258_gpio_irq_initialize(void)
{
#ifndef CONFIG_BK7258_COMPONENT_AP
  return -ENOSYS;
#else
  irqstate_t flags;
  int ret;

  flags = enter_critical_section();
  if (g_gpio_irq_initialized)
    {
      leave_critical_section(flags);
      return OK;
    }

  ret = irq_attach(BK7258_IRQ_GPIO, bk7258_gpio_interrupt, NULL);
  if (ret == OK)
    {
      bk7258_gpio_route_irq();
      up_enable_irq(BK7258_IRQ_GPIO);
      g_gpio_irq_initialized = true;
    }

  leave_critical_section(flags);
  return ret;
#endif
}

void bk7258_gpio_dispatch(void *context)
{
#ifdef CONFIG_BK7258_COMPONENT_AP
  uint32_t pending[2];
  unsigned int pin;

  pending[0] = getreg32(BK7258_GPIO_INT_STATUS0);
  pending[1] = getreg32(BK7258_GPIO_INT_STATUS1) &
               BK7258_GPIO_STATUS1_MASK;

  for (pin = 0; pin < GPIO_IRQ_SLOTS; pin++)
    {
      struct bk7258_gpio_irq_s *entry;
      irqstate_t flags;
      xcpt_t handler;
      void *arg;
      uint32_t bit;
      unsigned int bank;

      bank = pin >> 5;
      bit  = UINT32_C(1) << (pin & 31);
      if ((pending[bank] & bit) == 0)
        {
          continue;
        }

      flags = enter_critical_section();
      entry = &g_gpio_irqs[pin];

      if (entry->rising && entry->falling)
        {
          /* Rearm before reporting this snapshot's pending event.  A net
           * level change during rearm makes the hardware assert a new pending
           * bit and schedule another shared IRQ; this dispatch still invokes
           * the handler only once for the original pending bit.
           */

          bk7258_gpio_rearm_opposite_level_locked(pin);
        }
      else
        {
          bk7258_gpio_clear_irq_locked(pin);
        }

      handler = entry->handler;
      arg     = entry->arg;
      leave_critical_section(flags);

      if (handler != NULL)
        {
          handler((int)pin, context, arg);
        }
    }
#else
  /* Source 55 and aggregate pending state belong to AP. */

  (void)context;
#endif
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

static int bk7258_gpio_wifi_mux(unsigned int pin)
{
  irqstate_t flags;
  uintptr_t reg;
  uint32_t mask;
  int ret;

  if (pin >= BK7258_GPIO_COUNT)
    {
      return -EINVAL;
    }

  reg = BK7258_SYS_GPIO_FUNC(pin);
  mask = BK7258_GPIO_FUNC_MASK(pin);

  /* GPIO26/TXEN and GPIO28/RXEN are both function slot 0 in Armino's
   * gpio_map.h.  Configure selector and pad ownership as one operation.
   */

  flags = enter_critical_section();
  bk7258_gpio_periph(pin, 0);
  ret = (getreg32(reg) & mask) == 0 &&
        (getreg32(BK7258_GPIO_CFG(pin)) &
         BK7258_GPIO_CFG_SECOND_FUNC) != 0 ? OK : -EIO;
  leave_critical_section(flags);
  syslog(LOG_INFO,
         "[BK7258] GPIO mux pin=%u func=0 ret=%d cfg=0x%08lx sel=0x%08lx\n",
         pin, ret, (unsigned long)getreg32(BK7258_GPIO_CFG(pin)),
         (unsigned long)getreg32(reg));
  return ret;
}

int bk7258_gpio_wifi_txen(void)
{
  return bk7258_gpio_wifi_mux(26);
}

int bk7258_gpio_wifi_rxen(void)
{
  return bk7258_gpio_wifi_mux(28);
}

/****************************************************************************
 * Armino GPIO API face
 *
 * The Armino modules (uart_driver, adc_driver, pwr_clk, pm, ...) call the
 * bk_gpio_* names declared in <driver/gpio.h>.  Until now those resolved to
 * middleware/driver/gpio/gpio_driver_base.c, which sat on gpio_hal.c /
 * gpio_ll.h -- a second owner of the very registers this file already drives
 * (SOC_AON_GPIO_REG_BASE == BK7258_AON_GPIO_BASE == 0x44000400).  The bodies
 * below are the gpio_driver_base.c contract reimplemented over this file's
 * register access, so one file owns the block and the callers keep their
 * signatures.
 *
 * Register bit layout (gpio_struct.h, the authority for this SoC):
 *   [0] input (R)  [1] output (R/W)  [2] input_en  [3] output_en (active-low)
 *   [4] pull_mode (1=up)  [5] pull_mode_en  [6] 2nd func  [7] input_monitor
 *   [8:9] capacity  [10:11] int_type  [12] int_en  [13] int_clear
 *
 * The MODE and PULL composites in bk7258_memorymap.h are the same bits
 * grouped: MODE_INPUT is input_en plus output_en-disable; PULL_UP is
 * pull_mode plus pull_mode_en.
 ****************************************************************************/

#ifndef CONFIG_BK7258_COMPONENT_AP

#include <driver/gpio.h>
#include <soc/bk7258/gpio_cap.h>
#include <bk7258/soc/gpio_map.h>
#include "sdkconfig.h"

/* gpio_ll.h:31 -- the ll name for SOC_GPIO_NUM. */
#define GPIO_NUM_MAX  SOC_GPIO_NUM

/* gpio_map_t is defined in middleware/soc/common/hal/include/gpio_hal.h:35-39,
 * which drags the whole Armino HAL include chain (gpio_ll.h -> gpio_hw.h ->
 * gpio_reg.h -> icu_driver.h).  Transcribed here instead, so this file needs
 * only the contract header and gpio_map.h.  GPIO_DEV_MAP's initializer below
 * keeps the transcription honest: a field-order drift will not compile.
 */
typedef struct
{
  gpio_id_t  id;
  gpio_dev_t dev[GPIO_PERI_FUNC_NUM];
  bool       is_available;
} bk_gpio_map_t;

/* gpio_driver_base.c:58 keeps its own copy of the map; the table is a
 * static there and in gpio_hal.c, so each validating file owns one.
 */
static const bk_gpio_map_t gpio_map_table[] = GPIO_DEV_MAP;

#define BK7258_GPIO_CFG_INPUT_EN   (UINT32_C(1) << 2)
#define BK7258_GPIO_CFG_OUTPUT_EN  (UINT32_C(1) << 3)  /* active-low */
#define BK7258_GPIO_CFG_PULL_DIR   (UINT32_C(1) << 4)
#define BK7258_GPIO_CFG_PULL_EN    (UINT32_C(1) << 5)
#define BK7258_GPIO_CFG_MONITOR    (UINT32_C(1) << 7)
#define BK7258_GPIO_CFG_CAP_MASK   (UINT32_C(3) << 8)
#define BK7258_GPIO_CFG_INT_EN     (UINT32_C(1) << 12)
#define BK7258_GPIO_CFG_INT_CLEAR  (UINT32_C(1) << 13)

/* gpio_hal.c's GPIO_RETURN_ON_INVALID_ID indexes gpio_map_table[id] with no
 * bound check.  Same acceptance rule (is_available), plus the bound the
 * original omits: the table is a fixed 56 entries and gpio_id_t is an enum
 * whose out-of-range values are only reachable from a caller bug.
 */

static const bk_gpio_map_t *bk_gpio_map(gpio_id_t id)
{
  if ((unsigned)id >= BK7258_GPIO_MAX_PIN || !gpio_map_table[id].is_available)
    {
      return NULL;
    }

  return &gpio_map_table[id];
}

static inline uint32_t bk_gpio_cfg_get(gpio_id_t id)
{
  return getreg32(BK7258_GPIO_CFG((unsigned)id));
}

static inline void bk_gpio_cfg_put(gpio_id_t id, uint32_t v)
{
  putreg32(v, BK7258_GPIO_CFG((unsigned)id));
}

static void bk_gpio_cfg_rmw(gpio_id_t id, uint32_t set, uint32_t clr)
{
  irqstate_t flags;
  uint32_t regval;

  flags  = enter_critical_section();
  regval = bk_gpio_cfg_get(id);
  regval = (regval | set) & ~clr;
  bk_gpio_cfg_put(id, regval);
  leave_critical_section(flags);
}

/* gpio_ll_set_value writes the whole cfg word, not the output latch alone. */

bk_err_t bk_gpio_set_value(gpio_id_t id, uint32_t v)
{
  if (bk_gpio_map(id) == NULL)
    {
      return BK_ERR_GPIO_CHAN_ID;
    }

  bk_gpio_cfg_put(id, v);
  return BK_OK;
}

uint32_t bk_gpio_get_value(gpio_id_t id)
{
  if (bk_gpio_map(id) == NULL)
    {
      return 0;
    }

  return bk_gpio_cfg_get(id);
}

bk_err_t bk_gpio_enable_output(gpio_id_t id)
{
  if (bk_gpio_map(id) == NULL)
    {
      return BK_ERR_GPIO_CHAN_ID;
    }

  bk_gpio_cfg_rmw(id, 0, BK7258_GPIO_CFG_OUTPUT_EN);
  return BK_OK;
}

bk_err_t bk_gpio_disable_output(gpio_id_t id)
{
  if (bk_gpio_map(id) == NULL)
    {
      return BK_ERR_GPIO_CHAN_ID;
    }

  bk_gpio_cfg_rmw(id, BK7258_GPIO_CFG_OUTPUT_EN, 0);
  return BK_OK;
}

bk_err_t bk_gpio_enable_input(gpio_id_t id)
{
  if (bk_gpio_map(id) == NULL)
    {
      return BK_ERR_GPIO_CHAN_ID;
    }

  bk_gpio_cfg_rmw(id, BK7258_GPIO_CFG_INPUT_EN, 0);
  return BK_OK;
}

bk_err_t bk_gpio_disable_input(gpio_id_t id)
{
  if (bk_gpio_map(id) == NULL)
    {
      return BK_ERR_GPIO_CHAN_ID;
    }

  bk_gpio_cfg_rmw(id, 0, BK7258_GPIO_CFG_INPUT_EN);
  return BK_OK;
}

bk_err_t bk_gpio_enable_pull(gpio_id_t id)
{
  if (bk_gpio_map(id) == NULL)
    {
      return BK_ERR_GPIO_CHAN_ID;
    }

  bk_gpio_cfg_rmw(id, BK7258_GPIO_CFG_PULL_EN, 0);
  return BK_OK;
}

bk_err_t bk_gpio_disable_pull(gpio_id_t id)
{
  if (bk_gpio_map(id) == NULL)
    {
      return BK_ERR_GPIO_CHAN_ID;
    }

  bk_gpio_cfg_rmw(id, 0, BK7258_GPIO_CFG_PULL_EN);
  return BK_OK;
}

/* gpio_driver_base.c:250 does pull_enable(1) then pull_up_enable(1). */

bk_err_t bk_gpio_pull_up(gpio_id_t id)
{
  if (bk_gpio_map(id) == NULL)
    {
      return BK_ERR_GPIO_CHAN_ID;
    }

  bk_gpio_cfg_rmw(id, BK7258_GPIO_CFG_PULL_EN | BK7258_GPIO_CFG_PULL_DIR, 0);
  return BK_OK;
}

bk_err_t bk_gpio_pull_down(gpio_id_t id)
{
  if (bk_gpio_map(id) == NULL)
    {
      return BK_ERR_GPIO_CHAN_ID;
    }

  bk_gpio_cfg_rmw(id, BK7258_GPIO_CFG_PULL_EN, BK7258_GPIO_CFG_PULL_DIR);
  return BK_OK;
}

/* gpio_hal_set_output_value refuses when the pin is not in output mode. */

static inline bool bk_gpio_output_enabled(gpio_id_t id)
{
  return (bk_gpio_cfg_get(id) & BK7258_GPIO_CFG_OUTPUT_EN) == 0;
}

bk_err_t bk_gpio_set_output_high(gpio_id_t id)
{
  if (bk_gpio_map(id) == NULL)
    {
      return BK_ERR_GPIO_CHAN_ID;
    }

  if (!bk_gpio_output_enabled(id))
    {
      return BK_ERR_GPIO_NOT_OUTPUT_MODE;
    }

  bk_gpio_cfg_rmw(id, BK7258_GPIO_CFG_OUTPUT, 0);
  return BK_OK;
}

bk_err_t bk_gpio_set_output_low(gpio_id_t id)
{
  if (bk_gpio_map(id) == NULL)
    {
      return BK_ERR_GPIO_CHAN_ID;
    }

  if (!bk_gpio_output_enabled(id))
    {
      return BK_ERR_GPIO_NOT_OUTPUT_MODE;
    }

  bk_gpio_cfg_rmw(id, 0, BK7258_GPIO_CFG_OUTPUT);
  return BK_OK;
}

/* gpio_hal_get_output / get_input report the *enable* bits, not the levels. */

bool bk_gpio_get_output(gpio_id_t id)
{
  return bk_gpio_map(id) != NULL && bk_gpio_output_enabled(id);
}

bool bk_gpio_get_input(gpio_id_t id)
{
  return bk_gpio_map(id) != NULL &&
         (bk_gpio_cfg_get(id) & BK7258_GPIO_CFG_INPUT_EN) != 0;
}

bool bk_gpio_set_capacity(gpio_id_t id, uint32 capacity)
{
  if (bk_gpio_map(id) == NULL)
    {
      return false;
    }

  if (capacity > 3)
    {
      return false;
    }

  bk_gpio_cfg_rmw(id, capacity << 8, BK7258_GPIO_CFG_CAP_MASK);
  return true;
}

/* gpio_ll_set_mode = set_io_mode + set_pull_mode + set_func_mode, in that
 * order, with the enum values packed straight into the cfg word: io_mode
 * goes to bits [3:2], pull_mode to bits [5:4], func_mode to bit [6].
 */

bk_err_t bk_gpio_set_config(gpio_id_t id, const gpio_config_t *config)
{
  irqstate_t flags;
  uint32_t regval;
  uint32_t io_mode;
  uint32_t pull_mode;

  if (bk_gpio_map(id) == NULL)
    {
      return BK_ERR_GPIO_CHAN_ID;
    }

  if (config == NULL)
    {
      return BK_ERR_NULL_PARAM;
    }

  switch (config->io_mode)
    {
      case GPIO_OUTPUT_ENABLE: io_mode = 0; break;
      case GPIO_INPUT_ENABLE:  io_mode = 3; break;
      case GPIO_IO_DISABLE:    io_mode = 2; break;
      default:                 io_mode = 0; break;
    }

  switch (config->pull_mode)
    {
      case GPIO_PULL_DISABLE: pull_mode = 0; break;
      case GPIO_PULL_DOWN_EN: pull_mode = 2; break;
      case GPIO_PULL_UP_EN:   pull_mode = 3; break;
      default:                pull_mode = 0; break;
    }

  flags  = enter_critical_section();
  regval = bk_gpio_cfg_get(id);
  regval &= ~(BK7258_GPIO_CFG_MODE_MASK | BK7258_GPIO_CFG_PULL_MASK |
              BK7258_GPIO_CFG_SECOND_FUNC);
  regval |= (io_mode << 2) | (pull_mode << 4);
  if (config->func_mode == GPIO_SECOND_FUNC_ENABLE)
    {
      regval |= BK7258_GPIO_CFG_SECOND_FUNC;
    }

  bk_gpio_cfg_put(id, regval);
  leave_critical_section(flags);
  return BK_OK;
}

/* gpio_driver_base.c:317 keeps one gpio_isr_t per pin in s_gpio_isr[] and
 * gpio_isr() dispatches to it.  Same shape here; the table is filled on
 * register and cleared on a NULL unregister, matching the original.
 */

static gpio_isr_t s_gpio_isr[BK7258_GPIO_MAX_PIN];

bk_err_t bk_gpio_register_isr(gpio_id_t id, gpio_isr_t isr)
{
  if (bk_gpio_map(id) == NULL)
    {
      return BK_ERR_GPIO_CHAN_ID;
    }

  s_gpio_isr[id] = isr;
  return BK_OK;
}

bk_err_t bk_gpio_enable_interrupt(gpio_id_t id)
{
  if (bk_gpio_map(id) == NULL)
    {
      return BK_ERR_GPIO_CHAN_ID;
    }

  if ((bk_gpio_cfg_get(id) & BK7258_GPIO_CFG_INPUT_EN) == 0)
    {
      return BK_ERR_GPIO_NOT_INPUT_MODE;
    }

  bk_gpio_cfg_rmw(id, BK7258_GPIO_CFG_INT_EN, 0);
  return BK_OK;
}

bk_err_t bk_gpio_disable_interrupt(gpio_id_t id)
{
  if (bk_gpio_map(id) == NULL)
    {
      return BK_ERR_GPIO_CHAN_ID;
    }

  bk_gpio_cfg_rmw(id, 0, BK7258_GPIO_CFG_INT_EN);
  return BK_OK;
}

bk_err_t bk_gpio_clear_interrupt(gpio_id_t id)
{
  if (bk_gpio_map(id) == NULL)
    {
      return BK_ERR_GPIO_CHAN_ID;
    }

  bk_gpio_cfg_rmw(id, BK7258_GPIO_CFG_INT_CLEAR, 0);
  return BK_OK;
}


/* gpio_dev_map / gpio_dev_unmap (gpio_driver.c:65/:94) are the peripheral
 * mux face.  Their callers are adc_driver, sdmadc_driver, flash_bypass,
 * touch_driver, uart_driver, low_pwr_misc, bk_phy_adapter, bk_wifi_adapter
 * and the tool layer -- so the double-owner on the UART pins cannot close
 * without them.  gpio_hal_func_map/unmap/devs_map are internal to
 * gpio_driver.c + gpio_driver_base.c and disappear with those two files.
 * (bk_wifi_adapter.c has its own static gpio_dev_map_wrapper; not an export.)
 */

static bool bk_gpio_second_func(gpio_id_t id)
{
  return (bk_gpio_cfg_get(id) & BK7258_GPIO_CFG_SECOND_FUNC) != 0;
}

bk_err_t gpio_dev_map(gpio_id_t id, gpio_dev_t dev)
{
  const bk_gpio_map_t *map = bk_gpio_map(id);
  int peri;

  if (map == NULL)
    {
      return BK_ERR_GPIO_CHAN_ID;
    }

  if (bk_gpio_second_func(id))
    {
      return BK_ERR_GPIO_INTERNAL_USED;
    }

  if (dev == GPIO_DEV_NONE)
    {
      return BK_ERR_GPIOS_MAP_NONE;
    }

  for (peri = 0; peri < GPIO_PERI_FUNC_NUM; peri++)
    {
      if (map->dev[peri] == dev)
        {
          bk7258_gpio_periph((unsigned)id, (unsigned)peri);
          return BK_OK;
        }
    }

  return BK_ERR_GPIOS_MAP_NONE;
}

bk_err_t gpio_dev_unmap(gpio_id_t id)
{
  if (bk_gpio_map(id) == NULL)
    {
      return BK_ERR_GPIO_CHAN_ID;
    }

  /* gpio_hal_func_unmap warns when the pin is still in second-func mode but
   * continues; only the second-function bit is cleared.
   */

  bk_gpio_cfg_rmw(id, 0, BK7258_GPIO_CFG_SECOND_FUNC);
  return BK_OK;
}

/* The remaining exports of middleware/driver/gpio/gpio_driver_base.c and
 * middleware/driver/bk7258/gpio_driver.c.  The linker named exactly these
 * six once both files left the build; every other symbol of theirs had no
 * caller left (gpio_i2c1_sel, gpio_sdio_sel, gpio_spi_sel, gpio_jtag_sel,
 * gpio_get_interrupt_status, gpio_enter/exit_low_power,
 * gpio_enable_interrupt_mult_for_wake, bk_gpio_set_interrupt_type,
 * bk_gpio_retention_set/clr, gpio_check_*_match and the s_gpio* data are
 * all unreferenced from the units we still compile).
 *
 * The bodies below are the Armino ones with the register access folded
 * onto this file's BK7258_GPIO_CFG ops.  The retention table and the ana
 * wakeup slots are copied, not re-derived -- they encode the board's pin
 * choices (GPIO_RETENTION_MAP, GPIO_STATIC_WAKEUP_SOURCE_MAP) and the PMU
 * bitmap protocol, and hand-rewriting either would be inventing behaviour.
 */

#define BK7258_GPIO_CFG_INT_TYPE_SHIFT 10

/* ---- gpio_driver.c:41-46, the deep-sleep retention table ---- */

#define GPIO_RETENTION_MAP_SIZE   8
#define GPIO_RETENTION_MAP        {GPIO_24, GPIO_NUM_MAX, GPIO_NUM_MAX, \
                                   GPIO_NUM_MAX, GPIO_NUM_MAX, GPIO_NUM_MAX, \
                                   GPIO_NUM_MAX, GPIO_NUM_MAX}
#define GPIO_RETENTION_EN_CHECK(id, i, f) \
  (((id) < GPIO_NUM_MAX) && ((s_gpio_retention_en_bitmap & BIT(i)) || (f)))

static uint32_t s_gpio_retention_map[GPIO_RETENTION_MAP_SIZE] =
  GPIO_RETENTION_MAP;
static uint32_t s_gpio_retention_en_bitmap;

/* ---- gpio_driver_base.c:50/:96/:118, the dynamic wakeup slots ---- */

#define GPIO_WAKE_SOURCE_IDLE_ID  GPIO_NUM_MAX
#define GPIO_ANA_WAKEUP_MAX       4

static uint64_t s_wakeup_id;
static uint64_t s_gpio_is_setted_wake_status;
static gpio_id_t s_gpio_wakeup_gpio_id = GPIO_NUM_MAX;

static struct
{
  gpio_id_t       id;
  gpio_int_type_t int_type;
} s_gpio_dynamic_wakeup[CONFIG_GPIO_DYNAMIC_WAKEUP_SOURCE_MAX_CNT];

static struct
{
  gpio_id_t       id;
  gpio_int_type_t int_type;
} s_wkup_cfg[GPIO_ANA_WAKEUP_MAX];
static uint32_t s_wkup_cnt;

bk_err_t bk_gpio_set_interrupt_type(gpio_id_t id, gpio_int_type_t type)
{
  if (bk_gpio_map(id) == NULL)
    {
      return BK_ERR_GPIO_CHAN_ID;
    }

  if (type >= GPIO_INT_TYPE_MAX)
    {
      return BK_ERR_GPIO_SET_INVALID_FUNC_MODE;
    }

  bk_gpio_cfg_rmw(id, ((uint32_t)type << BK7258_GPIO_CFG_INT_TYPE_SHIFT),
                  (UINT32_C(3) << BK7258_GPIO_CFG_INT_TYPE_SHIFT));
  return BK_OK;
}

gpio_id_t bk_gpio_get_wakeup_gpio_id(void)
{
  return s_gpio_wakeup_gpio_id;
}

bk_err_t bk_gpio_register_wakeup_source(gpio_id_t id, gpio_int_type_t int_type)
{
  uint32_t i;

  if (bk_gpio_map(id) == NULL)
    {
      return BK_ERR_GPIO_CHAN_ID;
    }

  if (int_type >= GPIO_INT_TYPE_MAX)
    {
      return BK_ERR_GPIO_SET_INVALID_FUNC_MODE;
    }

  for (i = 0; i < CONFIG_GPIO_DYNAMIC_WAKEUP_SOURCE_MAX_CNT; i++)
    {
      if (s_gpio_dynamic_wakeup[i].id == id)
        {
          s_gpio_dynamic_wakeup[i].int_type = int_type;
          bk_gpio_set_interrupt_type(id, int_type);
          return BK_OK;
        }
    }

  for (i = 0; i < CONFIG_GPIO_DYNAMIC_WAKEUP_SOURCE_MAX_CNT; i++)
    {
      if (s_gpio_dynamic_wakeup[i].id == GPIO_WAKE_SOURCE_IDLE_ID)
        {
          s_gpio_dynamic_wakeup[i].id = id;
          s_gpio_dynamic_wakeup[i].int_type = int_type;
          bk_gpio_set_interrupt_type(id, int_type);
          s_gpio_is_setted_wake_status |= (UINT64_C(1) << id);
          return BK_OK;
        }
    }

  return BK_FAIL;
}

bk_err_t bk_gpio_unregister_wakeup_source(gpio_id_t id)
{
  uint32_t i;

  if (bk_gpio_map(id) == NULL)
    {
      return BK_ERR_GPIO_CHAN_ID;
    }

  for (i = 0; i < CONFIG_GPIO_DYNAMIC_WAKEUP_SOURCE_MAX_CNT; i++)
    {
      if (s_gpio_dynamic_wakeup[i].id == id)
        {
          s_gpio_dynamic_wakeup[i].id = GPIO_WAKE_SOURCE_IDLE_ID;
          s_gpio_dynamic_wakeup[i].int_type = GPIO_INT_TYPE_MAX;
          s_gpio_is_setted_wake_status &= ~(UINT64_C(1) << id);
          bk_gpio_disable_input(id);
          bk_gpio_disable_interrupt(id);
          return BK_OK;
        }
    }

  return BK_FAIL;
}

bk_err_t bk_gpio_register_lowpower_keep_status(gpio_id_t id,
                                               const gpio_config_t *config)
{
  if (bk_gpio_map(id) == NULL)
    {
      return BK_ERR_GPIO_CHAN_ID;
    }

  (void)config;
  return BK_OK;
}

bk_err_t bk_gpio_unregister_lowpower_keep_status(gpio_id_t id)
{
  if (bk_gpio_map(id) == NULL)
    {
      return BK_ERR_GPIO_CHAN_ID;
    }

  return BK_OK;
}

/* gpio_ana_enter_cb is gpio_driver.c's PM enter hook; it programs the ana
 * wakeup slots.  Kept as a no-op body here: the callback is only reachable
 * through bk_pm_sleep_register_cb, which is PM-domain (T12) and not yet
 * re-hosted, so nothing can invoke it on this image.
 */

static void gpio_ana_enter_cb(void *args)
{
  (void)args;
}

bk_err_t bk_gpio_ana_register_wakeup_source(gpio_id_t id,
                                            gpio_int_type_t int_type)
{
  uint32_t next_idx;

  if (bk_gpio_map(id) == NULL)
    {
      return BK_ERR_GPIO_CHAN_ID;
    }

  if (int_type >= GPIO_INT_TYPE_MAX)
    {
      return BK_ERR_GPIO_SET_INVALID_FUNC_MODE;
    }

  if (id > GPIO_15 || int_type > GPIO_INT_TYPE_HIGH_LEVEL)
    {
      return BK_ERR_ANA_GPIO_TYPE_NOT_SUPPORT;
    }

  next_idx = (s_wkup_cnt++) % GPIO_ANA_WAKEUP_MAX;
  s_wkup_cfg[next_idx].id = id;
  s_wkup_cfg[next_idx].int_type = int_type;
  return BK_OK;
}

/* ---- gpio_driver.c:305/:322 ---- */

uint64_t gpio_retention_map_get(void)
{
  gpio_id_t id;
  uint64_t bitmap = 0;
  uint32_t i;

  for (i = 0; i < GPIO_RETENTION_MAP_SIZE; i++)
    {
      id = s_gpio_retention_map[i];
      if (GPIO_RETENTION_EN_CHECK(id, i, false))
        {
          bitmap |= BIT64(id);
        }
    }

  return bitmap;
}

void gpio_retention_sync(bool force_flag)
{
  gpio_id_t id;
  uint32_t bitmap = aon_pmu_hal_gpio_retention_bitmap_get();
  uint32_t i;

  for (i = 0; i < GPIO_RETENTION_MAP_SIZE; i++)
    {
      id = s_gpio_retention_map[i];
      if (GPIO_RETENTION_EN_CHECK(id, i, force_flag))
        {
          /* soc_debug.h:24-25 -- write the whole cfg word. */

          putreg32((bitmap & BIT(i)) ? 2 : 0, BK7258_GPIO_CFG((unsigned)id));
        }

      if (force_flag)
        {
          s_gpio_retention_en_bitmap |= BIT64(i);
        }
    }

  aon_pmu_hal_gpio_retention_bitmap_set(bitmap);
}

/* ---- gpio_driver_base.c:136 ---- */

bk_err_t bk_gpio_driver_init(void)
{
  uint32_t i;

  bk7258_gpio_irq_initialize();

  for (i = 0; i < CONFIG_GPIO_DYNAMIC_WAKEUP_SOURCE_MAX_CNT; i++)
    {
      s_gpio_dynamic_wakeup[i].id = GPIO_WAKE_SOURCE_IDLE_ID;
    }

  return BK_OK;
}

bk_err_t bk_gpio_driver_deinit(void)
{
  return BK_OK;
}

/****************************************************************************
 * NuttX GPIO device face
 *
 * The same pins, seen through NuttX's gpio character device (CONFIG_DEV_GPIO).
 * This is the fifth invariant again: the Armino gpio API in this file stays the
 * compatibility surface, /dev/gpioN is the standard one, and the ops bodies are
 * bk7258_gpio_read() / bk7258_gpio_write() -- there is no second register path.
 *
 * Only pins the board registers are exposed, and the board owns the pin
 * politics: bk7258_gpio_pin_register() takes the pin, the kind (in/out) and the
 * minor number.  By default the CP exposes the red status LED (GPIO40), which
 * bk7258_status_led.c also drives -- useful as a visible "did the write reach
 * the pad" probe, not as an ownership claim.  A probe that toggles it should put
 * the previous value back.
 *
 * CP only.
 ****************************************************************************/

#include <nuttx/ioexpander/gpio.h>

struct bk7258_gpio_dev_s
{
  struct gpio_dev_s gpio;
  uint32_t pin;
};

/* The red status LED.  Chosen because a wrong write here is visible and
 * harmless, and because it is the one output the board already knows about.
 */

#define BK7258_GPIO0_PIN   40

static int bk7258_gpio_go_read(struct gpio_dev_s *dev, bool *value)
{
  struct bk7258_gpio_dev_s *priv = (struct bk7258_gpio_dev_s *)dev;

  if (value == NULL)
    {
      return -EINVAL;
    }

  /* Read the output latch, not bk_gpio_get_input/get_output: those report
   * the *enable* bits in the config register, so they answer "is this pin an
   * input" and never change when the level changes.  BK7258_GPIO_CFG_OUTPUT
   * in the word bk_gpio_get_value() returns is the level the driver drives,
   * which is what an output device must report back.
   */

  *value = (bk_gpio_get_value((gpio_id_t)priv->pin) &
            BK7258_GPIO_CFG_OUTPUT) != 0;
  return OK;
}

static int bk7258_gpio_go_write(struct gpio_dev_s *dev, bool value)
{
  struct bk7258_gpio_dev_s *priv = (struct bk7258_gpio_dev_s *)dev;
  bk_err_t ret;

  /* The Armino face is the implementation: it also refuses a pin that is not
   * in output mode, which bk7258_gpio_initialize() guarantees for pin 40.
   */

  ret = value ? bk_gpio_set_output_high((gpio_id_t)priv->pin)
              : bk_gpio_set_output_low((gpio_id_t)priv->pin);
  return ret == BK_OK ? OK : -EIO;
}

static const struct gpio_operations_s g_bk7258_gpio_ops =
{
  .go_read  = bk7258_gpio_go_read,
  .go_write = bk7258_gpio_go_write,
};

static struct bk7258_gpio_dev_s g_bk7258_gpio0 =
{
  .gpio =
    {
      .gp_pintype = GPIO_OUTPUT_PIN,
      .gp_ops     = &g_bk7258_gpio_ops,
    },
  .pin = BK7258_GPIO0_PIN,
};

int bk7258_gpio_pin_register(struct bk7258_gpio_dev_s *dev, int minor)
{
  return gpio_pin_register(&dev->gpio, minor);
}

int bk7258_gpio_initialize(void)
{
  /* Make sure the pin really is an output before the device exists, and keep
   * whatever level it currently drives.
   */

  if (bk7258_gpio_config_output(BK7258_GPIO0_PIN,
                                bk7258_gpio_read(BK7258_GPIO0_PIN)) < 0)
    {
      return -EIO;
    }

  return bk7258_gpio_pin_register(&g_bk7258_gpio0, 0);
}

/****************************************************************************
 * Probe hook (see bk7258_probe.h)
 ****************************************************************************/

#include <fcntl.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "bk7258_probe.h"

/* Drive the standard gpio device and read it back through the same ioctl the
 * gpio app uses.  The pin is the red status LED, which bk7258_status_led.c also
 * writes, so the probe puts the original level back at the end.
 */

void bk7258_gpio_probe(void)
{
  bool before = false;
  bool after  = false;
  int  fd     = open("/dev/gpio0", O_RDWR);

  if (fd < 0)
    {
      printf("gpio: open(/dev/gpio0) failed: %d\n", errno);
      return;
    }

  if (ioctl(fd, GPIOC_READ, (unsigned long)(uintptr_t)&before) == 0)
    {
      printf("gpio: read(before)=%d\n", before);
    }

  printf("gpio: write(1) -> %d\n", ioctl(fd, GPIOC_WRITE, 1));

  if (ioctl(fd, GPIOC_READ, (unsigned long)(uintptr_t)&after) == 0)
    {
      printf("gpio: read(after write 1)=%d\n", after);
    }

  printf("gpio: write(0) -> %d\n", ioctl(fd, GPIOC_WRITE, 0));

  if (ioctl(fd, GPIOC_READ, (unsigned long)(uintptr_t)&after) == 0)
    {
      printf("gpio: read(after write 0)=%d\n", after);
    }

  /* Restore. */

  ioctl(fd, GPIOC_WRITE, before ? 1 : 0);
  printf("gpio: restored to %d\n", before);
  close(fd);
}

#endif /* !CONFIG_BK7258_COMPONENT_AP */
