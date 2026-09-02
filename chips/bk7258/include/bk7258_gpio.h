/****************************************************************************
 * chips/bk7258/include/bk7258_gpio.h
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to you under the Apache License, Version
 * 2.0 (the "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
 * implied.  See the License for the specific language governing
 * permissions and limitations under the License.
 *
 ****************************************************************************/

#ifndef __VENDOR_BEKEN_CHIP_BK7258_INCLUDE_BK7258_GPIO_H
#define __VENDOR_BEKEN_CHIP_BK7258_INCLUDE_BK7258_GPIO_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdbool.h>
#include <stdint.h>

#include <nuttx/irq.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Maximum GPIO pin count for BK7258 */

#define BK7258_GPIO_MAX_PIN  56

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* GPIO pull resistor configuration */

enum bk7258_gpio_pull_e
{
  BK7258_GPIO_PULL_NONE = 0,  /* No pull resistor */
  BK7258_GPIO_PULL_UP   = 1,  /* Pull-up enabled */
  BK7258_GPIO_PULL_DOWN = 2   /* Pull-down enabled */
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef __cplusplus
#define EXTERN extern "C"
extern "C"
{
#else
#define EXTERN extern
#endif

/****************************************************************************
 * Name: bk7258_gpio_config_input
 *
 * Description:
 *   Configure a GPIO pin as an input with optional pull resistor.
 *
 * Input Parameters:
 *   pin  - GPIO pin number (0 to BK7258_GPIO_MAX_PIN-1)
 *   pull - Pull resistor configuration
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno on failure.
 *
 ****************************************************************************/

int bk7258_gpio_config_input(unsigned int pin,
                             enum bk7258_gpio_pull_e pull);

/****************************************************************************
 * Name: bk7258_gpio_config_output
 *
 * Description:
 *   Configure a GPIO pin as a push-pull output with no pull resistor.
 *   The initial output level is set before switching to output mode
 *   to avoid glitches.
 *
 * Input Parameters:
 *   pin     - GPIO pin number (0 to BK7258_GPIO_MAX_PIN-1)
 *   initial - Initial output level (true = high, false = low)
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno on failure.
 *
 ****************************************************************************/

int bk7258_gpio_config_output(unsigned int pin, bool initial);

/****************************************************************************
 * Name: bk7258_gpio_read
 *
 * Description:
 *   Read the current level of a GPIO pin.
 *
 * Input Parameters:
 *   pin - GPIO pin number (0 to BK7258_GPIO_MAX_PIN-1)
 *
 * Returned Value:
 *   true if the pin is high, false if low.
 *
 ****************************************************************************/

bool bk7258_gpio_read(unsigned int pin);

/****************************************************************************
 * Name: bk7258_gpio_write
 *
 * Description:
 *   Drive a GPIO output pin to the requested level.
 *
 * Input Parameters:
 *   pin   - GPIO pin number (0 to BK7258_GPIO_MAX_PIN-1)
 *   value - Output level (true = high, false = low)
 *
 ****************************************************************************/

void bk7258_gpio_write(unsigned int pin, bool value);

/****************************************************************************
 * GPIO Interrupt Ownership
 *
 * ICU source 55 and the aggregate GPIO pending registers are owned
 * exclusively by the AP image.  GPIO interrupt attach/enable operations
 * return -ENOSYS in a CP build, and disable is a no-op there.  Non-interrupt
 * GPIO and UART/pin-mux operations remain available to both images.
 *
 ****************************************************************************/

/****************************************************************************
 * Name: bk7258_gpio_irq_attach
 *
 * Description:
 *   Attach an interrupt handler to a GPIO pin.
 *
 * Input Parameters:
 *   pin     - GPIO pin number (0 to BK7258_GPIO_MAX_PIN-1)
 *   handler - Interrupt handler function pointer
 *   arg     - Argument passed to the handler
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno on failure.
 *
 ****************************************************************************/

int bk7258_gpio_irq_attach(unsigned int pin, xcpt_t handler, void *arg);

/****************************************************************************
 * Name: bk7258_gpio_irq_initialize
 *
 * Description:
 *   Initialize and route the shared GPIO interrupt source.
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno on failure.
 *
 ****************************************************************************/

int bk7258_gpio_irq_initialize(void);

/****************************************************************************
 * Name: bk7258_gpio_irq_enable
 *
 * Description:
 *   Enable GPIO interrupt with specified edge configuration.
 *
 * Input Parameters:
 *   pin     - GPIO pin number (0 to BK7258_GPIO_MAX_PIN-1)
 *   rising  - Enable rising edge interrupt
 *   falling - Enable falling edge interrupt
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno on failure.
 *
 ****************************************************************************/

int bk7258_gpio_irq_enable(unsigned int pin, bool rising, bool falling);

/****************************************************************************
 * Name: bk7258_gpio_irq_disable
 *
 * Description:
 *   Disable GPIO interrupt for the specified pin.
 *
 * Input Parameters:
 *   pin - GPIO pin number (0 to BK7258_GPIO_MAX_PIN-1)
 *
 ****************************************************************************/

void bk7258_gpio_irq_disable(unsigned int pin);

/****************************************************************************
 * Name: bk7258_gpio_wifi_txen / bk7258_gpio_wifi_rxen
 *
 * Description:
 *   Mux GPIO26 (TXEN) / GPIO28 (RXEN) for the WiFi radio control lines
 *   (both function slot 0).
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno on failure.
 *
 ****************************************************************************/

int bk7258_gpio_wifi_txen(void);
int bk7258_gpio_wifi_rxen(void);

#undef EXTERN
#ifdef __cplusplus
}
#endif

#endif /* __VENDOR_BEKEN_CHIP_BK7258_INCLUDE_BK7258_GPIO_H */
