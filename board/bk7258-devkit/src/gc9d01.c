/****************************************************************************
 * board/bk7258-devkit/src/gc9d01.c
 *
 * GC9D01 SPI TFT LCD lower-half driver for the DevKit's two round 0.71"
 * 160x160 panels (NT071BB-C12-02).  The initialization sequence is the
 * reference sequence used by the Armino SDK (lcd_spi_gc9d01.c).
 *
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

#include "gc9d01.h"

#ifdef CONFIG_LCD_GC9D01

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifndef CONFIG_LCD_GC9D01_SPIMODE
#  define CONFIG_LCD_GC9D01_SPIMODE SPIDEV_MODE0
#endif

#ifndef CONFIG_LCD_GC9D01_FREQUENCY
#  define CONFIG_LCD_GC9D01_FREQUENCY 1000000
#endif

#if !defined(CONFIG_LCD_MAXCONTRAST)
#  define CONFIG_LCD_MAXCONTRAST 1
#endif

#if !defined(CONFIG_LCD_MAXPOWER) || CONFIG_LCD_MAXPOWER < 1
#  define CONFIG_LCD_MAXPOWER 1
#endif

#if CONFIG_LCD_MAXPOWER > 255
#  error "CONFIG_LCD_MAXPOWER must be less than 256 to fit in uint8_t"
#endif

/* Display resolution.  The round panels are 160x160. */

#if !defined(CONFIG_LCD_GC9D01_XRES)
#  define CONFIG_LCD_GC9D01_XRES 160
#endif

#if !defined(CONFIG_LCD_GC9D01_YRES)
#  define CONFIG_LCD_GC9D01_YRES 160
#endif

#if !defined(CONFIG_LCD_GC9D01_BPP)
#  define CONFIG_LCD_GC9D01_BPP 16
#endif

#if !defined(CONFIG_LCD_GC9D01_XOFFSET)
#  define CONFIG_LCD_GC9D01_XOFFSET 0
#endif

#if !defined(CONFIG_LCD_GC9D01_YOFFSET)
#  define CONFIG_LCD_GC9D01_YOFFSET 0
#endif

#define GC9D01_LUT_SIZE    CONFIG_LCD_GC9D01_YRES

#define GC9D01_XRES        CONFIG_LCD_GC9D01_XRES
#define GC9D01_YRES        CONFIG_LCD_GC9D01_YRES
#define GC9D01_XOFFSET     CONFIG_LCD_GC9D01_XOFFSET
#define GC9D01_YOFFSET     CONFIG_LCD_GC9D01_YOFFSET

#define GC9D01_BPP           16
#define GC9D01_COLORFMT      FB_FMT_RGB16_565
#define GC9D01_BYTESPP       2

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct gc9d01_dev_s
{
  struct lcd_dev_s dev;

  FAR struct spi_dev_s *spi;  /* SPI device */
  uint8_t bpp;                /* Selected color depth */
  uint8_t power;              /* Current power setting */

  uint16_t runbuffer[GC9D01_LUT_SIZE];
};

/****************************************************************************
 * Private Function Protototypes
 ****************************************************************************/

static void gc9d01_select(FAR struct spi_dev_s *spi, int bits);
static void gc9d01_deselect(FAR struct spi_dev_s *spi);

static inline void gc9d01_sendcmd(FAR struct gc9d01_dev_s *dev, uint8_t cmd);
static void gc9d01_cmddata(FAR struct gc9d01_dev_s *dev, uint8_t cmd,
                           const uint8_t *data, int len);
static void gc9d01_init(FAR struct gc9d01_dev_s *dev);
static void gc9d01_sleep(FAR struct gc9d01_dev_s *dev, bool sleep);
static void gc9d01_setorientation(FAR struct gc9d01_dev_s *dev);
static void gc9d01_display(FAR struct gc9d01_dev_s *dev, bool on);
static void gc9d01_setcursor(FAR struct gc9d01_dev_s *dev,
                             uint16_t x0, uint16_t y0,
                             uint16_t x1, uint16_t y1);
static void gc9d01_bpp(FAR struct gc9d01_dev_s *dev, int bpp);
static void gc9d01_wrram(FAR struct gc9d01_dev_s *dev,
                         FAR const uint8_t *buff, size_t size, size_t skip,
                         size_t count);
#ifndef CONFIG_LCD_NOGETRUN
static void gc9d01_rdram(FAR struct gc9d01_dev_s *dev,
                         FAR uint16_t *buff, size_t size);
#endif
static void gc9d01_fill(FAR struct gc9d01_dev_s *dev, uint16_t color);

