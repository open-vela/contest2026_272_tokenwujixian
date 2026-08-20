/****************************************************************************
 * board/bk7258-devkit/src/bk7258_spi_hw.c
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
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * This lower-half drives the LCD1 display FPC through the SoC SPI1
 * controller (the "QSPI1" net on the schematic maps to the SPI1 pads
 * GPIO2/3/4/5; see the ARMINO bk7258 gpio_map.h mux option 0).  It
 * replaces the GPIO bit-bang lower-half (bk7258_spi_bitbang.c) with the
 * hardware SPI master so the JD9853/GC9D01 SPI panels can be clocked at
 * MHz rates instead of a bit-bang crawl.
 *
 * Interface notes:
 *  - The FPC carries no MISO, so RX always samples whatever the pad
 *    drives; reads are only used by getrun() which the panel never
 *    requires.
 *  - CS (GPIO3) and D/C (GPIO5) stay plain GPIOs.  CS must be held low
 *    across the whole RAMWR pixel burst, and the display command/data
 *    selection must not be tied to the SPI controller, so both lines are
 *    driven by software.
 *  - SPI mode 0, 8-bit, MSB-first, master, XTAL 26M source clock.  The
 *    master transfer length is capped at 4095 bytes by the tx_trans_len
 *    field; larger writes are chunked while CS remains asserted.
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

#include <nuttx/arch.h>
#include <nuttx/kmalloc.h>
#include <nuttx/mutex.h>
#include <nuttx/spi/spi.h>

#include "arm_internal.h"
#include "bk7258_internal.h"
#include "board.h"
#include "bk7258_memorymap.h"
#include "bk7258_spi.h"

#ifdef CONFIG_BK7258_LCD1_SPI_HW

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* SPI1 peripheral mux option 0 selects the SPI1 function on GPIO2/3/4/5
 * (ARMINO gpio_map.h).  GPIO2=SCK, GPIO4=MOSI are routed to the SPI
 * controller; GPIO3=CS and GPIO5=D/C are kept as plain GPIO outputs.
 */

#define BK7258_SPI1_FUNC_SCK  0
#define BK7258_SPI1_FUNC_MOSI 0

/* Poll-loop bounds.  Each wait is bounded so a missing FIFO/busy bit can
 * never hang the LCD flush path.
 */

