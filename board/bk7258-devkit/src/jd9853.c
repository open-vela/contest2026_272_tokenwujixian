/****************************************************************************
 * board/bk7258-devkit/src/jd9853.c
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

#include <sys/types.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <debug.h>

#include <nuttx/arch.h>
#include <nuttx/spi/spi.h>
#include <nuttx/lcd/lcd.h>

#include "jd9853.h"

#ifdef CONFIG_LCD_JD9853

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Verify that all configuration requirements have been met */

#ifndef CONFIG_LCD_JD9853_SPIMODE
#  define CONFIG_LCD_JD9853_SPIMODE SPIDEV_MODE0
#endif

/* SPI frequency */

#ifndef CONFIG_LCD_JD9853_FREQUENCY
#  define CONFIG_LCD_JD9853_FREQUENCY 1000000
#endif

/* Check contrast selection */

#if !defined(CONFIG_LCD_MAXCONTRAST)
#  define CONFIG_LCD_MAXCONTRAST 1
#endif

/* Check power setting */

#if !defined(CONFIG_LCD_MAXPOWER) || CONFIG_LCD_MAXPOWER < 1
#  define CONFIG_LCD_MAXPOWER 1
#endif

#if CONFIG_LCD_MAXPOWER > 255
#  error "CONFIG_LCD_MAXPOWER must be less than 256 to fit in uint8_t"
#endif

/* Check orientation */

#if defined(CONFIG_LCD_PORTRAIT)
#  if defined(CONFIG_LCD_LANDSCAPE) || defined(CONFIG_LCD_RLANDSCAPE) ||\
      defined(CONFIG_LCD_RPORTRAIT)
#    error "Cannot define both portrait and any other orientations"
#  endif
#elif defined(CONFIG_LCD_RPORTRAIT)
#  if defined(CONFIG_LCD_LANDSCAPE) || defined(CONFIG_LCD_RLANDSCAPE)
#    error "Cannot define both rportrait and any other orientations"
#  endif
#elif defined(CONFIG_LCD_LANDSCAPE)
#  ifdef CONFIG_LCD_RLANDSCAPE
#    error "Cannot define both landscape and any other orientations"
#  endif
#elif !defined(CONFIG_LCD_RLANDSCAPE)
#  define CONFIG_LCD_LANDSCAPE 1
#endif

/* Display Resolution.  The JD9853 has a 240x320 RAM window; the actual
 * panel uses a 240x296 (T201BM-C12-03) visible area by default.
 */

#if !defined(CONFIG_LCD_JD9853_XRES)
#  define CONFIG_LCD_JD9853_XRES 240
#endif

#if !defined(CONFIG_LCD_JD9853_YRES)
#  define CONFIG_LCD_JD9853_YRES 296
#endif

#if !defined(CONFIG_LCD_JD9853_BPP)
#  define CONFIG_LCD_JD9853_BPP 16
#endif

#if !defined(CONFIG_LCD_JD9853_XOFFSET)
#  define CONFIG_LCD_JD9853_XOFFSET 0
#endif

#if !defined(CONFIG_LCD_JD9853_YOFFSET)
#  define CONFIG_LCD_JD9853_YOFFSET 0
#endif

#define JD9853_LUT_SIZE    CONFIG_LCD_JD9853_YRES

/* Scratch buffer used to byte-swap RGB565 pixel data (little-endian in
 * memory, MSB-first on the wire) before a block SPI transfer.  Chunking a
 * large putarea() through this buffer lets the SPI lower-half stream the
 * whole RAMWR burst as a handful of SNDBLOCK calls instead of two
 * per-pixel byte transfers.
 */

#define JD9853_SWAP_BUFSIZE  2048

#if defined(CONFIG_LCD_LANDSCAPE) || defined(CONFIG_LCD_RLANDSCAPE)
#  define JD9853_XRES       CONFIG_LCD_JD9853_YRES
#  define JD9853_YRES       CONFIG_LCD_JD9853_XRES
#  define JD9853_XOFFSET    CONFIG_LCD_JD9853_YOFFSET
#  define JD9853_YOFFSET    CONFIG_LCD_JD9853_XOFFSET
#else
#  define JD9853_XRES       CONFIG_LCD_JD9853_XRES
#  define JD9853_YRES       CONFIG_LCD_JD9853_YRES
#  define JD9853_XOFFSET    CONFIG_LCD_JD9853_XOFFSET
#  define JD9853_YOFFSET    CONFIG_LCD_JD9853_YOFFSET
#endif

