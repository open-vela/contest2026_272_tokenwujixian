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
