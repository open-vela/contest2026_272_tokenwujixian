/****************************************************************************
 * chips/bk7258/audio/bk7258_gdma.c
 *
 * Minimal BK7258 General DMA (unit 0) driver for audio playback.  See
 * bk7258_gdma.h and BK7258_DAC_INIT_SEQUENCE.md §2.4 for context.
 *
 * Derived from Apache-2.0 code in bk_avdk_smp/:
 *   - middleware/soc/bk7258_ap/soc/dma_struct.h  (register layout)
 *   - middleware/soc/bk7258_ap/soc/dma_reg.h     (request-mux IDs)
 *   - middleware/soc/bk7258_ap/hal/dma_ll.h      (field encodings)
 *   - middleware/driver/general_dma/dma_driver.c (channel lifecycle)
 ****************************************************************************/

#include <nuttx/config.h>

#include <debug.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <nuttx/arch.h>
#include <nuttx/irq.h>
#include <nuttx/spinlock.h>

#include "arm_internal.h"
#include "bk7258_internal.h"
#include "bk7258_gdma.h"

#include <arch/chip/bk7258_memorymap.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BK7258_DMA0_BASE           UINT32_C(0x45020000)
#define BK7258_GDMA_CHANNELS       8

/* Global (per-unit) offsets, word index * 4. */

#define BK7258_DMA_PRIO_MODE       (BK7258_DMA0_BASE + (0x02u * 4u))
#define BK7258_DMA_SECURE_ATTR     (BK7258_DMA0_BASE + (0x04u * 4u))
#define BK7258_DMA_PRIV_ATTR       (BK7258_DMA0_BASE + (0x05u * 4u))
#define BK7258_DMA_INT_STATUS0     (BK7258_DMA0_BASE + (0x06u * 4u))
#define BK7258_DMA_INT_STATUS1     (BK7258_DMA0_BASE + (0x07u * 4u))
#define BK7258_DMA_INT_ALLOCATE    (BK7258_DMA0_BASE + (0x0Au * 4u))

#define DMA_PRIO_SOFT_RESET        (UINT32_C(1) << 0)
#define DMA_PRIO_BPS_CLK_GATE      (UINT32_C(1) << 1)  /* 0=run, 1=gate */

/* Each channel occupies 16 32-bit words starting at word 0x10 + ch*0x10. */

#define DMA_CH_BASE(ch)            (BK7258_DMA0_BASE + \
                                    ((0x10u + (uint32_t)(ch) * 0x10u) * 4u))
#define DMA_CH_CTRL(ch)            (DMA_CH_BASE(ch) + 0x00u)
#define DMA_CH_DST_ADDR(ch)        (DMA_CH_BASE(ch) + 0x04u)
#define DMA_CH_SRC_ADDR(ch)        (DMA_CH_BASE(ch) + 0x08u)
#define DMA_CH_DST_LOOP_END(ch)    (DMA_CH_BASE(ch) + 0x0Cu)
#define DMA_CH_DST_LOOP_START(ch)  (DMA_CH_BASE(ch) + 0x10u)
#define DMA_CH_SRC_LOOP_END(ch)    (DMA_CH_BASE(ch) + 0x14u)
#define DMA_CH_SRC_LOOP_START(ch)  (DMA_CH_BASE(ch) + 0x18u)
#define DMA_CH_REQ_MUX(ch)         (DMA_CH_BASE(ch) + 0x1Cu)
#define DMA_CH_SRC_PAUSE(ch)       (DMA_CH_BASE(ch) + 0x20u)
#define DMA_CH_DST_PAUSE(ch)       (DMA_CH_BASE(ch) + 0x24u)
#define DMA_CH_STATUS(ch)          (DMA_CH_BASE(ch) + 0x30u)

/* ctrl register bit layout (dma_struct.h). */