/* Color depth and format */

#ifdef CONFIG_LCD_JD9853_BPP
#  if (CONFIG_LCD_JD9853_BPP == 16)
#    define JD9853_BPP           16
#    define JD9853_COLORFMT      FB_FMT_RGB16_565
#    define JD9853_BYTESPP       2
#  else
#    define JD9853_BPP           16
#    define JD9853_COLORFMT      FB_FMT_RGB16_565
#    define JD9853_BYTESPP       2
#    warning "Invalid color depth.  Falling back to 16bpp"
#  endif
#endif

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* This structure describes the state of this driver */

struct jd9853_dev_s
{
  /* Publicly visible device structure */

  struct lcd_dev_s dev;

  /* Private LCD-specific information follows */

  FAR struct spi_dev_s *spi;  /* SPI device */
  uint8_t bpp;                /* Selected color depth */
  uint8_t power;              /* Current power setting */

  /* This is working memory allocated by the LCD driver for each LCD device
   * and for each color plane. This memory will hold one raster line of data.
   * The size of the allocated run buffer must therefore be at least
   * (bpp * xres / 8).  Actual alignment of the buffer must conform to the
   * bitwidth of the underlying pixel type.
   *
   * If there are multiple planes, they may share the same working buffer
   * because different planes will not be operate on concurrently.  However,
   * if there are multiple LCD devices, they must each have unique run
   * buffers.
   */

  uint16_t runbuffer[JD9853_LUT_SIZE];
};

/* Working buffer for byte-swapped pixel data.  Only one panel transfer is
 * in flight at a time (the SPI bus is locked and the LCD flush path is
 * single-threaded), so a shared buffer is safe.
 */

static uint8_t g_jd9853_swap[JD9853_SWAP_BUFSIZE];

/****************************************************************************
 * Private Function Protototypes
 ****************************************************************************/

/* Misc. Helpers */

static void jd9853_select(FAR struct spi_dev_s *spi, int bits);
static void jd9853_deselect(FAR struct spi_dev_s *spi);

static inline void jd9853_sendcmd(FAR struct jd9853_dev_s *dev, uint8_t cmd);
static void jd9853_cmddata(FAR struct jd9853_dev_s *dev, uint8_t cmd,
                               const uint8_t *data, int len);
static void jd9853_init(FAR struct jd9853_dev_s *dev);
static void jd9853_sleep(FAR struct jd9853_dev_s *dev, bool sleep);
static void jd9853_setorientation(FAR struct jd9853_dev_s *dev);
static void jd9853_display(FAR struct jd9853_dev_s *dev, bool on);
static void jd9853_setcursor(FAR struct jd9853_dev_s *dev,
                           uint16_t x0, uint16_t y0,
                           uint16_t x1, uint16_t y1);
static void jd9853_bpp(FAR struct jd9853_dev_s *dev, int bpp);
static void jd9853_wrram(FAR struct jd9853_dev_s *dev,
                         FAR const uint8_t *buff, size_t size , size_t skip,
                         size_t count);
#ifndef CONFIG_LCD_NOGETRUN
static void jd9853_rdram(FAR struct jd9853_dev_s *dev,
                         FAR uint16_t *buff, size_t size);
#endif
static void jd9853_fill(FAR struct jd9853_dev_s *dev, uint16_t color);
static void jd9853_wrswap(FAR struct spi_dev_s *spi,
                          FAR const uint8_t *src, size_t nbytes);

/* LCD Data Transfer Methods */

static int jd9853_putrun(FAR struct lcd_dev_s *dev,
                         fb_coord_t row, fb_coord_t col,
                         FAR const uint8_t *buffer, size_t npixels);
static int jd9853_putarea(FAR struct lcd_dev_s *dev,
                          fb_coord_t row_start, fb_coord_t row_end,
                          fb_coord_t col_start, fb_coord_t col_end,
                          FAR const uint8_t *buffer, fb_coord_t stride);
#ifndef CONFIG_LCD_NOGETRUN
static int jd9853_getrun(FAR struct lcd_dev_s *dev,
                         fb_coord_t row, fb_coord_t col,
                         FAR uint8_t *buffer, size_t npixels);