static int gc9d01_putrun(FAR struct lcd_dev_s *dev,
                         fb_coord_t row, fb_coord_t col,
                         FAR const uint8_t *buffer, size_t npixels);
static int gc9d01_putarea(FAR struct lcd_dev_s *dev,
                          fb_coord_t row_start, fb_coord_t row_end,
                          fb_coord_t col_start, fb_coord_t col_end,
                          FAR const uint8_t *buffer, fb_coord_t stride);
#ifndef CONFIG_LCD_NOGETRUN
static int gc9d01_getrun(FAR struct lcd_dev_s *dev,
                         fb_coord_t row, fb_coord_t col,
                         FAR uint8_t *buffer, size_t npixels);
#endif

static int gc9d01_getvideoinfo(FAR struct lcd_dev_s *dev,
                               FAR struct fb_videoinfo_s *vinfo);
static int gc9d01_getplaneinfo(FAR struct lcd_dev_s *dev,
                               unsigned int planeno,
                               FAR struct lcd_planeinfo_s *pinfo);

static int gc9d01_getpower(FAR struct lcd_dev_s *dev);
static int gc9d01_setpower(FAR struct lcd_dev_s *dev, int power);
static int gc9d01_getcontrast(FAR struct lcd_dev_s *dev);
static int gc9d01_setcontrast(FAR struct lcd_dev_s *dev,
                              unsigned int contrast);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct gc9d01_dev_s g_lcddev;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void gc9d01_select(FAR struct spi_dev_s *spi, int bits)
{
  SPI_LOCK(spi, true);
  SPI_SELECT(spi, SPIDEV_DISPLAY(0), true);
  SPI_SETMODE(spi, CONFIG_LCD_GC9D01_SPIMODE);
  SPI_SETBITS(spi, bits);
  SPI_SETFREQUENCY(spi, CONFIG_LCD_GC9D01_FREQUENCY);
}

static void gc9d01_deselect(FAR struct spi_dev_s *spi)
{
  SPI_SELECT(spi, SPIDEV_DISPLAY(0), false);
  SPI_LOCK(spi, false);
}

static inline void gc9d01_sendcmd(FAR struct gc9d01_dev_s *dev, uint8_t cmd)
{
  gc9d01_select(dev->spi, 8);
  SPI_CMDDATA(dev->spi, SPIDEV_DISPLAY(0), true);
  SPI_SEND(dev->spi, cmd);
  SPI_CMDDATA(dev->spi, SPIDEV_DISPLAY(0), false);
  gc9d01_deselect(dev->spi);
}

static void gc9d01_cmddata(FAR struct gc9d01_dev_s *dev, uint8_t cmd,
                           const uint8_t *data, int len)
{
  gc9d01_select(dev->spi, 8);
  SPI_CMDDATA(dev->spi, SPIDEV_DISPLAY(0), true);
  SPI_SEND(dev->spi, cmd);
  SPI_CMDDATA(dev->spi, SPIDEV_DISPLAY(0), false);
  SPI_SNDBLOCK(dev->spi, data, len);
  gc9d01_deselect(dev->spi);
}

