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

/* On the CP the board helpers below reach the pads through the authority's
 * gpio_ll.h, the same layer the Armino face further down uses, so the CP
 * image has one register path for the GPIO block.  The AP builds no Armino
 * code and has no gpio_ll; its helpers keep the direct register writes,
 * which are the only GPIO path in that image.
 */

#ifndef CONFIG_BK7258_COMPONENT_AP
#  include "sdkconfig.h"
#  include "gpio_hal.h"
#  include "gpio_driver_base.h"
#endif

/* Pad config (AON GPIO block) and peripheral mux (SYS GPIO_FUNC) access for
 * the board helpers.  On the CP these are gpio_ll's whole-word get/set and
 * its mux field accessor, so each helper keeps exactly the read-modify-write
 * sequence it had.  gpio_ll_init() is called before each mux access because
 * the mux pointer it sets is a static in every file that includes
 * gpio_ll.h, and nothing else initialises this file's copy.
 */

#ifndef CONFIG_BK7258_COMPONENT_AP
#  define BK7258_GPIO_HW ((gpio_hw_t *)GPIO_LL_REG_BASE)

_Static_assert(SOC_AON_GPIO_REG_BASE == BK7258_AON_GPIO_BASE,
               "gpio_ll and BK7258_GPIO_CFG() must address the same block");
_Static_assert(sizeof(BK7258_GPIO_HW->gpio_num[0]) == 4,
               "gpio_ll pad index must match BK7258_GPIO_CFG()");

static inline uint32_t bk7258_gpio_cfg_read(unsigned int pin)
{
  return gpio_ll_get_value(BK7258_GPIO_HW, pin);
}

static inline void bk7258_gpio_cfg_write(unsigned int pin, uint32_t v)
{
  gpio_ll_set_value(BK7258_GPIO_HW, pin, v);
}

static inline uint32_t bk7258_gpio_func_read(unsigned int pin)
{
  gpio_ll_init(BK7258_GPIO_HW);
  return gpio_ll_get_perial_mode(BK7258_GPIO_HW, pin);
}

static inline void bk7258_gpio_func_write(unsigned int pin, uint32_t func)
{
  gpio_ll_init(BK7258_GPIO_HW);
  gpio_ll_set_perial_mode(BK7258_GPIO_HW, pin, func);
}
#else
/* The AP is built -O0 (CONFIG_DEBUG_NOOPT), where static inline is not
 * inlined.  As functions these two added a call to every pad access, and
 * the GPIO interrupt path (dispatch -> clear/rearm/enable) is mostly pad
 * accesses: with them, KEY1+KEY2 together lost 3 of 4 edges on the board
 * against 0 of 14 before.  As macros they compile to the same
 * instructions as the direct getreg32/putreg32 they replaced.
 */

#  define bk7258_gpio_cfg_read(pin)     getreg32(BK7258_GPIO_CFG(pin))
#  define bk7258_gpio_cfg_write(pin, v) putreg32(v, BK7258_GPIO_CFG(pin))

static inline uint32_t bk7258_gpio_func_read(unsigned int pin)
{
  return (getreg32(BK7258_SYS_GPIO_FUNC(pin)) & BK7258_GPIO_FUNC_MASK(pin)) >>
         (((uint32_t)pin & 7) << 2);
}

static inline void bk7258_gpio_func_write(unsigned int pin, uint32_t func)
{
  uint32_t regval;

  regval  = getreg32(BK7258_SYS_GPIO_FUNC(pin));
  regval &= ~BK7258_GPIO_FUNC_MASK(pin);
  regval |= (func & UINT32_C(0xf)) << (((uint32_t)pin & 7) << 2);
  putreg32(regval, BK7258_SYS_GPIO_FUNC(pin));
}
#endif

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
  bk7258_gpio_func_write(pin, 0);
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

  regval  = bk7258_gpio_cfg_read(pin);
  regval &= ~BK7258_GPIO_INT_TYPE_MASK;
  regval |= high ? BK7258_GPIO_INT_LOW_LEVEL :
                   BK7258_GPIO_INT_HIGH_LEVEL;
  bk7258_gpio_cfg_write(pin, regval);
}