#endif

/* LCD Configuration */

static int jd9853_getvideoinfo(FAR struct lcd_dev_s *dev,
                               FAR struct fb_videoinfo_s *vinfo);
static int jd9853_getplaneinfo(FAR struct lcd_dev_s *dev,
                               unsigned int planeno,
                               FAR struct lcd_planeinfo_s *pinfo);

/* LCD Specific Controls */

static int jd9853_getpower(FAR struct lcd_dev_s *dev);
static int jd9853_setpower(FAR struct lcd_dev_s *dev, int power);
static int jd9853_getcontrast(FAR struct lcd_dev_s *dev);
static int jd9853_setcontrast(FAR struct lcd_dev_s *dev,
                              unsigned int contrast);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct jd9853_dev_s g_lcddev;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: jd9853_select
 *
 * Description:
 *   Select the SPI, locking and re-configuring if necessary
 *
 ****************************************************************************/

static void jd9853_select(FAR struct spi_dev_s *spi, int bits)
{
  /* Select JD9853 chip (locking the SPI bus in case there are multiple
   * devices competing for the SPI bus
   */

  SPI_LOCK(spi, true);
  SPI_SELECT(spi, SPIDEV_DISPLAY(0), true);

  /* Now make sure that the SPI bus is configured for the JD9853 (it
   * might have gotten configured for a different device while unlocked)
   */

  SPI_SETMODE(spi, CONFIG_LCD_JD9853_SPIMODE);
  SPI_SETBITS(spi, bits);
  SPI_SETFREQUENCY(spi, CONFIG_LCD_JD9853_FREQUENCY);
}

/****************************************************************************
 * Name: jd9853_deselect
 *
 * Description:
 *   De-select the SPI
 *
 ****************************************************************************/

static void jd9853_deselect(FAR struct spi_dev_s *spi)
{
  /* De-select JD9853 chip and relinquish the SPI bus. */

  SPI_SELECT(spi, SPIDEV_DISPLAY(0), false);
  SPI_LOCK(spi, false);
}

/****************************************************************************
 * Name: jd9853_sendcmd
 *
 * Description:
 *   Send a command to the driver.
 *
 ****************************************************************************/

static inline void jd9853_sendcmd(FAR struct jd9853_dev_s *dev, uint8_t cmd)
{
  jd9853_select(dev->spi, 8);
  SPI_CMDDATA(dev->spi, SPIDEV_DISPLAY(0), true);
  SPI_SEND(dev->spi, cmd);
  SPI_CMDDATA(dev->spi, SPIDEV_DISPLAY(0), false);
  jd9853_deselect(dev->spi);
}

/****************************************************************************
 * Name: jd9853_cmddata
 *
 * Description:
 *   Send a command and a series of data to the driver.
 *
 ****************************************************************************/

static void jd9853_cmddata(FAR struct jd9853_dev_s *dev, uint8_t cmd,
                                      const uint8_t *data, int len)
{
  jd9853_select(dev->spi, 8);
  SPI_CMDDATA(dev->spi, SPIDEV_DISPLAY(0), true);
  SPI_SEND(dev->spi, cmd);
  SPI_CMDDATA(dev->spi, SPIDEV_DISPLAY(0), false);
  SPI_SNDBLOCK(dev->spi, data, len);
  jd9853_deselect(dev->spi);
}

/****************************************************************************
 * Name: jd9853_init
 *
 * Description:
 *   Send jd9853 internal init commands.
 *
 * Assumption/Limitations:
 *   Initialization is hardware-specific and may need to be rewritten for
 *   different modules.
 *
 ****************************************************************************/