#define DMA_CTRL_ENABLE            (UINT32_C(1) << 0)
#define DMA_CTRL_FIN_INT_EN        (UINT32_C(1) << 1)
#define DMA_CTRL_HALF_INT_EN       (UINT32_C(1) << 2)
#define DMA_CTRL_MODE_REPEAT       (UINT32_C(1) << 3)
#define DMA_CTRL_SRC_WIDTH_SHIFT   4
#define DMA_CTRL_DST_WIDTH_SHIFT   6
#define DMA_CTRL_WIDTH_MASK        0x3u
#define DMA_CTRL_SRC_INC_EN        (UINT32_C(1) << 8)
#define DMA_CTRL_DST_INC_EN        (UINT32_C(1) << 9)
#define DMA_CTRL_SRC_LOOP_EN       (UINT32_C(1) << 10)
#define DMA_CTRL_DST_LOOP_EN       (UINT32_C(1) << 11)
#define DMA_CTRL_PRIO_SHIFT        12
#define DMA_CTRL_TRANSFER_LEN_SHIFT 16
#define DMA_CTRL_TRANSFER_LEN_MASK (UINT32_C(0xFFFF) << 16)

/* req_mux register bit layout. */

#define DMA_REQ_SRC_MUX_SHIFT      0
#define DMA_REQ_DST_MUX_SHIFT      5
#define DMA_REQ_MUX_MASK           0x1Fu
/* Bit 20 = src_sec_attr, bit 21 = dst_sec_attr, bit 22 = bus_err_int_en
 * (per bk_avdk_smp/ap/middleware/soc/bk7258_ap/soc/dma_struct.h).  When the
 * DMA is programmed from the TrustZone secure world, both attr bits MUST
 * be set or the AHB slave silently drops the transfer without firing the
 * finish interrupt.
 */
#define DMA_REQ_SRC_SEC_ATTR       (UINT32_C(1) << 20)
#define DMA_REQ_DST_SEC_ATTR       (UINT32_C(1) << 21)
#define DMA_REQ_BUS_ERR_INT_EN     (UINT32_C(1) << 22)

/* status register bit layout. */

#define DMA_STAT_FIN_INT           (UINT32_C(1) << 18)
#define DMA_STAT_HALF_INT          (UINT32_C(1) << 19)
#define DMA_STAT_BUS_ERR_INT       (UINT32_C(1) << 20)

/* Sys ctrl bits. */

#define SYS_CPU_DEVICE_CLK_EN      (BK7258_SYS_BASE + (0xCu * 4u))
#define   SYS_CPU_DEVICE_CLK_GDMA_CKEN_BIT   16  /* dma_cken in sys_struct. */

/* Per-CPU interrupt enable table.  On BK7258 the sys ctrl exposes one
 * "cpu<N>_int_0_31_en" register per physical CPU.  The AP image runs on
 * physical CPU1, so we must gate the GDMA interrupt through the CPU1 table
 * (reg 0x22).  Writing bit 11 into CPU0's table (reg 0x20) as we did in
 * the initial bring-up left the AP silently deaf to every DMA finish IRQ.
 */