static void gc9d01_init(FAR struct gc9d01_dev_s *dev)
{
  static const uint8_t gammaseq[32] =
  {
    0x03, 0x03, 0x01, 0x01, 0x00, 0x00, 0x0F, 0x0F,
    0x0D, 0x0D, 0x0B, 0x0B, 0x09, 0x09, 0x00, 0x00,
    0x00, 0x00, 0x0A, 0x0A, 0x0C, 0x0C, 0x0E, 0x0E,
    0x10, 0x10, 0x00, 0x00, 0x02, 0x02, 0x04, 0x04,
  };
  static const uint8_t regs_ff[16] =
  {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  };

  /* Page unlock */

  gc9d01_sendcmd(dev, 0xFE);
  gc9d01_sendcmd(dev, 0xEF);

  /* 0x80-0x8F all set to 0xFF */

  gc9d01_cmddata(dev, 0x80, regs_ff, 16);

  gc9d01_cmddata(dev, GC9D01_COLMOD, (const uint8_t *)"\x05", 1);
  gc9d01_cmddata(dev, 0xEC, (const uint8_t *)"\x01", 1);
  gc9d01_cmddata(dev, 0x74, (const uint8_t *)
                 "\x02\x0E\x00\x00\x00\x00\x00", 7);
  gc9d01_cmddata(dev, 0x98, (const uint8_t *)"\x3E\x99\x3E", 3);
  gc9d01_cmddata(dev, 0xB5, (const uint8_t *)"\x0D\x0D", 2);
  gc9d01_cmddata(dev, 0x60, (const uint8_t *)"\x38\x0F\x79\x67", 4);
  gc9d01_cmddata(dev, 0x61, (const uint8_t *)"\x38\x11\x79\x67", 4);
  gc9d01_cmddata(dev, 0x64, (const uint8_t *)
                 "\x38\x17\x71\x5F\x79\x67", 6);
  gc9d01_cmddata(dev, 0x65, (const uint8_t *)
                 "\x38\x13\x71\x5B\x79\x67", 6);
  gc9d01_cmddata(dev, 0x6A, (const uint8_t *)"\x00\x00", 2);
  gc9d01_cmddata(dev, 0x6C, (const uint8_t *)
                 "\x22\x02\x22\x02\x22\x22\x50", 7);
  gc9d01_cmddata(dev, 0x6E, gammaseq, 32);
  gc9d01_cmddata(dev, 0xBF, (const uint8_t *)"\x01", 1);
  gc9d01_cmddata(dev, 0xF9, (const uint8_t *)"\x40", 1);
  gc9d01_cmddata(dev, 0x9B, (const uint8_t *)"\x3B", 1);
  gc9d01_cmddata(dev, 0x93, (const uint8_t *)"\x33\x7F\x00", 3);
  gc9d01_cmddata(dev, 0x7E, (const uint8_t *)"\x30", 1);
  gc9d01_cmddata(dev, 0x70, (const uint8_t *)
                 "\x0D\x02\x08\x0D\x02\x08", 6);
  gc9d01_cmddata(dev, 0x71, (const uint8_t *)"\x0D\x02\x08", 3);
  gc9d01_cmddata(dev, 0x91, (const uint8_t *)"\x0E\x09", 2);
  gc9d01_cmddata(dev, 0xC3, (const uint8_t *)"\x18", 1);
  gc9d01_cmddata(dev, 0xC4, (const uint8_t *)"\x18", 1);
  gc9d01_cmddata(dev, 0xC9, (const uint8_t *)"\x3C", 1);
  gc9d01_cmddata(dev, 0xF0, (const uint8_t *)
                 "\x13\x15\x04\x05\x01\x38", 6);
  gc9d01_cmddata(dev, 0xF2, (const uint8_t *)
                 "\x13\x15\x04\x05\x01\x34", 6);
  gc9d01_cmddata(dev, 0xF1, (const uint8_t *)
                 "\x4B\xB8\x7B\x34\x35\xEF", 6);
  gc9d01_cmddata(dev, 0xF3, (const uint8_t *)
                 "\x47\xB4\x72\x34\x35\xDA", 6);

  gc9d01_cmddata(dev, GC9D01_MADCTL, (const uint8_t *)"\x00", 1);
  gc9d01_sendcmd(dev, GC9D01_TEOFF);

  gc9d01_sendcmd(dev, GC9D01_SLEEP_OUT);
  up_mdelay(120);

  gc9d01_sendcmd(dev, GC9D01_DISON);
}

static void gc9d01_sleep(FAR struct gc9d01_dev_s *dev, bool sleep)
{
  if (sleep)
    {
      gc9d01_sendcmd(dev, GC9D01_SLEEP_IN);
    }
  else
    {
      gc9d01_sendcmd(dev, GC9D01_SLEEP_OUT);
    }

  up_mdelay(120);
}

static void gc9d01_display(FAR struct gc9d01_dev_s *dev, bool on)
{
#if defined(CONFIG_LCD_GC9D01_TE)
  gc9d01_cmddata(dev, GC9D01_TEON, (const uint8_t *)"\x00", 1);
#endif

#if defined(CONFIG_LCD_GC9D01_INVERT)
  gc9d01_sendcmd(dev, GC9D01_INVERSION_ON);
#endif

  if (on)
    {
      gc9d01_sendcmd(dev, GC9D01_DISON);
    }
  else
    {
      gc9d01_sendcmd(dev, GC9D01_DISOFF);
    }
}

static void gc9d01_setorientation(FAR struct gc9d01_dev_s *dev)
{
  uint8_t reg = 0x00;

#if defined(CONFIG_LCD_GC9D01_BGR)
  reg |= GC9D01_MADCTL_BGR;
#endif

  gc9d01_cmddata(dev, GC9D01_MADCTL, &reg, 1);
}