static void jd9853_init(FAR struct jd9853_dev_s *dev)
{
  /* Unlock chip registers */

  jd9853_cmddata(dev, JD9853_PASSWORD, (const uint8_t *)"\x98\x53", 2);

  /* Power settings (reference sequence for the 2.01" TFT=201 panel) */

  jd9853_cmddata(dev, JD9853_SET_DCDC, (const uint8_t *)"\x23", 1);
  jd9853_cmddata(dev, JD9853_GAMMA_SET_CMD, (const uint8_t *)
                 "\x00\x47\x00\x6F", 4);
  jd9853_cmddata(dev, JD9853_POWER_CTRL, (const uint8_t *)
                 "\x1C\x1A\x55\x73\x63\xF0", 6);

  /* Timing configuration */

  jd9853_cmddata(dev, JD9853_SETSTBA, (const uint8_t *)"\x44\x44", 2);
  jd9853_cmddata(dev, JD9853_SETPANEL, (const uint8_t *)"\x12", 1);
  jd9853_cmddata(dev, JD9853_SETRGBCYC, (const uint8_t *)
                 "\x7D\x07\x14\x06\xCF\x71\x72\x77", 8);
  jd9853_cmddata(dev, JD9853_SETTCON, (const uint8_t *)
                 "\x00\x00\xA0\x79\x0B\x0A\x16\x79\x0B\x0A\x16\x82", 12);

  /* Gamma correction (reference G2.2 table for the 2.01" panel) */

  jd9853_cmddata(dev, JD9853_R_GAMMA_SET_CMD, (const uint8_t *)
                 "\x3F\x32\x29\x29\x27\x2B\x27\x28\x28\x26\x25\x17\x12\x0d\
                  \x04\x00\x3F\x32\x29\x29\x27\x2B\x27\x28\x28\x26\x25\x17\
                  \x12\x0d\x04\x00", 32);

  /* Power management */

  jd9853_cmddata(dev, JD9853_SETGD, (const uint8_t *)
                 "\x04\x06\x6B\x0F\x00", 5);
  jd9853_cmddata(dev, JD9853_RAM_CTRL, (const uint8_t *)"\x00\x30", 2);
  jd9853_cmddata(dev, JD9853_SETOSCM, (const uint8_t *)"\x14", 1);

  /* Page 1 settings */

  jd9853_cmddata(dev, JD9853_SET_PAGE_CMD, (const uint8_t *)"\x01", 1);
  jd9853_cmddata(dev, JD9853_SETPANEL_1, (const uint8_t *)
                 "\x03\x13\xEF\x35\x35", 5);
  jd9853_cmddata(dev, JD9853_SETTCON_1, (const uint8_t *)
                 "\x14\x15\xC0", 3);
  jd9853_cmddata(dev, JD9853_SETTCON_2, (const uint8_t *)
                 "\x06\x3A", 2);
  jd9853_cmddata(dev, JD9853_SETTCON_3, (const uint8_t *)"\x72\x12", 2);
  jd9853_cmddata(dev, JD9853_SETDISP_1, (const uint8_t *)"\x00", 1);

  /* Page 2: interface settings */

  jd9853_cmddata(dev, JD9853_SET_PAGE_CMD, (const uint8_t *)"\x02", 1);
  jd9853_cmddata(dev, 0xE5, (const uint8_t *)"\x00\x02\x00", 3);
  jd9853_cmddata(dev, 0xE5, (const uint8_t *)"\x01\x02\x00", 3);

  /* Back to page 0 */

  jd9853_cmddata(dev, JD9853_SET_PAGE_CMD, (const uint8_t *)"\x00", 1);

  jd9853_cmddata(dev, JD9853_TEON, (const uint8_t *)"\x00", 1);

  jd9853_sendcmd(dev, JD9853_SLEEP_OUT);
  up_mdelay(120);

  /* Page 2 again after sleep out, then back to page 0 */

  jd9853_cmddata(dev, JD9853_SET_PAGE_CMD, (const uint8_t *)"\x02", 1);
  jd9853_cmddata(dev, 0xE5, (const uint8_t *)"\x00\x02\x00", 3);
  jd9853_cmddata(dev, JD9853_SET_PAGE_CMD, (const uint8_t *)"\x00", 1);
}

/****************************************************************************
 * Name: jd9853_sleep
 *
 * Description:
 *   Sleep or wake up the driver.
 *
 ****************************************************************************/

static void jd9853_sleep(FAR struct jd9853_dev_s *dev, bool sleep)
{
  if (sleep)
    {
      jd9853_sendcmd(dev, JD9853_SLEEP_IN);
    }
  else
    {
      jd9853_sendcmd(dev, JD9853_SLEEP_OUT);
    }

  up_mdelay(120);
}

/****************************************************************************
 * Name: jd9853_display
 *
 * Description:
 *   Turn on or off the display.
 *
 ****************************************************************************/

