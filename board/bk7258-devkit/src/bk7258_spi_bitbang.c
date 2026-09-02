/****************************************************************************
 * board/bk7258-devkit/src/bk7258_spi_bitbang.c
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

#include <nuttx/spi/spi.h>
#include <nuttx/spi/spi_bitbang.h>

#include "bk7258_internal.h"
#include "board.h"

#if defined(CONFIG_SPI_BITBANG) && defined(CONFIG_BK7258_LCD1_SPI_BITBANG)

/* The DevKit display FPC provides no MISO line, so the RX path always
 * reads back zero.
 */

#define SPI_SETSCK  bk7258_gpio_write(BOARD_LCD_SCL_PIN, true)
#define SPI_CLRSCK  bk7258_gpio_write(BOARD_LCD_SCL_PIN, false)
#define SPI_SETMOSI bk7258_gpio_write(BOARD_LCD_SDA_PIN, true)
#define SPI_CLRMOSI bk7258_gpio_write(BOARD_LCD_SDA_PIN, false)
#define SPI_GETMISO (0)

/* Only mode 0 is required by the JD9853 panel */

#undef  SPI_BITBANG_DISABLEMODE0
#define SPI_BITBANG_DISABLEMODE1 1
#define SPI_BITBANG_DISABLEMODE2 1
#define SPI_BITBANG_DISABLEMODE3 1

/* Only 8-bit data width */

#undef SPI_BITBANG_VARWIDTH

/* Delay loop calibration: approximate iterations of the bit-bang delay loop
 * per millisecond on the BK7258 CP core.  This only affects the reported
 * frequency and the timing margins, not the correctness of the protocol.
 */

#define SPI_BITBANG_LOOPSPERMSEC 10000

/* SPI_PERBIT_NSEC is the minimum time to transfer one bit.  This determines
 * the maximum frequency and is also used to calculate delays to achieve
 * other SPI frequencies.
 */

#define SPI_PERBIT_NSEC      100

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

/* Lower-half SPI */

static void spi_select(FAR struct spi_bitbang_s *priv, uint32_t devid,
                       bool selected);
static uint8_t spi_status(FAR struct spi_bitbang_s *priv, uint32_t devid);
#ifdef CONFIG_SPI_CMDDATA
static int spi_cmddata(FAR struct spi_bitbang_s *priv, uint32_t devid,
                       bool cmd);
#endif

/* Include the generic bit-bang skeleton.  This provides g_spiops, the
 * bitexchange functions and the frequency/mode handling.
 */

#include <nuttx/spi/spi_bitbang.c>

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: spi_select
 *
 * Description:
 *   Assert or de-assert the LCD chip select (active low).
 *
 ****************************************************************************/

static void spi_select(FAR struct spi_bitbang_s *priv, uint32_t devid,
                       bool selected)
{
  bk7258_gpio_write(BOARD_LCD_CS_PIN, !selected);
}

/****************************************************************************
 * Name: spi_status
 *
 * Description:
 *   Return status of the SPI device specified by 'devid'
 *
 ****************************************************************************/

static uint8_t spi_status(FAR struct spi_bitbang_s *priv, uint32_t devid)
{
  return 0;
}

/****************************************************************************
 * Name: spi_cmddata
 *
 * Description:
 *   Select command or data mode through the D/C line.
 *
 ****************************************************************************/

#ifdef CONFIG_SPI_CMDDATA
static int spi_cmddata(FAR struct spi_bitbang_s *priv, uint32_t devid,
                       bool cmd)
{
  bk7258_gpio_write(BOARD_LCD_DC_PIN, !cmd);
  return OK;
}
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bk7258_spi_initialize
 *
 * Description:
 *   Initialize the bit-bang SPI interface used by the display connector
 *   and return a NuttX SPI device instance.
 *
 ****************************************************************************/

FAR struct spi_dev_s *bk7258_spi_initialize(void)
{
  /* Configure the bit-bang GPIO pins */

  bk7258_gpio_config_output(BOARD_LCD_SCL_PIN, false);
  bk7258_gpio_config_output(BOARD_LCD_SDA_PIN, false);
  bk7258_gpio_config_output(BOARD_LCD_CS_PIN, true);
  bk7258_gpio_config_output(BOARD_LCD_DC_PIN, false);

  /* Idle levels: CS de-asserted high, D/C driven low until a command or
   * data transfer begins.
   */

  bk7258_gpio_write(BOARD_LCD_CS_PIN, true);
  bk7258_gpio_write(BOARD_LCD_DC_PIN, false);
  bk7258_gpio_write(BOARD_LCD_SCL_PIN, false);

  return spi_create_bitbang(&g_spiops, NULL);
}

#endif /* CONFIG_SPI_BITBANG && CONFIG_BK7258_LCD1_SPI_BITBANG */
