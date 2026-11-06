/****************************************************************************
 * chips/bk7258/bk7258_qspi.c
 *
 * QSPI1 master, exposed as a NuttX qspi_dev_s, carrying this board's JD9853
 * 4-line serial panel.
 *
 * Why: the plain SPI1 controller takes its clock from the 26 MHz XTAL only, so
 * the link tops out at 13 MHz -- measured at ~87 ms per 240x296@16bpp frame,
 * owning 28..45% of the single AP core.  The pins already suit QSPI1
 * (GPIO2/3/4 = CLK/CSN/IO0 at peripheral function 6) and its clock ladder
 * reaches 53.3 MHz (480 MHz / 9), which measured out as 1.3M cycles per flush
 * against 2.28M on SPI.
 *
 * How the panel still speaks 4-line serial: only CLK, CSN and IO0 belong to the
 * controller.  D/C stays a plain GPIO output and is driven here, exactly as the
 * authority keeps it for its serial panels (lcd_spi_driver.c:
 * gpio_dev_unmap(dc_pin) + output).  Commands and payload go out as cmd_c
 * frames: four bytes inline in cmd_c_h, the rest through the 64-word data FIFO.
 *
 * Register access is ours: the layout comes from the generated header
 * bk7258_qspi_hw.h, which chips/bk7258/tools/qspi_register_gate.py emits from
 * the authority's qspi_struct.h and re-checks on every run.  The AP image has
 * no Armino include path, and that gate is what keeps this file from drifting:
 * every operation below has a named authority counterpart in its coverage table.
 *
 * Measured facts this file depends on (all from the board):
 *
 *   - cmd_c_cfg2.data_len is a plain BYTE COUNT (a 10-bit field).  Frames of
 *     124 FIFO bytes with the field at 248 / 496 / 992 took 112 / 162 / 255 us,
 *     i.e. 0.192 us per unit against the 0.167 us a byte costs on one 53 MHz
 *     data line.  Cycle and nibble readings predict slopes 9x and 4x smaller.
 *   - The data FIFO holds 64 words (256 bytes).  A slice larger than that is
 *     written past the FIFO and lost, which shows up as the left part of every
 *     row being correct and the rest being stale.
 *   - wait_done() must spin on the status register with no delay call:
 *     up_udelay(1) rounds up to the 32 kHz tick on this port and costs ~61 us,
 *     which turned a 24 ms screen into a 2.4 s one.
 *
 * The memory-mapped window (0x68000000 with force_spi_cs_low + io_cpu_mem_sel +
 * disable_cmd_sck, the authority's shipped path for exactly this panel type)
 * was implemented three ways -- CPU byte stores, CPU word stores, and GDMA --
 * and in every case the controller clocked the wire at the SCK rate while the
 * panel received nothing.  Both io_cpu_mem_sel polarities, the cmd_a mapping
 * bit and glb_ctrl.soft_reset were tried.  It is not used here.
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BK7258_QSPI

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <debug.h>

#include <nuttx/arch.h>
#include <nuttx/clock.h>
#include <nuttx/kmalloc.h>
#include <nuttx/mutex.h>
#include <nuttx/spi/qspi.h>
#include <syslog.h>

#include "arm_internal.h"
#include "include/bk7258_qspi.h"
#include "include/bk7258_qspi_hw.h"
#include "include/bk7258_gpio.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* QSPI1 clock selection lives in SYS reg0x0a (byte 0x28):
 *   bit[10]   cksel_qspi1     0 = 320 MHz source, 1 = 480 MHz source
 *   bit[9:6]  ckdiv_qspi1     stage one, divides by n (measured: n = 9 gives
 *                             480/9 = 53.3 MHz, which the wire rate confirms)
 * The controller's own clk_rate field is stage two and stays bypassed.
 */

#define BK7258_SYS_QSPI_CLKDIV    (BK7258_SYS_BASE + UINT32_C(0x28))
#define BK7258_QSPI1_CKDIV_SHIFT  6
#define BK7258_QSPI1_CKDIV_MASK   (UINT32_C(0xf) << BK7258_QSPI1_CKDIV_SHIFT)
#define BK7258_QSPI1_CKSEL_480M   (UINT32_C(1) << 10)