static void jd9853_display(FAR struct jd9853_dev_s *dev, bool on)
{
#if defined(CONFIG_LCD_JD9853_TE)
  uint8_t reg;

  reg = 0x00;
  jd9853_cmddata(dev, JD9853_TEON, &reg, 1);
#endif

#if defined(CONFIG_LCD_JD9853_INVERT)
  jd9853_sendcmd(dev, JD9853_INVERSION_ON);
#endif

  if (on)
    {
      jd9853_sendcmd(dev, JD9853_DISON);
    }
  else
    {
      jd9853_sendcmd(dev, JD9853_DISOFF);
    }
}

/****************************************************************************
 * Name: jd9853_setorientation
 *
 * Description:
 *   Set screen orientation.
 *
 ****************************************************************************/

static void jd9853_setorientation(FAR struct jd9853_dev_s *dev)
{
  uint8_t reg = 0x00;

#if !defined(CONFIG_LCD_PORTRAIT) || defined(CONFIG_LCD_JD9853_BGR)

#  if defined(CONFIG_LCD_RLANDSCAPE)

  reg = JD9853_MADCTL_MY | JD9853_MADCTL_MV;

#  elif defined(CONFIG_LCD_LANDSCAPE)

  reg = JD9853_MADCTL_MV | JD9853_MADCTL_MX;

#  elif defined(CONFIG_LCD_RPORTRAIT)

  reg = JD9853_MADCTL_MY | JD9853_MADCTL_MX;

#  endif

#  if defined(CONFIG_LCD_JD9853_BGR)

  reg |= JD9853_MADCTL_BGR;

#  endif

#endif

  jd9853_cmddata(dev, JD9853_MADCTL, &reg, 1);
}

/****************************************************************************
 * Name: jd9853_setcursor
 *
 * Description:
 *   Set the rectangular area for an upcoming read or write from RAM.
 *
 ****************************************************************************/

static void jd9853_setcursor(FAR struct jd9853_dev_s *dev,
                           uint16_t x0, uint16_t y0,
                           uint16_t x1, uint16_t y1)
{
  /* Set page address */

  jd9853_sendcmd(dev, JD9853_PASET);
  jd9853_select(dev->spi, 8);
  SPI_SEND(dev->spi, (y0 + JD9853_YOFFSET) >> 8);
  SPI_SEND(dev->spi, (y0 + JD9853_YOFFSET) & 0xff);
  SPI_SEND(dev->spi, (y1 + JD9853_YOFFSET) >> 8);
  SPI_SEND(dev->spi, (y1 + JD9853_YOFFSET) & 0xff);
  jd9853_deselect(dev->spi);

  /* Set column address */

  jd9853_sendcmd(dev, JD9853_CASET);
  jd9853_select(dev->spi, 8);
  SPI_SEND(dev->spi, (x0 + JD9853_XOFFSET) >> 8);
  SPI_SEND(dev->spi, (x0 + JD9853_XOFFSET) & 0xff);
  SPI_SEND(dev->spi, (x1 + JD9853_XOFFSET) >> 8);
  SPI_SEND(dev->spi, (x1 + JD9853_XOFFSET) & 0xff);
  jd9853_deselect(dev->spi);
}

/****************************************************************************
 * Name: jd9853_bpp
 *
 * Description:
 *   Set the color depth of the device.
 *
 ****************************************************************************/

static void jd9853_bpp(FAR struct jd9853_dev_s *dev, int bpp)
{
  uint8_t depth;

  /* Don't send any command if the depth hasn't changed. */

  if (dev->bpp != bpp)
    {
      /* The JD9853 uses a vendor-specific pixel format encoding, not the
       * standard MIPI DCS value: 0x05 = RGB565, 0x06 = RGB666 (see the
       * reference TFT=201 init sequence).
       */

      depth = (bpp == 16) ? 0x05 : (bpp == 18) ? 0x06 : 0x00;
      jd9853_cmddata(dev, JD9853_COLMOD, &depth, 1);

      /* Cache the new BPP */

      dev->bpp = bpp;
    }
}

/****************************************************************************
 * Name: jd9853_wrswap
 *
 * Description:
 *   Byte-swap a run of RGB565 pixels (low byte first in memory, MSB-first
 *   on the wire) into the scratch buffer and transmit it as SNDBLOCK
 *   chunks so the SPI lower-half streams the whole run in one CS-low
 *   window.
 *
 ****************************************************************************/