static void gc9d01_setcursor(FAR struct gc9d01_dev_s *dev,
                             uint16_t x0, uint16_t y0,
                             uint16_t x1, uint16_t y1)
{
  gc9d01_sendcmd(dev, GC9D01_PASET);
  gc9d01_select(dev->spi, 8);
  SPI_SEND(dev->spi, (y0 + GC9D01_YOFFSET) >> 8);
  SPI_SEND(dev->spi, (y0 + GC9D01_YOFFSET) & 0xff);
  SPI_SEND(dev->spi, (y1 + GC9D01_YOFFSET) >> 8);
  SPI_SEND(dev->spi, (y1 + GC9D01_YOFFSET) & 0xff);
  gc9d01_deselect(dev->spi);

  gc9d01_sendcmd(dev, GC9D01_CASET);
  gc9d01_select(dev->spi, 8);
  SPI_SEND(dev->spi, (x0 + GC9D01_XOFFSET) >> 8);
  SPI_SEND(dev->spi, (x0 + GC9D01_XOFFSET) & 0xff);
  SPI_SEND(dev->spi, (x1 + GC9D01_XOFFSET) >> 8);
  SPI_SEND(dev->spi, (x1 + GC9D01_XOFFSET) & 0xff);
  gc9d01_deselect(dev->spi);
}

static void gc9d01_bpp(FAR struct gc9d01_dev_s *dev, int bpp)
{
  uint8_t depth;

  if (dev->bpp != bpp)
    {
      depth = bpp >> 2 | 1;
      depth = (depth & 0x0f) << 4 | depth;
      gc9d01_cmddata(dev, GC9D01_COLMOD, &depth, 1);

      dev->bpp = bpp;
    }
}

static void gc9d01_wrram(FAR struct gc9d01_dev_s *dev,
                         FAR const uint8_t *buff, size_t size, size_t skip,
                         size_t count)
{
  size_t i;
  size_t j;

  gc9d01_sendcmd(dev, GC9D01_RAMWR);

  gc9d01_select(dev->spi, 8);

  /* RGB565 pixels are stored little-endian in memory (low byte first) but
   * the panel expects the most-significant byte first.  The bit-bang
   * lower-half only supports 8-bit transfers, so swap each byte pair in
   * software before transmission.
   */

  for (i = 0; i < count; i++)
    {
      FAR const uint8_t *src = buff + (i * (size + skip));

      for (j = 0; j + 1 < size; j += 2)
        {
          SPI_SEND(dev->spi, src[j + 1]);
          SPI_SEND(dev->spi, src[j]);
        }
    }

  gc9d01_deselect(dev->spi);
}

#ifndef CONFIG_LCD_NOGETRUN
static void gc9d01_rdram(FAR struct gc9d01_dev_s *dev,
                         FAR uint16_t *buff, size_t size)
{
  gc9d01_sendcmd(dev, GC9D01_RAMRD);

  gc9d01_select(dev->spi, 8);
  SPI_RECVBLOCK(dev->spi, buff, size * 2);
  gc9d01_deselect(dev->spi);

  for (size_t i = 0; i + 1 < size * 2; i += 2)
    {
      FAR uint8_t *p = (FAR uint8_t *)buff;
      uint8_t tmp = p[i];

      p[i]     = p[i + 1];
      p[i + 1] = tmp;
    }
}
#endif

static void gc9d01_fill(FAR struct gc9d01_dev_s *dev, uint16_t color)
{
  int i;

  gc9d01_setcursor(dev, 0, 0, GC9D01_XRES - 1, GC9D01_YRES - 1);

  gc9d01_sendcmd(dev, GC9D01_RAMWR);
  gc9d01_select(dev->spi, 8);

  for (i = 0; i < GC9D01_XRES * GC9D01_YRES; i++)
    {
      SPI_SEND(dev->spi, color >> 8);
      SPI_SEND(dev->spi, color & 0xff);
    }

  gc9d01_deselect(dev->spi);
}

static int gc9d01_putrun(FAR struct lcd_dev_s *dev,
                         fb_coord_t row, fb_coord_t col,
                         FAR const uint8_t *buffer, size_t npixels)
{
  FAR struct gc9d01_dev_s *priv = (FAR struct gc9d01_dev_s *)dev;

  DEBUGASSERT(buffer && ((uintptr_t)buffer & 1) == 0);

  gc9d01_setcursor(priv, col, row, col + npixels - 1, row);
  gc9d01_wrram(priv, buffer, npixels * (priv->bpp >> 3), 0, 1);

  return OK;
}

static int gc9d01_putarea(FAR struct lcd_dev_s *dev,
                          fb_coord_t row_start, fb_coord_t row_end,
                          fb_coord_t col_start, fb_coord_t col_end,
                          FAR const uint8_t *buffer, fb_coord_t stride)
{
  FAR struct gc9d01_dev_s *priv = (FAR struct gc9d01_dev_s *)dev;
  size_t cols = col_end - col_start + 1;
  size_t rows = row_end - row_start + 1;
  size_t row_size = cols * (priv->bpp >> 3);

  DEBUGASSERT(buffer && ((uintptr_t)buffer & 1) == 0);

  gc9d01_setcursor(priv, col_start, row_start, col_end, row_end);

  if (stride == row_size)
    {
      gc9d01_wrram(priv, buffer, rows * row_size, 0, 1);
    }
  else
    {
      gc9d01_wrram(priv, buffer, row_size, stride - row_size, rows);
    }

  return OK;
}