static void bk7258_gpio_set_irq_type_locked(unsigned int pin, bool rising)
{
  uint32_t regval;
  uint32_t type;

  regval = bk7258_gpio_cfg_read(pin);
  type    = rising ? BK7258_GPIO_INT_RISING_EDGE :
                     BK7258_GPIO_INT_FALLING_EDGE;
  regval &= ~BK7258_GPIO_INT_TYPE_MASK;
  regval |= type;
  bk7258_gpio_cfg_write(pin, regval);
}

static void bk7258_gpio_clear_irq_locked(unsigned int pin)
{
  uint32_t regval;
  uint32_t bit;

  regval = bk7258_gpio_cfg_read(pin);
  regval |= BK7258_GPIO_INT_CLEAR;
  bk7258_gpio_cfg_write(pin, regval);

  bit = UINT32_C(1) << (pin & 31);
  putreg32(bit, pin < 32 ? BK7258_GPIO_INT_STATUS0 :
                            BK7258_GPIO_INT_STATUS1);
}

static void bk7258_gpio_enable_irq_locked(unsigned int pin)
{
  uint32_t regval;

  regval  = bk7258_gpio_cfg_read(pin);
  regval &= ~BK7258_GPIO_INT_CLEAR;
  regval |= BK7258_GPIO_INT_ENABLE;
  bk7258_gpio_cfg_write(pin, regval);
}

static void bk7258_gpio_rearm_opposite_level_locked(unsigned int pin)
{
  bool high;
  uint32_t regval;

  regval  = bk7258_gpio_cfg_read(pin);
  high    = (regval & BK7258_GPIO_CFG_INPUT) != 0;
  regval &= ~BK7258_GPIO_INT_ENABLE;
  bk7258_gpio_cfg_write(pin, regval);

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

  regval  = bk7258_gpio_cfg_read(pin);
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

  bk7258_gpio_cfg_write(pin, regval);
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

  regval = bk7258_gpio_cfg_read(pin);
  if (initial)
    {
      regval |= BK7258_GPIO_CFG_OUTPUT;
    }
  else
    {
      regval &= ~BK7258_GPIO_CFG_OUTPUT;
    }

  bk7258_gpio_cfg_write(pin, regval);

  regval &= ~(BK7258_GPIO_CFG_MODE_MASK |
              BK7258_GPIO_CFG_PULL_MASK |
              BK7258_GPIO_CFG_SECOND_FUNC);
  regval |= BK7258_GPIO_CFG_MODE_OUTPUT;
  bk7258_gpio_cfg_write(pin, regval);
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
  regval = bk7258_gpio_cfg_read(pin);
  if (high)
    {
      regval |= BK7258_GPIO_CFG_OUTPUT;
    }
  else
    {
      regval &= ~BK7258_GPIO_CFG_OUTPUT;
    }

  bk7258_gpio_cfg_write(pin, regval);
  leave_critical_section(flags);
}

bool bk7258_gpio_read(unsigned int pin)
{
  if (!bk7258_gpio_valid(pin))
    {
      return false;
    }

  return (bk7258_gpio_cfg_read(pin) & BK7258_GPIO_CFG_INPUT) != 0;
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

  regval  = bk7258_gpio_cfg_read(pin);
  regval &= ~BK7258_GPIO_INT_ENABLE;
  bk7258_gpio_cfg_write(pin, regval);

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
  regval  = bk7258_gpio_cfg_read(pin);
  regval &= ~BK7258_GPIO_INT_ENABLE;
  bk7258_gpio_cfg_write(pin, regval);
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
  regval = bk7258_gpio_cfg_read(pin);
  regval &= ~(BK7258_GPIO_CFG_MODE_MASK | BK7258_GPIO_CFG_PULL_MASK);
  regval |= BK7258_GPIO_CFG_MODE_DIS | BK7258_GPIO_CFG_PULL_UP |
            BK7258_GPIO_CFG_SECOND_FUNC;
  bk7258_gpio_cfg_write(pin, regval);

  bk7258_gpio_func_write(pin, 0);
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
  regval = bk7258_gpio_cfg_read(pin);
  regval &= ~(BK7258_GPIO_CFG_MODE_MASK | BK7258_GPIO_CFG_PULL_MASK);
  regval |= BK7258_GPIO_CFG_MODE_DIS | BK7258_GPIO_CFG_SECOND_FUNC;
  bk7258_gpio_cfg_write(pin, regval);

  bk7258_gpio_func_write(pin, func);
  leave_critical_section(flags);
}