static void jd9853_wrswap(FAR struct spi_dev_s *spi,
                          FAR const uint8_t *src, size_t nbytes)
{
  while (nbytes > 0)
    {
      size_t chunk = nbytes > JD9853_SWAP_BUFSIZE ?
                     JD9853_SWAP_BUFSIZE : nbytes;
      size_t j;

      for (j = 0; j + 1 < chunk; j += 2)
        {
          g_jd9853_swap[j]     = src[j + 1];
          g_jd9853_swap[j + 1] = src[j];
        }

      SPI_SNDBLOCK(spi, g_jd9853_swap, chunk);
      src    += chunk;
      nbytes -= chunk;
    }
}

/****************************************************************************
 * Name: jd9853_wrram
 *
 * Description:
 *   Write to the driver's RAM. It is possible to write multiples of size
 *   while skipping some values.
 *
 ****************************************************************************/

static void jd9853_wrram(FAR struct jd9853_dev_s *dev,
                         FAR const uint8_t *buff, size_t size, size_t skip,
                         size_t count)
{
  size_t i;

  jd9853_sendcmd(dev, JD9853_RAMWR);

  jd9853_select(dev->spi, 8);

  /* RGB565 pixels are stored little-endian in memory (low byte first) but
   * the panel expects the most-significant byte first.  size/skip are
   * always even for 16bpp, which keeps the swap chunk alignment across
   * row boundaries.
   */

  for (i = 0; i < count; i++)
    {
      jd9853_wrswap(dev->spi, buff + (i * (size + skip)), size);
    }

  jd9853_deselect(dev->spi);
}

/****************************************************************************
 * Name: jd9853_rdram
 *
 * Description:
 *   Read from the driver's RAM.
 *
 ****************************************************************************/

#ifndef CONFIG_LCD_NOGETRUN
static void jd9853_rdram(FAR struct jd9853_dev_s *dev,
                         FAR uint16_t *buff, size_t size)
{
  jd9853_sendcmd(dev, JD9853_RAMRD);

  /* NOTE: The DevKit display connector carries no MISO line, so reads
   * return zeroes.  Eight-bit transfers keep the interface compatible
   * with both SPI controllers and the GPIO bit-bang lower-half.  Byte
   * pairs are swapped back to native little-endian after the receive.
   */

  jd9853_select(dev->spi, 8);
  SPI_RECVBLOCK(dev->spi, buff, size * 2);
  jd9853_deselect(dev->spi);

  for (size_t i = 0; i + 1 < size * 2; i += 2)
    {
      FAR uint8_t *p = (FAR uint8_t *)buff;
      uint8_t tmp = p[i];

      p[i]     = p[i + 1];
      p[i + 1] = tmp;
    }
}
#endif

/****************************************************************************
 * Name: jd9853_fill
 *
 * Description:
 *   Fill the display with the specified color.
 *
 ****************************************************************************/

static void jd9853_fill(FAR struct jd9853_dev_s *dev, uint16_t color)
{
  int i;

  jd9853_setcursor(dev, 0, 0, JD9853_XRES - 1, JD9853_YRES - 1);

  jd9853_sendcmd(dev, JD9853_RAMWR);
  jd9853_select(dev->spi, 8);

  /* RGB565 pixels are transmitted most-significant byte first.  Build one
   * byte-swapped row in the scratch buffer and stream it once per row.
   */

  DEBUGASSERT(JD9853_XRES * 2 <= JD9853_SWAP_BUFSIZE);

  for (i = 0; i < JD9853_XRES; i++)
    {
      g_jd9853_swap[2 * i]     = color & 0xff;
      g_jd9853_swap[2 * i + 1] = color >> 8;
    }

  for (i = 0; i < JD9853_YRES; i++)
    {
      SPI_SNDBLOCK(dev->spi, g_jd9853_swap, JD9853_XRES * 2);
    }

  jd9853_deselect(dev->spi);
}

/****************************************************************************
 * Name:  jd9853_putrun
 *
 * Description:
 *   This method can be used to write a partial raster line to the LCD.
 *
 ****************************************************************************/