#define BK7258_SPI_WR_READY_MAX  1000000
#define BK7258_SPI_FINISH_MAX    10000000

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct bk7258_spi_dev_s
{
  struct spi_dev_s dev;
  FAR bk7258_spi_hw_t *hw;   /* SPI1 register block */
  mutex_t lock;              /* Mutual exclusion */
  uint32_t frequency;        /* Requested frequency */
  uint32_t actual;           /* Achieved frequency */
  uint8_t mode;              /* Current SPI mode */
  uint8_t nbits;             /* Current bit width (8 only) */
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int      bk7258_spi_lock(FAR struct spi_dev_s *dev, bool lock);
static void     bk7258_spi_select(FAR struct spi_dev_s *dev, uint32_t devid,
                                  bool selected);
static uint32_t bk7258_spi_setfrequency(FAR struct spi_dev_s *dev,
                                        uint32_t frequency);
static void     bk7258_spi_setmode(FAR struct spi_dev_s *dev,
                                   enum spi_mode_e mode);
static void     bk7258_spi_setbits(FAR struct spi_dev_s *dev, int nbits);
static uint8_t  bk7258_spi_status(FAR struct spi_dev_s *dev,
                                  uint32_t devid);
#ifdef CONFIG_SPI_CMDDATA
static int      bk7258_spi_cmddata(FAR struct spi_dev_s *dev,
                                   uint32_t devid, bool cmd);
#endif
static uint32_t bk7258_spi_send(FAR struct spi_dev_s *dev, uint32_t wd);
static void     bk7258_spi_exchange(FAR struct spi_dev_s *dev,
                                    FAR const void *txbuffer,
                                    FAR void *rxbuffer, size_t nwords);

static void bk7258_spi_transfer(FAR struct bk7258_spi_dev_s *priv,
                                FAR const uint8_t *txbuf, FAR uint8_t *rxbuf,
                                size_t nwords);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct spi_ops_s g_bk7258_spiops =
{
  bk7258_spi_lock,          /* lock */
  bk7258_spi_select,        /* select */
  bk7258_spi_setfrequency,  /* setfrequency */
  bk7258_spi_setmode,       /* setmode */
  bk7258_spi_setbits,       /* setbits */
  bk7258_spi_status,        /* status */
#ifdef CONFIG_SPI_CMDDATA
  bk7258_spi_cmddata,       /* cmddata */
#endif
  bk7258_spi_send,          /* send */
  bk7258_spi_exchange,      /* exchange */
  0                         /* registercallback */
};

static struct bk7258_spi_dev_s g_spidev;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int bk7258_spi_lock(FAR struct spi_dev_s *dev, bool lock)
{
  FAR struct bk7258_spi_dev_s *priv = (FAR struct bk7258_spi_dev_s *)dev;
  int ret;

  if (lock)
    {
      ret = nxmutex_lock(&priv->lock);
    }
  else
    {
      ret = nxmutex_unlock(&priv->lock);
    }

  return ret;
}

static void bk7258_spi_select(FAR struct spi_dev_s *dev, uint32_t devid,
                              bool selected)
{
  /* Active-low chip select driven by GPIO3 */

  bk7258_gpio_write(BOARD_LCD_CS_PIN, !selected);
}

static uint32_t bk7258_spi_setfrequency(FAR struct spi_dev_s *dev,
                                        uint32_t frequency)
{
  FAR struct bk7258_spi_dev_s *priv = (FAR struct bk7258_spi_dev_s *)dev;
  uint32_t clk_rate;

  if (frequency > BK7258_SPI1_MAX_BAUD)
    {
      frequency = BK7258_SPI1_MAX_BAUD;
    }

  /* baud = f_src / (2 * clk_rate), round up to stay below the request */

  clk_rate = (BK7258_SPI1_SRC_CLK + (2 * frequency) - 1) / (2 * frequency);
  if (clk_rate < 1)
    {
      clk_rate = 1;
    }
  else if (clk_rate > 0xff)
    {
      clk_rate = 0xff;
    }

  priv->hw->ctrl.clk_rate = clk_rate;

  priv->frequency = frequency;
  priv->actual    = BK7258_SPI1_SRC_CLK / (2 * clk_rate);

  return priv->actual;
}

static void bk7258_spi_setmode(FAR struct spi_dev_s *dev,
                               enum spi_mode_e mode)
{
  FAR struct bk7258_spi_dev_s *priv = (FAR struct bk7258_spi_dev_s *)dev;

  switch (mode)
    {
      case SPIDEV_MODE0: /* CPOL=0 CPHA=0 */
        priv->hw->ctrl.cpol = 0;
        priv->hw->ctrl.cpha = 0;
        break;

      case SPIDEV_MODE1: /* CPOL=0 CPHA=1 */
        priv->hw->ctrl.cpol = 0;
        priv->hw->ctrl.cpha = 1;
        break;

      case SPIDEV_MODE2: /* CPOL=1 CPHA=0 */
        priv->hw->ctrl.cpol = 1;
        priv->hw->ctrl.cpha = 0;
        break;

      case SPIDEV_MODE3: /* CPOL=1 CPHA=1 */
        priv->hw->ctrl.cpol = 1;
        priv->hw->ctrl.cpha = 1;
        break;

      default:
        printf("SPI1: ERROR invalid mode %d\n", mode);
        return;
    }

  priv->mode = mode;
}

static void bk7258_spi_setbits(FAR struct spi_dev_s *dev, int nbits)
{
  FAR struct bk7258_spi_dev_s *priv = (FAR struct bk7258_spi_dev_s *)dev;

  if (nbits != 8)
    {
      printf("SPI1: ERROR only 8-bit (%d)\n", nbits);
      return;
    }

  priv->hw->ctrl.bit_width = 0; /* 8-bit */
  priv->nbits = nbits;
}

static uint8_t bk7258_spi_status(FAR struct spi_dev_s *dev, uint32_t devid)
{
  return 0;
}

#ifdef CONFIG_SPI_CMDDATA
static int bk7258_spi_cmddata(FAR struct spi_dev_s *dev, uint32_t devid,
                              bool cmd)
{
  /* D/C line: low for command, high for data (same polarity as the
   * bit-bang lower-half).
   */

  bk7258_gpio_write(BOARD_LCD_DC_PIN, !cmd);
  return OK;
}
#endif

static uint32_t bk7258_spi_send(FAR struct spi_dev_s *dev, uint32_t wd)
{
  FAR struct bk7258_spi_dev_s *priv = (FAR struct bk7258_spi_dev_s *)dev;
  uint8_t tx = (uint8_t)wd;
  uint8_t rx;

  bk7258_spi_transfer(priv, &tx, &rx, 1);
  return rx;
}

static void bk7258_spi_exchange(FAR struct spi_dev_s *dev,
                                FAR const void *txbuffer,
                                FAR void *rxbuffer, size_t nwords)
{
  FAR struct bk7258_spi_dev_s *priv = (FAR struct bk7258_spi_dev_s *)dev;

  bk7258_spi_transfer(priv, (FAR const uint8_t *)txbuffer,
                      (FAR uint8_t *)rxbuffer, nwords);
}

static void bk7258_spi_transfer(FAR struct bk7258_spi_dev_s *priv,
                                FAR const uint8_t *txbuf, FAR uint8_t *rxbuf,
                                size_t nwords)
{
  FAR bk7258_spi_hw_t *hw = priv->hw;
  uint32_t timeout;

  while (nwords > 0)
    {
      size_t chunk = nwords > BK7258_SPI_MAX_TRANSFER ?
                     BK7258_SPI_MAX_TRANSFER : nwords;
      size_t i;

      /* Start a master transmit of exactly `chunk` bytes.  Software CS is
       * already held low by jd9853/gc9d01 select() across a burst, so the
       * panel sees one continuous CS-low window even when the 4095-byte
       * hardware transfer cap forces chunking.
       */

      hw->int_status.tx_fifo_clr = 1;   /* write-1-clear FIFO */
      hw->cfg.tx_trans_len = chunk;
      hw->cfg.tx_en = 1;

      for (i = 0; i < chunk; i++)
        {
          timeout = BK7258_SPI_WR_READY_MAX;
          while (!hw->int_status.tx_fifo_wr_ready)
            {
              if (--timeout == 0)
                {
                  printf("SPI1: ERROR tx fifo not ready (status=%08x)\n",
                         (unsigned int)hw->int_status.v);
                  goto next_chunk;
                }
            }

          hw->data.fifo_data = txbuf != NULL ? txbuf[i] : 0xff;
        }

      /* Wait for the last byte to shift out before the next chunk. */

      timeout = BK7258_SPI_FINISH_MAX;
      while (!hw->int_status.tx_finish_int)
        {
          if (--timeout == 0)
            {
              printf("SPI1: ERROR tx finish timeout "
                     "(status=%08x cfg=%08x dev_status=%08x)\n",
                     (unsigned int)hw->int_status.v,
                     (unsigned int)hw->cfg.v,
                     (unsigned int)hw->dev_status);
              break;
            }
        }

next_chunk:
      hw->int_status.tx_finish_int = 1;  /* write-1-clear */
      hw->int_status.tx_fifo_int   = 1;  /* write-1-clear */
      hw->cfg.tx_en = 0;

      if (txbuf != NULL)
        {
          txbuf += chunk;
        }

      if (rxbuf != NULL)
        {
          memset(rxbuf, 0, chunk);
          rxbuf += chunk;
        }

      nwords -= chunk;
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

FAR struct spi_dev_s *bk7258_spi_initialize(void)
{
  FAR struct bk7258_spi_dev_s *priv = &g_spidev;
  FAR bk7258_spi_hw_t *hw = (FAR bk7258_spi_hw_t *)BK7258_SPI1_BASE;
  uint32_t regval;

  /* Enable the SPI1 clock gate and pin the source clock to XTAL 26M. */

  regval  = getreg32(BK7258_SYS_DEV_CLK_EN);
  regval |= BK7258_SYS_SPI1_CLK_EN;
  putreg32(regval, BK7258_SYS_DEV_CLK_EN);

  regval  = getreg32(BK7258_SYS_CLKSEL);
  regval &= ~BK7258_SYS_SPI1_APLL;
  putreg32(regval, BK7258_SYS_CLKSEL);

  /* Route SCK (GPIO2) and MOSI (GPIO4) to the SPI1 controller.  CS
   * (GPIO3) and D/C (GPIO5) stay plain GPIO outputs.
   */

  bk7258_gpio_periph(BOARD_LCD_SCL_PIN, BK7258_SPI1_FUNC_SCK);
  bk7258_gpio_periph(BOARD_LCD_SDA_PIN, BK7258_SPI1_FUNC_MOSI);
  bk7258_gpio_config_output(BOARD_LCD_CS_PIN);
  bk7258_gpio_config_output(BOARD_LCD_DC_PIN);

  /* Idle levels: CS de-asserted high, D/C low until a transfer begins. */

  bk7258_gpio_write(BOARD_LCD_CS_PIN, true);
  bk7258_gpio_write(BOARD_LCD_DC_PIN, false);

  /* Bring the controller out of reset.  soft_reset is a low-active reset
   * bit: the ARMINO spi_ll_init() sets it to 1 and keeps it there (the
   * controller is out of reset while it is 1).  The earlier draft cleared
   * it back to 0, which held the shift engine in reset: registers stayed
   * writable through AHB but SCK never ran and tx_finish_int never fired.
   */

  hw->global_ctrl.soft_reset = 1;

  hw->ctrl.v = 0;
  hw->cfg.v  = 0;
  hw->int_status.v = 0xffff;  /* clear sticky status */

  /* Mirror the ARMINO spi_hal_configure() interrupt enables. */

  hw->ctrl.tx_udf_int_en = 1;
  hw->ctrl.rx_ovf_int_en = 1;
  hw->ctrl.tx_fifo_int_en = 0;
  hw->ctrl.rx_fifo_int_en = 1;

  hw->ctrl.master_en    = 1;  /* master role */
  hw->ctrl.bit_width    = 0;  /* 8-bit */
  hw->ctrl.lsb_first_en = 0;  /* MSB first */
  hw->ctrl.wire3_en     = 0;  /* 4-wire (MOSI/MISO) */
  hw->ctrl.cpol         = 0;  /* mode 0 */
  hw->ctrl.cpha         = 0;
  hw->ctrl.enable       = 1;

  hw->cfg.tx_finish_int_en = 1;
  hw->cfg.rx_finish_int_en = 1;

  hw->int_status.tx_fifo_int = 1;        /* write-1-clear */
  hw->int_status.rx_fifo_int = 1;        /* write-1-clear */

  priv->hw       = hw;
  priv->frequency = 0;
  priv->actual    = 0;
  priv->mode      = SPIDEV_MODE0;
  priv->nbits     = 8;
  priv->dev.ops   = &g_bk7258_spiops;
  nxmutex_init(&priv->lock);

  return &priv->dev;
}

#endif /* CONFIG_BK7258_LCD1_SPI_HW */
