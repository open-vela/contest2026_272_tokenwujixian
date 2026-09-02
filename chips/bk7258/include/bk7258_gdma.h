/****************************************************************************
 * chips/bk7258/audio/bk7258_gdma.h
 *
 * Minimal BK7258 General DMA driver, scoped to the audio playback need:
 *   - memory-to-peripheral half-word / word transfers,
 *   - circular source buffer (double-buffer) with half-transfer and
 *     full-transfer interrupts,
 *   - a single client callback invoked from IRQ context.
 *
 * Wider GDMA use cases (mem-to-mem, chained, scatter/gather, DMA1) live
 * outside this file; add them behind their own gate when they are needed.
 *
 * Register layout and request-mux numbers taken from
 *   bk_avdk_smp/ap/middleware/soc/bk7258_ap/soc/dma_struct.h
 *   bk_avdk_smp/ap/middleware/soc/bk7258_ap/soc/dma_reg.h
 *   bk_avdk_smp/ap/include/soc/bk7258/dma_cap.h
 * (Apache-2.0).
 ****************************************************************************/

#ifndef __CHIPS_BK7258_AUDIO_BK7258_GDMA_H
#define __CHIPS_BK7258_AUDIO_BK7258_GDMA_H

#include <nuttx/config.h>

#include <stdbool.h>
#include <stdint.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* DMA peripheral request mux IDs on BK7258 (from soc/dma_reg.h). */

#define BK7258_DMA_REQ_MEM              0x00u  /* memory (no req mux) */
#define BK7258_DMA_REQ_AUDIO_DAC        0x0Du  /* DMA_V_REQ_MUX_AUDIO */
#define BK7258_DMA_REQ_AUDIO_ADC        0x0Eu  /* DMA_V_REQ_MUX_AUDIO_RX */

/* Transfer data widths, matching dma_hw_t ctrl.src_data_width /
 * dest_data_width encoding.
 */

#define BK7258_DMA_WIDTH_8              0u
#define BK7258_DMA_WIDTH_16             1u
#define BK7258_DMA_WIDTH_32             2u

/* Result codes for the client callback. */

#define BK7258_DMA_EVENT_HALF           0x01u
#define BK7258_DMA_EVENT_FULL           0x02u
#define BK7258_DMA_EVENT_BUS_ERROR      0x04u

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* Opaque channel handle.  A negative value means "not allocated". */

typedef int bk7258_dma_ch_t;

/* IRQ callback.  Runs in interrupt context.  event is a bitmask of
 * BK7258_DMA_EVENT_* flags.
 */

typedef void (*bk7258_dma_cb_t)(bk7258_dma_ch_t ch, uint32_t event,
                                FAR void *arg);

/* Configuration for a mem-to-peripheral circular transfer.  The source
 * region [buf .. buf + buf_len) is treated as a ring; the DMA hardware
 * loops back to buf when it reaches buf + buf_len.
 *
 * - buf must be cache-line aligned when caches are enabled.
 * - buf_len must be a multiple of 2 * transfer_unit so that half-transfer
 *   IRQs land on a period boundary.
 * - dest is the peripheral FIFO port; it is written repeatedly without
 *   incrementing.
 */

struct bk7258_dma_m2p_cfg_s
{
  uintptr_t         buf;
  size_t            buf_len;
  uintptr_t         dest;
  uint8_t           dest_req_mux;   /* BK7258_DMA_REQ_AUDIO_DAC etc. */
  uint8_t           src_width;      /* BK7258_DMA_WIDTH_* */
  uint8_t           dst_width;      /* BK7258_DMA_WIDTH_* */
  bool              one_shot;       /* true: single transfer, no loop.
                                     * false: circular repeat with half+full
                                     * IRQs.
                                     */
  bk7258_dma_cb_t   callback;
  FAR void         *arg;
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef __cplusplus
extern "C"
{
#endif

/* One-time GDMA subsystem init: enable clock, soft-reset the controller
 * and register the GDMA IRQ handler.  Idempotent.
 */

int bk7258_gdma_initialize(void);

/* Allocate a free channel on DMA0 (channels 0..7).  Returns >=0 on
 * success, -EBUSY when all channels are in use.
 */

bk7258_dma_ch_t bk7258_gdma_alloc(void);

/* Release a channel previously returned by bk7258_gdma_alloc().  Stops
 * the channel and clears any pending IRQ.
 */

void bk7258_gdma_free(bk7258_dma_ch_t ch);

/* Program a channel for a mem-to-peripheral circular transfer, enable
 * half-transfer + full-transfer + bus-error IRQs and start the channel.
 */

int bk7258_gdma_start_m2p(bk7258_dma_ch_t ch,
                          const struct bk7258_dma_m2p_cfg_s *cfg);

/* Stop a running channel.  Safe to call from any context. */

void bk7258_gdma_stop(bk7258_dma_ch_t ch);

/* Diagnostic snapshot returned to user space. */

struct bk7258_gdma_stats_s
{
  uint32_t isr_count;      /* Total ISR entries.                     */
  uint32_t finish_count;   /* Finish IRQs seen (any channel).        */
  uint32_t half_count;     /* Half-transfer IRQs seen.               */
  uint32_t buserr_count;   /* Bus-error IRQs seen.                   */
  uint32_t cpu_int_en_reg; /* Address of the sys-ctrl CPU int enable */
  uint32_t cpu_int_en_val; /* Its current value.                     */
  uint32_t dev_clk_en_val; /* Sys-ctrl device clock enable.          */
  uint32_t prio_mode;      /* GDMA prio_mode reg.                    */
  uint32_t int_status0;    /* GDMA aggregate finish status.          */
  uint32_t int_allocate;   /* GDMA int allocation reg.               */
  struct
  {
    uint32_t ctrl;
    uint32_t status;
    uint32_t src;            /* Live source read-address register.   */
    uint32_t dst;            /* Live destination write-address reg. */
    uint32_t src_loop_start;
    uint32_t src_loop_end;
    uint32_t dst_loop_start;
    uint32_t dst_loop_end;
    uint32_t reqmux;
    uint8_t  in_use;
  } ch[8];
};

/* Fill a snapshot for user-space diagnostics. */

void bk7258_gdma_get_stats(struct bk7258_gdma_stats_s *out);

#ifdef __cplusplus
}
#endif

#endif /* __CHIPS_BK7258_AUDIO_BK7258_GDMA_H */