static int jd9853_putrun(FAR struct lcd_dev_s *dev,
                         fb_coord_t row, fb_coord_t col,
                         FAR const uint8_t *buffer, size_t npixels)
{
  FAR struct jd9853_dev_s *priv = (FAR struct jd9853_dev_s *)dev;

  ginfo("row: %d col: %d npixels: %d\n", row, col, npixels);
  DEBUGASSERT(buffer && ((uintptr_t)buffer & 1) == 0);

  jd9853_setcursor(priv, col, row, col + npixels - 1, row);
  jd9853_wrram(priv, buffer, npixels * (priv->bpp >> 3), 0, 1);

  return OK;
}

/****************************************************************************
 * Name:  jd9853_putarea
 *
 * Description:
 *   This method can be used to write a partial area to the LCD.
 *
 ****************************************************************************/

static int jd9853_putarea(FAR struct lcd_dev_s *dev,
                          fb_coord_t row_start, fb_coord_t row_end,
                          fb_coord_t col_start, fb_coord_t col_end,
                          FAR const uint8_t *buffer, fb_coord_t stride)
{
  FAR struct jd9853_dev_s *priv = (FAR struct jd9853_dev_s *)dev;
  size_t cols = col_end - col_start + 1;
  size_t rows = row_end - row_start + 1;
  size_t row_size = cols * (priv->bpp >> 3);

  ginfo("row_start: %d row_end: %d col_start: %d col_end: %d\n",
         row_start, row_end, col_start, col_end);

  DEBUGASSERT(buffer && ((uintptr_t)buffer & 1) == 0);

  jd9853_setcursor(priv, col_start, row_start, col_end, row_end);

  /* If the stride is the same of the row, a single SPI transfer is enough.
   * That is always true for lcddev. For framebuffer, that indicates a full
   * screen or full row update.
   */

  if (stride == row_size)
    {
      /* simpler case, we can just send the whole buffer */

      ginfo("Using full screen/full row mode\n");
      jd9853_wrram(priv, buffer, rows * row_size, 0, 1);
    }
  else
    {
      /* We have to go row by row */

      ginfo("Falling-back to row by row mode\n");
      jd9853_wrram(priv, buffer, row_size, stride - row_size, rows);
    }

  return OK;
}

/****************************************************************************
 * Name:  jd9853_getrun
 *
 * Description:
 *   This method can be used to read a partial raster line from the LCD.
 *
 ****************************************************************************/

#ifndef CONFIG_LCD_NOGETRUN
static int jd9853_getrun(FAR struct lcd_dev_s *dev,
                         fb_coord_t row, fb_coord_t col,
                         FAR uint8_t *buffer, size_t npixels)
{
  FAR struct jd9853_dev_s *priv = (FAR struct jd9853_dev_s *)dev;
  FAR uint16_t *dest = (FAR uint16_t *)buffer;

  ginfo("row: %d col: %d npixels: %d\n", row, col, npixels);
  DEBUGASSERT(buffer && ((uintptr_t)buffer & 1) == 0);

  jd9853_setcursor(priv, col, row, col + npixels - 1, row);
  jd9853_rdram(priv, dest, npixels);

  return OK;
}
#endif

/****************************************************************************
 * Name:  jd9853_getvideoinfo
 *
 * Description:
 *   Get information about the LCD video controller configuration.
 *
 ****************************************************************************/

static int jd9853_getvideoinfo(FAR struct lcd_dev_s *dev,
                               FAR struct fb_videoinfo_s *vinfo)
{
  DEBUGASSERT(dev && vinfo);
  lcdinfo("fmt: %d xres: %d yres: %d nplanes: 1\n",
          JD9853_COLORFMT, JD9853_XRES, JD9853_YRES);

  vinfo->fmt     = JD9853_COLORFMT;    /* Color format: RGB16-565 */
  vinfo->xres    = JD9853_XRES;        /* Horizontal resolution in pixels */
  vinfo->yres    = JD9853_YRES;        /* Vertical resolution in pixels */
  vinfo->nplanes = 1;                  /* Number of color planes supported */
  return OK;
}

/****************************************************************************
 * Name:  jd9853_getplaneinfo
 *
 * Description:
 *   Get information about the configuration of each LCD color plane.
 *
 ****************************************************************************/