void bk7258_gpio_pull_up(unsigned int pin)
{
  irqstate_t flags;
  uint32_t regval;

  if (!bk7258_gpio_valid(pin))
    {
      return;
    }

  flags  = enter_critical_section();
  regval = bk7258_gpio_cfg_read(pin);
  regval &= ~BK7258_GPIO_CFG_PULL_MASK;
  regval |= BK7258_GPIO_CFG_PULL_UP;
  bk7258_gpio_cfg_write(pin, regval);
  leave_critical_section(flags);
}

void bk7258_gpio_drive(unsigned int pin, unsigned int capacity)
{
  irqstate_t flags;
  uint32_t regval;

  if (!bk7258_gpio_valid(pin))
    {
      return;
    }

  if (capacity > 3)
    {
      capacity = 3;
    }

  flags  = enter_critical_section();
  regval = bk7258_gpio_cfg_read(pin);
  regval &= ~BK7258_GPIO_CFG_CAPACITY_MASK;
  regval |= (capacity << 8) & BK7258_GPIO_CFG_CAPACITY_MASK;
  bk7258_gpio_cfg_write(pin, regval);
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
  int ret;

  if (pin >= BK7258_GPIO_COUNT)
    {
      return -EINVAL;
    }

  /* GPIO26/TXEN and GPIO28/RXEN are both function slot 0 in Armino's
   * gpio_map.h.  Configure selector and pad ownership as one operation.
   */

  flags = enter_critical_section();
  bk7258_gpio_periph(pin, 0);
  ret = bk7258_gpio_func_read(pin) == 0 &&
        (bk7258_gpio_cfg_read(pin) & BK7258_GPIO_CFG_SECOND_FUNC) != 0 ?
        OK : -EIO;
  leave_critical_section(flags);
  syslog(LOG_INFO,
         "[BK7258] GPIO mux pin=%u func=0 ret=%d cfg=0x%08lx sel=%lu\n",
         pin, ret, (unsigned long)bk7258_gpio_cfg_read(pin),
         (unsigned long)bk7258_gpio_func_read(pin));
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
 * bk_gpio_* names declared in <driver/gpio.h>; they used to resolve to
 * middleware/driver/gpio/gpio_driver_base.c and bk7258/gpio_driver.c.  The
 * bodies below are those two files' bodies, on the same gpio_hal.c /
 * gpio_ll.h layer they used (SOC_AON_GPIO_REG_BASE == BK7258_AON_GPIO_BASE ==
 * 0x44000400), so the CP image has one register path for the block and the
 * callers keep their signatures.
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
#include "gpio_hal.h"
#include "gpio_driver_base.h"
#include "gpio_driver.h"
#include "aon_pmu_hal.h"
#include "usr_gpio_cfg.h"

/* gpio_driver_base.c:44 -- the driver state, exported under the baseline's
 * name.  The Armino face below reaches the pins only through s_gpio.hal,
 * i.e. gpio_hal.c / gpio_ll.h; it has no register access of its own.
 *
 * Peripheral muxing goes through gpio_hal_func_map(), not
 * gpio_ll_set_perial_mode(): the latter uses a static pointer that each
 * translation unit sets with its own gpio_ll_init(), and gpio_hal_init()
 * only sets the one in bk7258_gpio_hal.c.
 */

gpio_driver_t s_gpio =
{
  .hal.hw = (gpio_hw_t *)GPIO_LL_REG_BASE,
};

/* gpio_driver_base.c:58 keeps its own copy of the map; the table is a
 * static there and in gpio_hal.c, so each validating file owns one.
 */

static const gpio_map_t gpio_map_table[] = GPIO_DEV_MAP;

/* gpio_driver_base.c:60-86, the authority's argument checks. */

#define GPIO_RETURN_ON_INVALID_ID(id) \
  do \
    { \
      if ((id) >= SOC_GPIO_NUM) \
        { \
          return BK_ERR_GPIO_CHAN_ID; \
        } \
      if (!gpio_map_table[id].is_available) \
        { \
          GPIO_LOGW("id %d is not available\r\n", id); \
          return BK_ERR_GPIO_CHAN_ID; \
        } \
    } \
  while (0)

#define GPIO_RETURN_ON_INVALID_INT_TYPE_MODE(mode) \
  do \
    { \
      if ((mode) >= GPIO_INT_TYPE_MAX) \
        { \
          return BK_ERR_GPIO_INVALID_INT_TYPE; \
        } \
    } \
  while (0)

#define GPIO_RETURN_ON_INVALID_IO_MODE(mode) \
  do \
    { \
      if ((mode) >= GPIO_IO_INVALID) \
        { \
          return BK_ERR_GPIO_INVALID_MODE; \
        } \
    } \
  while (0)

#define GPIO_RETURN_ON_INVALID_PULL_MODE(mode) \
  do \
    { \
      if ((mode) >= GPIO_PULL_INVALID) \
        { \
          return BK_ERR_GPIO_INVALID_MODE; \
        } \
    } \
  while (0)

/* The deferred T12 wakeup functions further down still test validity with
 * a pointer; same rule as GPIO_RETURN_ON_INVALID_ID.
 */

static const gpio_map_t *bk_gpio_map(gpio_id_t id)
{
  if ((unsigned)id >= SOC_GPIO_NUM || !gpio_map_table[id].is_available)
    {
      return NULL;
    }

  return &gpio_map_table[id];
}

/* gpio_driver_base.c:194-354 and gpio_driver.c:65-120: the Armino face, one
 * gpio_hal_* call per function, with the authority's checks and return
 * values -- including the odd ones (bk_gpio_get_value returns the error
 * code as a uint32_t, bk_gpio_set_capacity returns BK_OK as false).
 */

bk_err_t bk_gpio_set_value(gpio_id_t gpio_id, uint32_t v)
{
  GPIO_RETURN_ON_INVALID_ID(gpio_id);

  return gpio_hal_set_value(&s_gpio.hal, gpio_id, v);
}

uint32_t bk_gpio_get_value(gpio_id_t gpio_id)
{
  GPIO_RETURN_ON_INVALID_ID(gpio_id);

  return gpio_hal_get_value(&s_gpio.hal, gpio_id);
}

bk_err_t bk_gpio_enable_output(gpio_id_t gpio_id)
{
  GPIO_RETURN_ON_INVALID_ID(gpio_id);

  return gpio_hal_output_enable(&s_gpio.hal, gpio_id, 1);
}

bk_err_t bk_gpio_disable_output(gpio_id_t gpio_id)
{
  GPIO_RETURN_ON_INVALID_ID(gpio_id);

  return gpio_hal_output_enable(&s_gpio.hal, gpio_id, 0);
}

bk_err_t bk_gpio_enable_input(gpio_id_t gpio_id)
{
  GPIO_RETURN_ON_INVALID_ID(gpio_id);

  return gpio_hal_input_enable(&s_gpio.hal, gpio_id, 1);
}

bk_err_t bk_gpio_disable_input(gpio_id_t gpio_id)
{
  GPIO_RETURN_ON_INVALID_ID(gpio_id);

  return gpio_hal_input_enable(&s_gpio.hal, gpio_id, 0);
}

bk_err_t bk_gpio_enable_pull(gpio_id_t gpio_id)
{
  GPIO_RETURN_ON_INVALID_ID(gpio_id);

  return gpio_hal_pull_enable(&s_gpio.hal, gpio_id, 1);
}

bk_err_t bk_gpio_disable_pull(gpio_id_t gpio_id)
{
  GPIO_RETURN_ON_INVALID_ID(gpio_id);

  return gpio_hal_pull_enable(&s_gpio.hal, gpio_id, 0);
}

bk_err_t bk_gpio_pull_up(gpio_id_t gpio_id)
{
  GPIO_RETURN_ON_INVALID_ID(gpio_id);

  gpio_hal_pull_enable(&s_gpio.hal, gpio_id, 1);
  return gpio_hal_pull_up_enable(&s_gpio.hal, gpio_id, 1);
}

bk_err_t bk_gpio_pull_down(gpio_id_t gpio_id)
{
  GPIO_RETURN_ON_INVALID_ID(gpio_id);

  gpio_hal_pull_enable(&s_gpio.hal, gpio_id, 1);
  return gpio_hal_pull_up_enable(&s_gpio.hal, gpio_id, 0);
}

bk_err_t bk_gpio_set_output_high(gpio_id_t gpio_id)
{
  GPIO_RETURN_ON_INVALID_ID(gpio_id);

  return gpio_hal_set_output_value(&s_gpio.hal, gpio_id, 1);
}

bk_err_t bk_gpio_set_output_low(gpio_id_t gpio_id)
{
  GPIO_RETURN_ON_INVALID_ID(gpio_id);

  return gpio_hal_set_output_value(&s_gpio.hal, gpio_id, 0);
}

bool bk_gpio_get_output(gpio_id_t gpio_id)
{
  GPIO_RETURN_ON_INVALID_ID(gpio_id);

  return gpio_hal_get_output(&s_gpio.hal, gpio_id);
}

bool bk_gpio_get_input(gpio_id_t gpio_id)
{
  GPIO_RETURN_ON_INVALID_ID(gpio_id);

  return gpio_hal_get_input(&s_gpio.hal, gpio_id);
}

bool bk_gpio_set_capacity(gpio_id_t gpio_id, uint32 capacity)
{
  GPIO_RETURN_ON_INVALID_ID(gpio_id);

  return gpio_hal_set_capacity(&s_gpio.hal, gpio_id, capacity);
}

bk_err_t bk_gpio_set_config(gpio_id_t gpio_id, const gpio_config_t *config)
{
  GPIO_RETURN_ON_INVALID_ID(gpio_id);
  GPIO_RETURN_ON_INVALID_IO_MODE(config->io_mode);
  GPIO_RETURN_ON_INVALID_PULL_MODE(config->pull_mode);

  return gpio_hal_set_config(&s_gpio.hal, gpio_id, config);
}

/* gpio_driver_base.c:47 -- one gpio_isr_t per pin, filled on register. */

static gpio_isr_t s_gpio_isr[SOC_GPIO_NUM];

bk_err_t bk_gpio_register_isr(gpio_id_t gpio_id, gpio_isr_t isr)
{
  GPIO_RETURN_ON_INVALID_ID(gpio_id);
  s_gpio_isr[gpio_id] = isr;

  return BK_OK;
}

bk_err_t bk_gpio_enable_interrupt(gpio_id_t gpio_id)
{
  GPIO_RETURN_ON_INVALID_ID(gpio_id);

  /* Before enabling the interrupt, wait for the chip to settle. */

  for (volatile int i = 0; i < 1000; i++)
    {
    }

  return gpio_hal_enable_interrupt(&s_gpio.hal, gpio_id);
}

bk_err_t bk_gpio_disable_interrupt(gpio_id_t gpio_id)
{
  GPIO_RETURN_ON_INVALID_ID(gpio_id);

  /* Only the pin's own enable: the CPU-side group IRQ is shared by every
   * GPIO and stays as it is.
   */

  gpio_hal_disable_interrupt(&s_gpio.hal, gpio_id);
  return BK_OK;
}

bk_err_t bk_gpio_clear_interrupt(gpio_id_t gpio_id)
{
  GPIO_RETURN_ON_INVALID_ID(gpio_id);

  gpio_hal_clear_chan_interrupt_status(&s_gpio.hal, gpio_id);
  return BK_OK;
}

bk_err_t bk_gpio_set_interrupt_type(gpio_id_t gpio_id, gpio_int_type_t type)
{
  GPIO_RETURN_ON_INVALID_ID(gpio_id);
  GPIO_RETURN_ON_INVALID_INT_TYPE_MODE(type);

  return gpio_hal_set_int_type(&s_gpio.hal, gpio_id, type);
}

/* gpio_driver.c:41-63, CONFIG_GPIO_DEFAULT_SET_SUPPORT=1: a pin may only be
 * muxed if the board's GPIO_DEFAULT_DEV_CONFIG (usr_gpio_cfg.h) lists it.
 */

static const gpio_default_map_t s_default_map[] = GPIO_DEFAULT_DEV_CONFIG;

static const gpio_default_map_t *get_gpio_config(gpio_id_t gpio_id)
{
  for (int i = 0; i < sizeof(s_default_map) / sizeof(gpio_default_map_t);
       i++)
    {
      if (s_default_map[i].gpio_id == gpio_id)
        {
          return &s_default_map[i];
        }
    }

  return NULL;
}

bk_err_t gpio_dev_map(gpio_id_t gpio_id, gpio_dev_t dev)
{
  if (get_gpio_config(gpio_id) == NULL)
    {
      return BK_ERR_GPIO_INVALID_OPERATE;
    }

  /* Restore a configuration that is not a secondary function to its
   * initial state, then select the peripheral.
   */

  gpio_hal_output_enable(&s_gpio.hal, gpio_id, 0);
  gpio_hal_input_enable(&s_gpio.hal, gpio_id, 0);
  gpio_hal_pull_enable(&s_gpio.hal, gpio_id, 0);
  gpio_hal_disable_interrupt(&s_gpio.hal, gpio_id);
  gpio_hal_func_map(&s_gpio.hal, gpio_id, dev);

  return BK_OK;
}

bk_err_t gpio_dev_unmap(gpio_id_t gpio_id)
{
  if (get_gpio_config(gpio_id) == NULL)
    {
      return BK_ERR_GPIO_INVALID_OPERATE;
    }

  gpio_hal_output_enable(&s_gpio.hal, gpio_id, 0);
  gpio_hal_input_enable(&s_gpio.hal, gpio_id, 0);
  gpio_hal_pull_enable(&s_gpio.hal, gpio_id, 0);
  gpio_hal_disable_interrupt(&s_gpio.hal, gpio_id);
  gpio_hal_func_unmap(&s_gpio.hal, gpio_id);

  return BK_OK;
}

/* gpio_driver.c:123-197: select a whole peripheral's pin group from the
 * SoC's map tables (gpio_map.h).  No caller in this image today; they are
 * the driver's exported contract.
 */

#define GPIO_RETURN_ON_INVALID_PERIAL_MODE(mode, mode_max) \
  do \
    { \
      if ((mode) >= (mode_max)) \
        { \
          return BK_ERR_GPIO_SET_INVALID_FUNC_MODE; \
        } \
    } \
  while (0)

bk_err_t gpio_i2c1_sel(gpio_i2c1_map_mode_t mode)
{
  GPIO_RETURN_ON_INVALID_PERIAL_MODE(mode, GPIO_I2C1_MAP_MODE_MAX);

  GPIO_MAP_TABLE(GPIO_I2C1_USED_GPIO_NUM, GPIO_I2C1_MAP_MODE_MAX,
                 i2c1_gpio_map) = GPIO_I2C1_MAP_TABLE;

  gpio_hal_devs_map(&s_gpio.hal, i2c1_gpio_map[mode].gpio_bits,
                    i2c1_gpio_map[mode].devs, GPIO_I2C1_USED_GPIO_NUM);

  return BK_OK;
}

bk_err_t gpio_i2s_sel(gpio_i2s_map_mode_t mode)
{
  GPIO_RETURN_ON_INVALID_PERIAL_MODE(mode, GPIO_I2S_MAP_MODE_MAX);

  GPIO_MAP_TABLE(GPIO_I2S_USED_GPIO_NUM, GPIO_I2S_MAP_MODE_MAX,
                 i2s_gpio_map) = GPIO_I2S_MAP_TABLE;

  gpio_hal_devs_map(&s_gpio.hal, i2s_gpio_map[mode].gpio_bits,
                    i2s_gpio_map[mode].devs, GPIO_I2S_USED_GPIO_NUM);

  return BK_OK;
}

bk_err_t gpio_spi_sel(gpio_spi1_map_mode_t mode)
{
  GPIO_RETURN_ON_INVALID_PERIAL_MODE(mode, GPIO_SPI_MAP_MODE_MAX);

  GPIO_MAP_TABLE(GPIO_SPI0_USED_GPIO_NUM, GPIO_SPI_MAP_MODE_MAX,
                 spi_gpio_map) = GPIO_SPI0_MAP_TABLE;

  gpio_hal_devs_map(&s_gpio.hal, spi_gpio_map[mode].gpio_bits,
                    spi_gpio_map[mode].devs, GPIO_SPI0_USED_GPIO_NUM);

  return BK_OK;
}

bk_err_t gpio_sdio_sel(gpio_sdio_map_mode_t mode)
{
  GPIO_RETURN_ON_INVALID_PERIAL_MODE(mode, GPIO_SDIO_MAP_MODE_MAX);

  GPIO_MAP_TABLE(GPIO_SDIO_USED_GPIO_NUM, GPIO_SDIO_MAP_MODE_MAX,
                 sdio_gpio_map) = GPIO_SDIO_MAP_TABLE;

  gpio_hal_devs_map(&s_gpio.hal, sdio_gpio_map[mode].gpio_bits,
                    sdio_gpio_map[mode].devs, GPIO_SDIO_USED_GPIO_NUM);

  return BK_OK;
}

bk_err_t gpio_sdio_one_line_sel(gpio_sdio_map_mode_t mode)
{
  GPIO_RETURN_ON_INVALID_PERIAL_MODE(mode, GPIO_SDIO_MAP_MODE_MAX);

  GPIO_MAP_TABLE(GPIO_SDIO_ONE_LINE_USED_GPIO_NUM, GPIO_SDIO_MAP_MODE_MAX,
                 sdio_gpio_map) = GPIO_SDIO_ONE_LINE_MAP_TABLE;

  gpio_hal_devs_map(&s_gpio.hal, sdio_gpio_map[mode].gpio_bits,
                    sdio_gpio_map[mode].devs,
                    GPIO_SDIO_ONE_LINE_USED_GPIO_NUM);

  return BK_OK;
}

bk_err_t gpio_jtag_sel(gpio_jtag_map_group_t group_id)
{
  if (group_id == GPIO_JTAG_MAP_GROUP0)
    {
      gpio_hal_func_unmap(&s_gpio.hal, GPIO_20);
      gpio_hal_func_unmap(&s_gpio.hal, GPIO_21);
      gpio_hal_func_map(&s_gpio.hal, GPIO_20, GPIO_DEV_JTAG_TCK);
      gpio_hal_func_map(&s_gpio.hal, GPIO_21, GPIO_DEV_JTAG_TMS);
    }
  else if (group_id == GPIO_JTAG_MAP_GROUP1)
    {
      gpio_hal_func_unmap(&s_gpio.hal, GPIO_0);
      gpio_hal_func_unmap(&s_gpio.hal, GPIO_1);
      gpio_hal_func_map(&s_gpio.hal, GPIO_0, GPIO_DEV_JTAG_TCK);
      gpio_hal_func_map(&s_gpio.hal, GPIO_1, GPIO_DEV_JTAG_TMS);
    }
  else
    {
      GPIO_LOGD("Unsupported group id(%d).\r\n", group_id);
      return BK_FAIL;
    }

  return BK_OK;
}

/* gpio_driver.c:362-411: check a pin against the board's default table. */

bk_err_t gpio_check_dev_match(gpio_id_t gpio_id, gpio_dev_t dev)
{
  const gpio_default_map_t *config = get_gpio_config(gpio_id);
  bool func_mode_en = true;

  if (config == NULL)
    {
      GPIO_LOGW("GPIO%d not found in GPIO_DEFAULT_DEV_CONFIG table\r\n",
                gpio_id);
      return BK_ERR_GPIO_INVALID_OPERATE;
    }

  if (config->second_func_en != GPIO_SECOND_FUNC_ENABLE)
    {
      func_mode_en = false;
      GPIO_LOGW("GPIO%d not enable second func\r\n", gpio_id);
    }

  if (config->second_func_dev != dev)
    {
      func_mode_en = false;
      GPIO_LOGW("GPIO%d dev mismatch, expect 0x%x, but current is 0x%x\r\n",
                gpio_id, dev, config->second_func_dev);
    }

  if (!func_mode_en)
    {
      GPIO_LOGW("gpio id=%d func_mode is not match\r\n", gpio_id);
      return BK_ERR_GPIO_CONFIG_MISMATCH;
    }

  return BK_OK;
}

bk_err_t gpio_check_capacity_match(gpio_id_t gpio_id, uint8_t expect_capacity)
{
  const gpio_default_map_t *config = get_gpio_config(gpio_id);

  if (config == NULL)
    {
      GPIO_LOGW("GPIO%d not found in GPIO_DEFAULT_DEV_CONFIG table\r\n",
                gpio_id);
      return BK_ERR_GPIO_INVALID_OPERATE;
    }

  if (config->driver_capacity != expect_capacity)
    {
      GPIO_LOGW("GPIO%d driver capacity mismatch, expect %d, but current is "
                "%d\r\n", gpio_id, expect_capacity, config->driver_capacity);
      return BK_ERR_GPIO_CONFIG_MISMATCH;
    }

  return BK_OK;
}

/* gpio_driver_base.c:479 */

void gpio_get_interrupt_status(uint32_t *h_status, uint32_t *l_status)
{
  gpio_hal_t *hal = &s_gpio.hal;
  gpio_interrupt_status_t gpio_status;

  gpio_hal_get_interrupt_status(hal, &gpio_status);
  if (h_status)
    {
      *h_status = gpio_status.gpio_32_64_int_status;
    }

  if (l_status)
    {
      *l_status = gpio_status.gpio_0_31_int_status;
    }
}

/* Wakeup, low-power keep-status and retention: the part of the two files
 * that belongs to PM.  What is here is what the image links today.  The
 * other 13 exports of this group -- gpio_enter/exit_low_power,
 * gpio_enable_interrupt_mult_for_wake, bk_gpio_retention_set/clr,
 * gpio_retention_map_set/clr, the four s_gpio_bak_* / s_gpio_is_* tables
 * and the gpio_ipc channel -- are deferred to T12 (PM) and T16 (cross-core);
 * see the exception table in docs/bk7258-nativization-contract.md.  Nothing
 * in the image references them.
 *
 * The retention table and the ana wakeup slots are copied, not re-derived
 * -- they encode the board's pin choices (GPIO_RETENTION_MAP,
 * GPIO_STATIC_WAKEUP_SOURCE_MAP) and the PMU bitmap protocol.
 */

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

          gpio_hal_set_value(&s_gpio.hal, id, (bitmap & BIT(i)) ? 2 : 0);
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

  /* gpio_driver_base.c:144.  This also points bk7258_gpio_hal.c's mux
   * accessor at the SYS block; gpio_hal_func_map() depends on it.
   */

  gpio_hal_init(&s_gpio.hal);
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
 * bk_gpio_set_output_high/low() and bk_gpio_get_value() -- there is no second
 * register path.
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
