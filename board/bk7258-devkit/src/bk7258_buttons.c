/****************************************************************************
 * board/bk7258-devkit/src/bk7258_buttons.c
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
#include <stdbool.h>
#include <stdint.h>
#include <syslog.h>

#include <nuttx/irq.h>

#include "bk7258_gpio.h"
#include "board.h"

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const uint8_t g_button_pins[NUM_BUTTONS] =
{
  BOARD_KEY1_PIN,
  BOARD_KEY2_PIN,
  BOARD_KEY3_PIN,
};

static const uint32_t g_button_bits[NUM_BUTTONS] =
{
  BUTTON_KEY1_BIT,
  BUTTON_KEY2_BIT,
  BUTTON_KEY3_BIT,
};

/****************************************************************************
 * Public Functions
 ****************************************************************************/

uint32_t board_button_initialize(void)
{
  unsigned int id;
  int ret;

  for (id = 0; id < NUM_BUTTONS; id++)
    {
      ret = bk7258_gpio_config_input(g_button_pins[id],
                                     BK7258_GPIO_PULL_UP);
      if (ret < 0)
        {
          syslog(LOG_ERR, "BK7258 btn: GPIO%d configuration failed: %d\n",
                 g_button_pins[id], ret);
          return 0;
        }
    }

#ifdef CONFIG_ARCH_IRQBUTTONS
  ret = bk7258_gpio_irq_initialize();
  if (ret < 0)
    {
      syslog(LOG_ERR, "BK7258 btn: GPIO IRQ initialization failed: %d\n",
             ret);
      return 0;
    }
#endif

  return NUM_BUTTONS;
}

uint32_t board_buttons(void)
{
  uint32_t buttons = 0;
  unsigned int id;

  for (id = 0; id < NUM_BUTTONS; id++)
    {
      if (!bk7258_gpio_read(g_button_pins[id]))
        {
          buttons |= g_button_bits[id];
        }
    }

  return buttons;
}

#ifdef CONFIG_ARCH_IRQBUTTONS
int board_button_irq(int id, xcpt_t irqhandler, void *arg)
{
  unsigned int pin;
  int ret;

  if (id < 0 || id >= NUM_BUTTONS)
    {
      return -EINVAL;
    }

  pin = g_button_pins[id];

  if (irqhandler == NULL)
    {
      bk7258_gpio_irq_disable(pin);
      ret = bk7258_gpio_irq_attach(pin, NULL, NULL);
      if (ret < 0)
        {
          syslog(LOG_ERR, "BK7258 btn: GPIO%d IRQ detach failed: %d\n",
                 pin, ret);
        }

      return ret;
    }

  ret = bk7258_gpio_irq_attach(pin, irqhandler, arg);
  if (ret < 0)
    {
      syslog(LOG_ERR, "BK7258 btn: GPIO%d IRQ attach failed: %d\n",
             pin, ret);
      return ret;
    }

  ret = bk7258_gpio_irq_enable(pin, true, true);
  if (ret < 0)
    {
      bk7258_gpio_irq_disable(pin);
      (void)bk7258_gpio_irq_attach(pin, NULL, NULL);
      syslog(LOG_ERR, "BK7258 btn: GPIO%d IRQ enable failed: %d\n",
             pin, ret);
    }

  return ret;
}
#endif