static int jd9853_getplaneinfo(FAR struct lcd_dev_s *dev,
                               unsigned int planeno,
                               FAR struct lcd_planeinfo_s *pinfo)
{
  FAR struct jd9853_dev_s *priv = (FAR struct jd9853_dev_s *)dev;

  DEBUGASSERT(dev && pinfo && planeno == 0);
  lcdinfo("planeno: %d bpp: %d\n", planeno, JD9853_BPP);

  pinfo->putrun = jd9853_putrun;                  /* Put a run into LCD */
  pinfo->putarea = jd9853_putarea;                /* Put an area into LCD */
#ifndef CONFIG_LCD_NOGETRUN
  pinfo->getrun = jd9853_getrun;                  /* Get a run from LCD */
#endif
  pinfo->buffer = (FAR uint8_t *)priv->runbuffer; /* Run scratch buffer */
  pinfo->bpp    = priv->bpp;                      /* Bits-per-pixel */
  pinfo->dev    = dev;                            /* The lcd device */
  return OK;
}

/****************************************************************************
 * Name:  jd9853_getpower
 ****************************************************************************/

static int jd9853_getpower(FAR struct lcd_dev_s *dev)
{
  FAR struct jd9853_dev_s *priv = (FAR struct jd9853_dev_s *)dev;

  lcdinfo("power: %d\n", priv->power);
  return priv->power;
}

/****************************************************************************
 * Name:  jd9853_setpower
 ****************************************************************************/

static int jd9853_setpower(FAR struct lcd_dev_s *dev, int power)
{
  FAR struct jd9853_dev_s *priv = (FAR struct jd9853_dev_s *)dev;

  lcdinfo("power: %d\n", power);
  DEBUGASSERT((unsigned)power <= CONFIG_LCD_MAXPOWER);

  /* Set new power level */

  if (power > 0)
    {
      /* Turn on the display */

      jd9853_display(priv, true);

      /* Save the power */

      priv->power = power;
    }
  else
    {
      /* Turn off the display */

      jd9853_display(priv, false);

      /* Save the power */

      priv->power = 0;
    }

  return OK;
}

/****************************************************************************
 * Name:  jd9853_getcontrast
 *
 * Description:
 *   Get the current contrast setting (0-CONFIG_LCD_MAXCONTRAST).
 *
 ****************************************************************************/

static int jd9853_getcontrast(FAR struct lcd_dev_s *dev)
{
  lcdinfo("Not implemented\n");
  return -ENOSYS;
}

/****************************************************************************
 * Name:  jd9853_setcontrast
 *
 * Description:
 *   Set LCD panel contrast (0-CONFIG_LCD_MAXCONTRAST).
 *
 ****************************************************************************/

static int jd9853_setcontrast(FAR struct lcd_dev_s *dev,
                              unsigned int contrast)
{
  lcdinfo("contrast: %d\n", contrast);
  return -ENOSYS;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name:  jd9853_initialize
 *
 * Description:
 *   Initialize the JD9853 video hardware.  The initial state of the
 *   LCD is fully initialized, display memory cleared, and the LCD ready
 *   to use, but with the power setting at 0 (full off == sleep mode).
 *
 * Returned Value:
 *
 *   On success, this function returns a reference to the LCD object for
 *   the specified LCD.  NULL is returned on any failure.
 *
 ****************************************************************************/

FAR struct lcd_dev_s *jd9853_lcdinitialize(FAR struct spi_dev_s *spi)
{
  FAR struct jd9853_dev_s *priv = &g_lcddev;

  /* Initialize the driver data structure */

  priv->dev.getvideoinfo = jd9853_getvideoinfo;
  priv->dev.getplaneinfo = jd9853_getplaneinfo;
  priv->dev.getpower     = jd9853_getpower;
  priv->dev.setpower     = jd9853_setpower;
  priv->dev.getcontrast  = jd9853_getcontrast;
  priv->dev.setcontrast  = jd9853_setcontrast;
  priv->spi              = spi;

  /* Init the hardware and clear the display */

  jd9853_init(priv);
  jd9853_display(priv, false);
  jd9853_sleep(priv, false);
  jd9853_bpp(priv, JD9853_BPP);
  jd9853_setorientation(priv);
  jd9853_fill(priv, 0x0000);
  jd9853_display(priv, true);

  return &priv->dev;
}

#endif /* CONFIG_LCD_JD9853 */
