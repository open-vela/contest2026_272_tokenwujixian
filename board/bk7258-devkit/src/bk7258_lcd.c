/****************************************************************************
 * board/bk7258-devkit/src/bk7258_lcd.c
 *
 * SPDX-License-Identifier: Apache-2.0
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

#include <stdbool.h>
#include <stdint.h>
#include <errno.h>
#include <debug.h>

#include <nuttx/arch.h>
#include <nuttx/board.h>
#include <nuttx/spi/spi.h>
#include <nuttx/lcd/lcd.h>

#include "bk7258_internal.h"
#include "board.h"

#ifdef CONFIG_LCD_JD9853
#  include "jd9853.h"
#endif
#ifdef CONFIG_LCD_GC9D01
#  include "gc9d01.h"
#endif

#if defined(CONFIG_LCD_JD9853) || defined(CONFIG_LCD_GC9D01)

/****************************************************************************
 * Private Data
 ****************************************************************************/

static FAR struct spi_dev_s *g_lcd_spi;
static FAR struct lcd_dev_s *g_lcd_dev;
static int (*g_lcd_setpower)(FAR struct lcd_dev_s *dev, int power);

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bk7258_backlight
 *
 * Description:
 *   Drive the display backlight switch (LCD_BL_PWM net, active high into
 *   the Q3 low-side switch).
 *
 ****************************************************************************/

static void bk7258_backlight(bool on)
{
  bk7258_gpio_write(BOARD_LCD_BL_PIN, on);
}

/****************************************************************************
 * Name: bk7258_lcd_hw_reset
 *
 * Description:
 *   Assert the panel hardware reset line (active low) with the timing
 *   required by the JD9853 datasheet.
 *
 ****************************************************************************/

static void bk7258_lcd_hw_reset(void)
{
  bk7258_gpio_config_output(BOARD_LCD_RST_PIN, false);

  bk7258_gpio_write(BOARD_LCD_RST_PIN, false);
  up_mdelay(20);
  bk7258_gpio_write(BOARD_LCD_RST_PIN, true);
  up_mdelay(120);
}

/****************************************************************************
 * Name: bk7258_lcd_setpower
 *
 * Description:
 *   Wrapper over the JD9853 setpower callback that also drives the board
 *   backlight switch.  Replaces the driver's own callback in place so the
 *   lcd_dev_s container pointer stays valid.
 *
 ****************************************************************************/

static int bk7258_lcd_setpower(FAR struct lcd_dev_s *dev, int power)
{
  int ret;

  ret = g_lcd_setpower(dev, power);
  if (ret == OK)
    {
      bk7258_backlight(power > 0);
    }

  return ret;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: board_lcd_initialize
 *
 * Description:
 *   Initialize the display SPI bit-bang interface, assert the panel reset
 *   and bring the JD9853 LCD driver to its ready state.
 *
 ****************************************************************************/

int board_lcd_initialize(void)
{
  if (g_lcd_dev != NULL)
    {
      return OK;
    }

  /* Enable the LDO_3V3 rail that powers the LCD (reference firmware
   * drives GPIO52 high for this).  Do this first so the panel has power.
   */

  bk7258_gpio_config_output(BOARD_LCD_LDO_PIN, false);
  bk7258_gpio_write(BOARD_LCD_LDO_PIN, true);

  /* Backlight switch is driven by GPIO25 (LCD_BL_PWM).  Keep it off until
   * the panel is initialized and powered on.
   */

  bk7258_gpio_config_output(BOARD_LCD_BL_PIN, false);
  bk7258_backlight(false);

  /* Hold the panel in reset while the SPI lines are brought up */

  bk7258_lcd_hw_reset();

  g_lcd_spi = bk7258_spi_initialize();
  if (g_lcd_spi == NULL)
    {
      lcderr("ERROR: Failed to initialize display SPI\n");
      return -ENODEV;
    }

#if defined(CONFIG_LCD_JD9853)
  g_lcd_dev = jd9853_lcdinitialize(g_lcd_spi);
#elif defined(CONFIG_LCD_GC9D01)
  g_lcd_dev = gc9d01_lcdinitialize(g_lcd_spi);
#endif
  if (g_lcd_dev == NULL)
    {
      lcderr("ERROR: Failed to initialize LCD\n");
      return -ENODEV;
    }

  /* Tie the board backlight to the panel power state */

  g_lcd_setpower       = g_lcd_dev->setpower;
  g_lcd_dev->setpower  = bk7258_lcd_setpower;

  /* The panel is initialized with the display on; enable the backlight
   * now so content is visible without an explicit SETPOWER.  The LVGL
   * NuttX LCD port (lv_nuttx_lcd) does not issue LCDDEVIO_SETPOWER.
   */

  g_lcd_dev->setpower(g_lcd_dev, 1);

  return OK;
}

/****************************************************************************
 * Name: board_lcd_getdev
 *
 * Description:
 *   Return the JD9853 LCD device instance.
 *
 ****************************************************************************/

FAR struct lcd_dev_s *board_lcd_getdev(int lcddev)
{
  return (lcddev == 0) ? g_lcd_dev : NULL;
}

/****************************************************************************
 * Name: board_lcd_uninitialize
 *
 * Description:
 *   Disable the display backlight and release the LCD resources.
 *
 ****************************************************************************/

void board_lcd_uninitialize(void)
{
  bk7258_backlight(false);
}

#endif /* CONFIG_LCD_JD9853 || CONFIG_LCD_GC9D01 */