#ifdef CONFIG_BK7258_COMPONENT_AP
#  define SYS_CPU_INT_0_31_EN      (BK7258_SYS_BASE + (0x22u * 4u))
#else
#  define SYS_CPU_INT_0_31_EN      (BK7258_SYS_BASE + (0x20u * 4u))
#endif
#define   SYS_CPU_INT_GDMA_INT_EN_BIT       11  /* cpu<N>_gdma_int_en. */

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct bk7258_gdma_ch_s
{
  bk7258_dma_cb_t cb;
  FAR void       *arg;
  bool            in_use;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static bool                    g_bk7258_gdma_ready;
static spinlock_t              g_bk7258_gdma_lock = SP_UNLOCKED;
static struct bk7258_gdma_ch_s g_bk7258_gdma_ch[BK7258_GDMA_CHANNELS];

/* Diagnostic counters.  IRQ context must not call syslog helpers, so we
 * bump a plain counter and let user-space read it back via a proc-like
 * accessor (or a debugger).  Values are cheap to inspect from a crash
 * dump and prove whether the GDMA IRQ ever fires.
 */

volatile uint32_t g_bk7258_gdma_isr_count;
volatile uint32_t g_bk7258_gdma_finish_count;
volatile uint32_t g_bk7258_gdma_half_count;
volatile uint32_t g_bk7258_gdma_buserr_count;

/****************************************************************************
 * Public: bk7258_gdma_dump
 ****************************************************************************/

void bk7258_gdma_get_stats(struct bk7258_gdma_stats_s *out)
{
  int ch;

  if (out == NULL)
    {
      return;
    }

  memset(out, 0, sizeof(*out));

  out->isr_count      = g_bk7258_gdma_isr_count;
  out->finish_count   = g_bk7258_gdma_finish_count;
  out->half_count     = g_bk7258_gdma_half_count;
  out->buserr_count   = g_bk7258_gdma_buserr_count;
  out->cpu_int_en_reg = SYS_CPU_INT_0_31_EN;
  out->cpu_int_en_val = getreg32(SYS_CPU_INT_0_31_EN);
  out->dev_clk_en_val = getreg32(SYS_CPU_DEVICE_CLK_EN);
  out->prio_mode      = getreg32(BK7258_DMA_PRIO_MODE);
  out->int_status0    = getreg32(BK7258_DMA_INT_STATUS0);
  out->int_allocate   = getreg32(BK7258_DMA_INT_ALLOCATE);

  for (ch = 0; ch < BK7258_GDMA_CHANNELS && ch < 8; ch++)
    {
      out->ch[ch].ctrl           = getreg32(DMA_CH_CTRL(ch));
      out->ch[ch].status         = getreg32(DMA_CH_STATUS(ch));
      out->ch[ch].src            = getreg32(DMA_CH_SRC_ADDR(ch));
      out->ch[ch].dst            = getreg32(DMA_CH_DST_ADDR(ch));
      out->ch[ch].src_loop_start = getreg32(DMA_CH_SRC_LOOP_START(ch));
      out->ch[ch].src_loop_end   = getreg32(DMA_CH_SRC_LOOP_END(ch));
      out->ch[ch].dst_loop_start = getreg32(DMA_CH_DST_LOOP_START(ch));
      out->ch[ch].dst_loop_end   = getreg32(DMA_CH_DST_LOOP_END(ch));
      out->ch[ch].reqmux         = getreg32(DMA_CH_REQ_MUX(ch));
      out->ch[ch].in_use         =
        g_bk7258_gdma_ch[ch].in_use ? 1 : 0;
    }
}

/****************************************************************************
 * Private Helpers
 ****************************************************************************/

static inline void bk7258_gdma_sys_bits(uint32_t addr, uint32_t mask,
                                        uint32_t value)
{
  uint32_t v = getreg32(addr);
  v &= ~mask;
  v |= (value & mask);
  putreg32(v, addr);
}

static int bk7258_gdma_isr(int irq, FAR void *context, FAR void *arg)
{
  int ch;

  (void)irq;
  (void)context;
  (void)arg;

  g_bk7258_gdma_isr_count++;

  for (ch = 0; ch < BK7258_GDMA_CHANNELS; ch++)
    {
      uint32_t status = getreg32(DMA_CH_STATUS(ch));
      uint32_t clear  = status & (DMA_STAT_FIN_INT | DMA_STAT_HALF_INT
                                  | DMA_STAT_BUS_ERR_INT);
      uint32_t event  = 0;

      if (clear == 0)
        {
          continue;
        }

      /* W1C: writing 1 to the pending bit clears it. */

      putreg32(clear, DMA_CH_STATUS(ch));

      if ((clear & DMA_STAT_HALF_INT) != 0u)
        {
          event |= BK7258_DMA_EVENT_HALF;
          g_bk7258_gdma_half_count++;
        }

      if ((clear & DMA_STAT_FIN_INT) != 0u)
        {
          event |= BK7258_DMA_EVENT_FULL;
          g_bk7258_gdma_finish_count++;
        }

      if ((clear & DMA_STAT_BUS_ERR_INT) != 0u)
        {
          event |= BK7258_DMA_EVENT_BUS_ERROR;
          g_bk7258_gdma_buserr_count++;
        }

      if (g_bk7258_gdma_ch[ch].cb != NULL)
        {
          g_bk7258_gdma_ch[ch].cb(ch, event, g_bk7258_gdma_ch[ch].arg);
        }
    }

  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int bk7258_gdma_initialize(void)
{
  irqstate_t flags;
  int ret;

  flags = spin_lock_irqsave(&g_bk7258_gdma_lock);
  if (g_bk7258_gdma_ready)
    {
      spin_unlock_irqrestore(&g_bk7258_gdma_lock, flags);
      return OK;
    }

  spin_unlock_irqrestore(&g_bk7258_gdma_lock, flags);

  /* Enable GDMA clock in sys ctrl. */

  bk7258_gdma_sys_bits(SYS_CPU_DEVICE_CLK_EN,
                       UINT32_C(1) << SYS_CPU_DEVICE_CLK_GDMA_CKEN_BIT,
                       UINT32_C(1) << SYS_CPU_DEVICE_CLK_GDMA_CKEN_BIT);

  /* Initialize the DMA controller.  Vendor dma_ll_init() sets prio_mode=0
   * then soft_reset=1 and leaves it at 1 — the comment "reset it before
   * any other operations" means assert reset *first*, configure channels,
   * and the controller is operational with soft_reset held.  Clearing
   * soft_reset to 0 (attempted earlier) is NOT what the vendor does and
   * caused the channel enable bit to fail to stick.  Match the vendor:
   * clear prio_mode, pulse a 0 to drop any bootloader state, then hold
   * soft_reset=1.
   */

  putreg32(0, BK7258_DMA_PRIO_MODE);
  putreg32(DMA_PRIO_SOFT_RESET, BK7258_DMA_PRIO_MODE);

  putreg32(UINT32_C(0xFFF), BK7258_DMA_SECURE_ATTR);
  putreg32(UINT32_C(0xFFF), BK7258_DMA_PRIV_ATTR);

  /* Route the aggregate GDMA IRQ line into CPU0 and attach a handler. */

  bk7258_gdma_sys_bits(SYS_CPU_INT_0_31_EN,
                       UINT32_C(1) << SYS_CPU_INT_GDMA_INT_EN_BIT,
                       UINT32_C(1) << SYS_CPU_INT_GDMA_INT_EN_BIT);

  ret = irq_attach(BK7258_IRQ_GDMA, bk7258_gdma_isr, NULL);
  if (ret < 0)
    {
      auderr("BK7258: gdma irq_attach failed: %d\n", ret);
      return ret;
    }

  up_enable_irq(BK7258_IRQ_GDMA);

  syslog(LOG_INFO,
         "BK7258 GDMA: irq=%d, cpu_int_en=%p bit=%u\n",
         BK7258_IRQ_GDMA, (void *)(uintptr_t)SYS_CPU_INT_0_31_EN,
         (unsigned)SYS_CPU_INT_GDMA_INT_EN_BIT);

  flags = spin_lock_irqsave(&g_bk7258_gdma_lock);
  g_bk7258_gdma_ready = true;
  spin_unlock_irqrestore(&g_bk7258_gdma_lock, flags);

  audinfo("BK7258: GDMA initialised (8 channels)\n");
  return OK;
}

bk7258_dma_ch_t bk7258_gdma_alloc(void)
{
  irqstate_t flags;
  int ch;

  if (!g_bk7258_gdma_ready)
    {
      int ret = bk7258_gdma_initialize();
      if (ret < 0)
        {
          return ret;
        }
    }

  flags = spin_lock_irqsave(&g_bk7258_gdma_lock);
  for (ch = 0; ch < BK7258_GDMA_CHANNELS; ch++)
    {
      if (!g_bk7258_gdma_ch[ch].in_use)
        {
          g_bk7258_gdma_ch[ch].in_use = true;
          g_bk7258_gdma_ch[ch].cb     = NULL;
          g_bk7258_gdma_ch[ch].arg    = NULL;
          spin_unlock_irqrestore(&g_bk7258_gdma_lock, flags);
          return ch;
        }
    }

  spin_unlock_irqrestore(&g_bk7258_gdma_lock, flags);
  return -EBUSY;
}

void bk7258_gdma_free(bk7258_dma_ch_t ch)
{
  irqstate_t flags;

  if (ch < 0 || ch >= BK7258_GDMA_CHANNELS)
    {
      return;
    }

  bk7258_gdma_stop(ch);

  flags = spin_lock_irqsave(&g_bk7258_gdma_lock);
  g_bk7258_gdma_ch[ch].cb     = NULL;
  g_bk7258_gdma_ch[ch].arg    = NULL;
  g_bk7258_gdma_ch[ch].in_use = false;
  spin_unlock_irqrestore(&g_bk7258_gdma_lock, flags);
}

int bk7258_gdma_start_m2p(bk7258_dma_ch_t ch,
                          const struct bk7258_dma_m2p_cfg_s *cfg)
{
  uint32_t ctrl;
  uint32_t reqmux;
  uint32_t transfer_len;

  if (ch < 0 || ch >= BK7258_GDMA_CHANNELS || cfg == NULL)
    {
      return -EINVAL;
    }

  if (cfg->buf == 0 || cfg->buf_len == 0 || cfg->dest == 0)
    {
      return -EINVAL;
    }

  if (cfg->src_width > BK7258_DMA_WIDTH_32 ||
      cfg->dst_width > BK7258_DMA_WIDTH_32)
    {
      return -EINVAL;
    }

  /* transfer_len is a byte count encoded as N - 1, independent of the
   * source and destination widths.  This matches the vendor DMA LL and
   * keeps the hardware remain_len value in bytes as well.
   */

  if (cfg->buf_len > 0x10000u)
    {
      return -EINVAL;
    }

  transfer_len = cfg->buf_len - 1u;

  /* Stash the client callback before we start; the ISR reads it. */

  g_bk7258_gdma_ch[ch].cb  = cfg->callback;
  g_bk7258_gdma_ch[ch].arg = cfg->arg;

  /* Reset the channel to a known state: disable, clear pending IRQs. */

  putreg32(0, DMA_CH_CTRL(ch));
  putreg32(DMA_STAT_FIN_INT | DMA_STAT_HALF_INT | DMA_STAT_BUS_ERR_INT,
           DMA_CH_STATUS(ch));

  /* Source: circular buffer in memory, address auto-increment. */

  putreg32((uint32_t)cfg->buf,                DMA_CH_SRC_ADDR(ch));
  if (!cfg->one_shot)
    {
      putreg32((uint32_t)cfg->buf,            DMA_CH_SRC_LOOP_START(ch));
      putreg32((uint32_t)(cfg->buf + cfg->buf_len),
               DMA_CH_SRC_LOOP_END(ch));
    }
  else
    {
      putreg32(0, DMA_CH_SRC_LOOP_START(ch));
      putreg32(0, DMA_CH_SRC_LOOP_END(ch));
    }

  /* Destination: peripheral FIFO port, no increment, no loop. */

  /* Destination: single 32-bit FIFO port.  Even though the physical
   * address must never change, the AVDK reference (onboard_speaker_stream.c
   * :461-486) sets dst_addr_inc_en=1 and dst_addr_loop_en=1 with
   * end = start + 4 so the DMA controller keeps advancing its internal
   * pointer and re-asserts the request line after each beat.  With
   * inc=0/loop=0 the controller latches the destination once and stops
   * requesting new data after only a few words (observed: remain_len
   * frozen at 1021, FIFO empty, no bus error).
   */

  putreg32((uint32_t)cfg->dest,               DMA_CH_DST_ADDR(ch));
  putreg32((uint32_t)cfg->dest,               DMA_CH_DST_LOOP_START(ch));
  putreg32((uint32_t)cfg->dest + 4u,          DMA_CH_DST_LOOP_END(ch));

  /* Request-mux: source from memory, destination to the peripheral.
   * bus-error IRQ on so a stray write surfaces immediately.
   */

  reqmux = ((uint32_t)BK7258_DMA_REQ_MEM & DMA_REQ_MUX_MASK)
                << DMA_REQ_SRC_MUX_SHIFT;
  reqmux |= ((uint32_t)cfg->dest_req_mux & DMA_REQ_MUX_MASK)
                << DMA_REQ_DST_MUX_SHIFT;
  reqmux |= DMA_REQ_BUS_ERR_INT_EN;
#ifdef CONFIG_ARCH_TRUSTZONE_SECURE
  reqmux |= DMA_REQ_SRC_SEC_ATTR | DMA_REQ_DST_SEC_ATTR;
#endif
  putreg32(reqmux, DMA_CH_REQ_MUX(ch));

  /* ctrl: full IRQ always on, half IRQ + repeat + src loop only for
   * circular playback.  One-shot mode fires just the "finish" IRQ when the
   * transfer completes and then stops the channel.
   */

  ctrl  = DMA_CTRL_FIN_INT_EN
        | DMA_CTRL_SRC_INC_EN
        | DMA_CTRL_DST_INC_EN
        | DMA_CTRL_DST_LOOP_EN;

  if (!cfg->one_shot)
    {
      ctrl |= DMA_CTRL_MODE_REPEAT
            | DMA_CTRL_HALF_INT_EN
            | DMA_CTRL_SRC_LOOP_EN;
    }

  ctrl |= ((uint32_t)cfg->src_width & DMA_CTRL_WIDTH_MASK)
                << DMA_CTRL_SRC_WIDTH_SHIFT;
  ctrl |= ((uint32_t)cfg->dst_width & DMA_CTRL_WIDTH_MASK)
                << DMA_CTRL_DST_WIDTH_SHIFT;
  ctrl |= (transfer_len & 0xFFFFu) << DMA_CTRL_TRANSFER_LEN_SHIFT;
  putreg32(ctrl, DMA_CH_CTRL(ch));

  /* Flush the source buffer out of D-cache so DMA sees the up-to-date
   * PCM samples.  audio lower-half is expected to flush again on each
   * enqueue; this call handles the very first period.
   */

#ifdef CONFIG_ARMV8M_DCACHE
  up_clean_dcache(cfg->buf, cfg->buf + cfg->buf_len);
#endif

  /* Kick it: set the enable bit last. */

  ctrl = getreg32(DMA_CH_CTRL(ch));
  ctrl |= DMA_CTRL_ENABLE;
  putreg32(ctrl, DMA_CH_CTRL(ch));

  audinfo("BK7258 GDMA ch%d started: ctrl=0x%08lx src=0x%08lx dst=0x%08lx "
          "reqmux=0x%08lx bytes=%lu\n",
          ch, (unsigned long)ctrl, (unsigned long)cfg->buf,
          (unsigned long)cfg->dest,
          (unsigned long)getreg32(DMA_CH_REQ_MUX(ch)),
          (unsigned long)cfg->buf_len);

  return OK;
}

void bk7258_gdma_stop(bk7258_dma_ch_t ch)
{
  uint32_t ctrl;

  if (ch < 0 || ch >= BK7258_GDMA_CHANNELS)
    {
      return;
    }

  ctrl = getreg32(DMA_CH_CTRL(ch));
  ctrl &= ~DMA_CTRL_ENABLE;
  putreg32(ctrl, DMA_CH_CTRL(ch));

  /* Clear any pending IRQ bits so the next start() begins from a clean
   * state.
   */

  putreg32(DMA_STAT_FIN_INT | DMA_STAT_HALF_INT | DMA_STAT_BUS_ERR_INT,
           DMA_CH_STATUS(ch));
}