/* Device clock gates, from the authority's dev_clk_pwr_id_t order: the enum is
 * 1-based while QSPI_ID_x is 0-based, so qspi_driver.c's qspi_clock_enable()
 * opens CLK_PWR_ID_QSPI_2 (bit 21) for QSPI_ID_1, which is this block
 * (0x46060000 / window 0x68000000).  Both gates are opened; unit 0 is unused.
 */

#define BK7258_SYS_QSPI0_CLK_EN   (UINT32_C(1) << 20)
#define BK7258_SYS_QSPI1_CLK_EN   (UINT32_C(1) << 21)

/* Pin peripheral function 6 selects QSPI1 on GPIO2/3/4 (gpio_map.h). */

#define BK7258_QSPI1_FUNC         6

/* Bound for the command-done poll: a few tens of milliseconds of spinning. */

#define BK7258_QSPI_DONE_SPINS    500000u

/* cmd_c packs four payload bytes into cmd_c_h; the rest goes through the FIFO.
 * 248 bytes is 62 of the 64 words, so 4 + 248 = one 252-byte frame that always
 * fits, and 252 is even, which keeps 16-bit pixels aligned across frames.
 */

#define BK7258_QSPI_INLINE_BYTES  4
#define BK7258_QSPI_SLICE_MAX     248

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct bk7258_qspi_dev_s
{
  struct qspi_dev_s dev;                /* Publicly visible part */
  mutex_t lock;                         /* Bus mutual exclusion */
  uint32_t frequency;                   /* Configured SCLK */
  uint32_t srcdiv;                      /* Stage-one divisor */
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct bk7258_qspi_dev_s g_qspidev =
{
  .lock = NXMUTEX_INITIALIZER,
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static inline uint32_t bk7258_qspi_get(uint32_t reg)
{
  return getreg32(reg);
}

static inline void bk7258_qspi_put(uint32_t reg, uint32_t value)
{
  putreg32(value, reg);
}

/* Read-modify-write one field of a QSPI register.  The masks come from the
 * generated header, so a field's position is never restated here.
 */

static inline void bk7258_qspi_setfield(uint32_t reg, uint32_t mask,
                                        uint32_t value)
{
  uint32_t shift = 0;
  uint32_t m = mask;

  while ((m & 1U) == 0)
    {
      m >>= 1;
      shift++;
    }

  bk7258_qspi_put(reg, (bk7258_qspi_get(reg) & ~mask) |
                       ((value << shift) & mask));
}

/* D/C is not a QSPI signal: it stays a plain GPIO output. */

static inline void bk7258_qspi_dc(bool data)
{
  bk7258_gpio_write(BK7258_LCD_QSPI_DC_PIN, data);
}

/* Wait for the command channel to report completion, then clear the flag the
 * way the authority does (status_clr is write-1-to-clear).  No up_udelay here:
 * see the file header.
 */

static int bk7258_qspi_wait_done(void)
{
  uint32_t i;

  for (i = 0; i < BK7258_QSPI_DONE_SPINS; i++)
    {
      if ((bk7258_qspi_get(BK7258_QSPI_STATUS) &
           BK7258_QSPI_STATUS_CMD_START_DONE_MASK) != 0)
        {
          break;
        }
    }

  bk7258_qspi_put(BK7258_QSPI_STATUS_CLR,
                  BK7258_QSPI_STATUS_CLR_CLR_CMD_START_DONE_MASK);
  bk7258_qspi_put(BK7258_QSPI_STATUS_CLR, 0);

  return i < BK7258_QSPI_DONE_SPINS ? OK : -ETIMEDOUT;
}

/* Copy payload words into the controller FIFO.  The authority's
 * qspi_ll_io_write() emits whole words, so a short tail is packed into an
 * aligned scratch word here instead of reading past the caller's buffer;
 * cfg2 (the byte count) is what decides how much reaches the wire.
 */

static void bk7258_qspi_fifo_write(FAR const uint8_t *data, size_t len)
{
  size_t words = (len + 3) / 4;
  size_t i;

  if ((len & 3) == 0)
    {
      for (i = 0; i < words; i++)
        {
          bk7258_qspi_put(BK7258_QSPI_FIFO_BASE + (i * 4),
                          ((FAR const uint32_t *)data)[i]);
        }

      return;
    }

  for (i = 0; i < words; i++)
    {
      uint8_t chunk[4] = { 0, 0, 0, 0 };
      uint32_t scratch;
      size_t n = len - (i * 4);

      if (n > 4)
        {
          n = 4;
        }

      memcpy(chunk, data + (i * 4), n);
      memcpy(&scratch, chunk, 4);
      bk7258_qspi_put(BK7258_QSPI_FIFO_BASE + (i * 4), scratch);
    }
}

/* One cmd_c frame, following the authority's
 * lcd_spi_send_data_with_qspi_indirect_mode() line for line:
 *
 *   len <= 4  the bytes ride in cmd_c_h and cfg1 marks the slot after them
 *             with 0x3 << (len * 2) -- QSPI_CMD_DISABLE, so the frame stops
 *             there.
 *   len  > 4  the first four bytes ride in cmd_c_h, cfg1 becomes 0x300, and
 *             the remaining count goes to cfg2 as a byte count while those
 *             bytes stream through the FIFO.
 */

static int bk7258_qspi_cmd_c(FAR const uint8_t *data, size_t len)
{
  size_t remain = len;
  size_t off = 0;

  bk7258_qspi_put(BK7258_QSPI_CMD_C_L, 0);
  bk7258_qspi_put(BK7258_QSPI_CMD_C_H, 0);
  bk7258_qspi_put(BK7258_QSPI_CMD_C_CFG1, 0);
  bk7258_qspi_put(BK7258_QSPI_CMD_C_CFG2, 0);

  for (;;)
    {
      size_t send;

      if (remain <= BK7258_QSPI_INLINE_BYTES)
        {
          uint32_t packed = 0;
          size_t i;

          for (i = 0; i < remain; i++)
            {
              packed |= (uint32_t)data[off + i] << (i * 8);
            }

          bk7258_qspi_put(BK7258_QSPI_CMD_C_H, packed);
          bk7258_qspi_put(BK7258_QSPI_CMD_C_CFG1,
                          UINT32_C(0x3) << (remain * 2));

          bk7258_qspi_setfield(BK7258_QSPI_CMD_C_CFG2,
                               BK7258_QSPI_CMD_C_CFG2_CMD_START_MASK, 1);
          return bk7258_qspi_wait_done();
        }

      bk7258_qspi_put(BK7258_QSPI_CMD_C_H,
                      (uint32_t)data[off] |
                      ((uint32_t)data[off + 1] << 8) |
                      ((uint32_t)data[off + 2] << 16) |
                      ((uint32_t)data[off + 3] << 24));
      remain -= BK7258_QSPI_INLINE_BYTES;
      off += BK7258_QSPI_INLINE_BYTES;

      send = remain < BK7258_QSPI_SLICE_MAX ? remain : BK7258_QSPI_SLICE_MAX;

      bk7258_qspi_put(BK7258_QSPI_CMD_C_CFG1, UINT32_C(0x300));
      bk7258_qspi_setfield(BK7258_QSPI_CMD_C_CFG2,
                           BK7258_QSPI_CMD_C_CFG2_DATA_LEN_MASK, send);
      bk7258_qspi_fifo_write(data + off, send);

      bk7258_qspi_setfield(BK7258_QSPI_CMD_C_CFG2,
                           BK7258_QSPI_CMD_C_CFG2_CMD_START_MASK, 1);

      if (bk7258_qspi_wait_done() != OK)
        {
          return -ETIMEDOUT;
        }

      remain -= send;
      off += send;

      if (remain == 0)
        {
          return OK;
        }
    }
}

/* Program the QSPI1 source clock and divider.  SYS reg0x0a carries the
 * selection: cksel_qspi1 picks 320 MHz (0) or 480 MHz (1) and ckdiv_qspi1
 * divides by n; the controller's own clk_rate field stays bypassed.
 */

static void bk7258_qspi_clock(uint32_t frequency)
{
  uint32_t src_div = (480000000U + frequency - 1U) / frequency;
  uint32_t regval;

  if (src_div < 5)
    {
      src_div = 5;                      /* vendor floor: divider of at least 4 */
    }
  else if (src_div > 16)
    {
      src_div = 16;
    }

  regval  = bk7258_qspi_get(BK7258_SYS_QSPI_CLKDIV);
  regval &= ~(BK7258_QSPI1_CKDIV_MASK | BK7258_QSPI1_CKSEL_480M);
  regval |= ((src_div - 1U) << BK7258_QSPI1_CKDIV_SHIFT);
  regval |= BK7258_QSPI1_CKSEL_480M;
  bk7258_qspi_put(BK7258_SYS_QSPI_CLKDIV, regval);

  bk7258_qspi_setfield(BK7258_QSPI_CONFIG, BK7258_QSPI_CONFIG_CLK_RATE_MASK, 0);

  g_qspidev.srcdiv    = src_div;
  g_qspidev.frequency = 480000000U / src_div;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

static int bk7258_qspi_lock(FAR struct qspi_dev_s *dev, bool lock)
{
  return lock ? nxmutex_lock(&g_qspidev.lock)
              : nxmutex_unlock(&g_qspidev.lock);
}

static uint32_t bk7258_qspi_setfrequency(FAR struct qspi_dev_s *dev,
                                         uint32_t frequency)
{
  bk7258_qspi_clock(frequency ? frequency : 48000000U);
  return g_qspidev.frequency;
}

static void bk7258_qspi_setmode(FAR struct qspi_dev_s *dev,
                                enum qspi_mode_e mode)
{
  (void)dev;
  (void)mode;
}

static void bk7258_qspi_setbits(FAR struct qspi_dev_s *dev, int nbits)
{
  (void)dev;
  (void)nbits;
}

/* Write-only: the DevKit display connector carries no return line, so a read
 * request is refused explicitly rather than silently returning zeroes.
 */

static int bk7258_qspi_command(FAR struct qspi_dev_s *dev,
                               FAR struct qspi_cmdinfo_s *cmdinfo)
{
  if (QSPICMD_ISREAD(cmdinfo->flags))
    {
      return -ENOSYS;
    }

  bk7258_qspi_dc(false);

  return bk7258_qspi_cmd_c(&cmdinfo->cmd, 1);
}

static int bk7258_qspi_memory(FAR struct qspi_dev_s *dev,
                              FAR struct qspi_meminfo_s *meminfo)
{
  return bk7258_qspi_write_pixels(meminfo->buffer, meminfo->buflen);
}

static FAR void *bk7258_qspi_alloc(FAR struct qspi_dev_s *dev, size_t buflen)
{
  (void)dev;
  return kmm_malloc(buflen);
}

static void bk7258_qspi_free(FAR struct qspi_dev_s *dev, FAR void *buffer)
{
  (void)dev;
  kmm_free(buffer);
}

static const struct qspi_ops_s g_qspiops =
{
  .lock          = bk7258_qspi_lock,
  .setfrequency  = bk7258_qspi_setfrequency,
  .setmode       = bk7258_qspi_setmode,
  .setbits       = bk7258_qspi_setbits,
  .command       = bk7258_qspi_command,
  .memory        = bk7258_qspi_memory,
  .alloc         = bk7258_qspi_alloc,
  .free          = bk7258_qspi_free,
};

FAR struct qspi_dev_s *bk7258_qspi_initialize(uint32_t frequency)
{
  g_qspidev.dev.ops = &g_qspiops;

  /* QSPI1 device clock gate first: without it every command burns the full
   * wait_done budget and the panel init appears to hang for seconds.
   */

  bk7258_qspi_put(BK7258_SYS_DEV_CLK_EN,
                  bk7258_qspi_get(BK7258_SYS_DEV_CLK_EN) |
                  BK7258_SYS_QSPI0_CLK_EN | BK7258_SYS_QSPI1_CLK_EN);
  up_udelay(10);

  bk7258_qspi_clock(frequency ? frequency : 48000000U);

  /* Route only CLK, CSN and IO0 to the controller; D/C stays a GPIO output on
   * GPIO5.  Routing that pad to QSPI1_IO1 (function 6) is what made an earlier
   * attempt fail, because the panel then never saw a D/C transition.
   *
   * Pad conditioning follows the authority's QSPI_SET_PIN(): CSN and IO0 pulled
   * up and all three pads at drive capacity 3 for the 53 MHz edges.
   */

  bk7258_gpio_periph(BK7258_LCD_QSPI_CLK_PIN, BK7258_QSPI1_FUNC);
  bk7258_gpio_periph(BK7258_LCD_QSPI_CS_PIN, BK7258_QSPI1_FUNC);
  bk7258_gpio_periph(BK7258_LCD_QSPI_IO0_PIN, BK7258_QSPI1_FUNC);

  bk7258_gpio_pull_up(BK7258_LCD_QSPI_CS_PIN);
  bk7258_gpio_pull_up(BK7258_LCD_QSPI_IO0_PIN);
  bk7258_gpio_drive(BK7258_LCD_QSPI_CLK_PIN, 3);
  bk7258_gpio_drive(BK7258_LCD_QSPI_CS_PIN, 3);
  bk7258_gpio_drive(BK7258_LCD_QSPI_IO0_PIN, 3);

  bk7258_gpio_config_output(BK7258_LCD_QSPI_DC_PIN, false);

  bk7258_qspi_setfield(BK7258_QSPI_GLB_CTRL,
                       BK7258_QSPI_GLB_CTRL_SOFT_RESET_MASK, 0);
  bk7258_qspi_setfield(BK7258_QSPI_RST_CFG,
                       BK7258_QSPI_RST_CFG_SW_RST_FIFO_MASK, 1);
  up_udelay(10);
  bk7258_qspi_setfield(BK7258_QSPI_RST_CFG,
                       BK7258_QSPI_RST_CFG_SW_RST_FIFO_MASK, 0);
  bk7258_qspi_setfield(BK7258_QSPI_CONFIG,
                       BK7258_QSPI_CONFIG_QSPI_EN_MASK, 1);

  /* dev_id reads 0x51535049, i.e. "QSPI": the cheapest proof that the block is
   * clocked and reachable at all.
   */

  syslog(LOG_INFO,
         "[qspi] QSPI1 up: %lu Hz (480M / %lu), pins fun%d, "
         "dev_id=%08lx version=%08lx\n",
         (unsigned long)g_qspidev.frequency,
         (unsigned long)g_qspidev.srcdiv, BK7258_QSPI1_FUNC,
         (unsigned long)bk7258_qspi_get(BK7258_QSPI_DEV_ID),
         (unsigned long)bk7258_qspi_get(BK7258_QSPI_VERSION_ID));

  return &g_qspidev.dev;
}

/* Panel command byte, D/C low. */

int bk7258_qspi_write_cmd(uint8_t cmd)
{
  bk7258_qspi_dc(false);
  return bk7258_qspi_cmd_c(&cmd, 1);
}

/* Parameter / pixel bytes only, D/C high. */

int bk7258_qspi_write_data(FAR const void *data, size_t len)
{
  if (len == 0)
    {
      return OK;
    }

  bk7258_qspi_dc(true);
  return bk7258_qspi_cmd_c(data, len);
}

/* Command byte followed by its parameter bytes, D/C toggling between them --
 * the same split the SPI path produces with SPI_CMDDATA().
 */

int bk7258_qspi_write_cmd_data(uint8_t cmd, FAR const void *data, size_t len)
{
  int ret = bk7258_qspi_write_cmd(cmd);

  if (ret != OK || len == 0)
    {
      return ret;
    }

  return bk7258_qspi_write_data(data, len);
}

/* Pixel burst.  begin/end keep chip select held by the frame itself: the
 * controller asserts CS for each cmd_c frame, so a whole frame of rows is
 * simply a run of frames with D/C high and no other traffic in between.
 */

int bk7258_qspi_pixels_begin(void)
{
  bk7258_qspi_dc(true);
  return OK;
}

int bk7258_qspi_pixels_write(FAR const void *data, size_t len)
{
  if (len == 0)
    {
      return OK;
    }

  bk7258_qspi_dc(true);
  return bk7258_qspi_cmd_c(data, len);
}

int bk7258_qspi_pixels_end(void)
{
  return OK;
}

/* Convenience: one complete burst. */

int bk7258_qspi_write_pixels(FAR const void *data, size_t len)
{
  int ret = bk7258_qspi_pixels_begin();

  if (ret == OK)
    {
      ret = bk7258_qspi_pixels_write(data, len);
    }

  bk7258_qspi_pixels_end();
  return ret;
}

#endif /* CONFIG_BK7258_QSPI */