#ifndef CONFIG_LCD_NOGETRUN
static int gc9d01_getrun(FAR struct lcd_dev_s *dev,
                         fb_coord_t row, fb_coord_t col,
                         FAR uint8_t *buffer, size_t npixels)
{
  FAR struct gc9d01_dev_s *priv = (FAR struct gc9d01_dev_s *)dev;
  FAR uint16_t *dest = (FAR uint16_t *)buffer;

  DEBUGASSERT(buffer && ((uintptr_t)buffer & 1) == 0);

  gc9d01_setcursor(priv, col, row, col + npixels - 1, row);
  gc9d01_rdram(priv, dest, npixels);

  return OK;
}
#endif

static int gc9d01_getvideoinfo(FAR struct lcd_dev_s *dev,
                               FAR struct fb_videoinfo_s *vinfo)
{
  DEBUGASSERT(dev && vinfo);

  vinfo->fmt     = GC9D01_COLORFMT;    /* Color format: RGB16-565 */
  vinfo->xres    = GC9D01_XRES;        /* Horizontal resolution */
  vinfo->yres    = GC9D01_YRES;        /* Vertical resolution */
  vinfo->nplanes = 1;                  /* Number of color planes */
  return OK;
}

static int gc9d01_getplaneinfo(FAR struct lcd_dev_s *dev,
                               unsigned int planeno,
                               FAR struct lcd_planeinfo_s *pinfo)
{
  FAR struct gc9d01_dev_s *priv = (FAR struct gc9d01_dev_s *)dev;

  DEBUGASSERT(dev && pinfo && planeno == 0);

  pinfo->putrun  = gc9d01_putrun;      /* Put a run into LCD */
  pinfo->putarea = gc9d01_putarea;     /* Put an area into LCD */
#ifndef CONFIG_LCD_NOGETRUN
  pinfo->getrun  = gc9d01_getrun;      /* Get a run from LCD */
#endif
  pinfo->buffer  = (FAR uint8_t *)priv->runbuffer; /* Run scratch buffer */
  pinfo->bpp     = priv->bpp;          /* Bits-per-pixel */
  pinfo->dev     = dev;                /* The lcd device */
  return OK;
}

static int gc9d01_getpower(FAR struct lcd_dev_s *dev)
{
  FAR struct gc9d01_dev_s *priv = (FAR struct gc9d01_dev_s *)dev;

  return priv->power;
}

static int gc9d01_setpower(FAR struct lcd_dev_s *dev, int power)
{
  FAR struct gc9d01_dev_s *priv = (FAR struct gc9d01_dev_s *)dev;

  DEBUGASSERT((unsigned)power <= CONFIG_LCD_MAXPOWER);

  if (power > 0)
    {
      gc9d01_display(priv, true);
      priv->power = power;
    }
  else
    {
      gc9d01_display(priv, false);
      priv->power = 0;
    }

  return OK;
}

static int gc9d01_getcontrast(FAR struct lcd_dev_s *dev)
{
  return -ENOSYS;
}

static int gc9d01_setcontrast(FAR struct lcd_dev_s *dev,
                              unsigned int contrast)
{
  return -ENOSYS;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

FAR struct lcd_dev_s *gc9d01_lcdinitialize(FAR struct spi_dev_s *spi)
{
  FAR struct gc9d01_dev_s *priv = &g_lcddev;

  priv->dev.getvideoinfo = gc9d01_getvideoinfo;
  priv->dev.getplaneinfo = gc9d01_getplaneinfo;
  priv->dev.getpower     = gc9d01_getpower;
  priv->dev.setpower     = gc9d01_setpower;
  priv->dev.getcontrast  = gc9d01_getcontrast;
  priv->dev.setcontrast  = gc9d01_setcontrast;
  priv->spi              = spi;

  gc9d01_init(priv);
  gc9d01_display(priv, false);
  gc9d01_sleep(priv, false);
  gc9d01_bpp(priv, GC9D01_BPP);
  gc9d01_setorientation(priv);
  gc9d01_fill(priv, 0x0000);
  gc9d01_display(priv, true);

  return &priv->dev;
}

#endif /* CONFIG_LCD_GC9D01 */
