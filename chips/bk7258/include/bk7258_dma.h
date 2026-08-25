/****************************************************************************
 * contest2026_272_tokenwujixian/chips/bk7258/include/bk7258_dma.h
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

#ifndef __VENDOR_BEKEN_CHIP_BK7258_DMA_H
#define __VENDOR_BEKEN_CHIP_BK7258_DMA_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <nuttx/compiler.h>

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* The first GDMA implementation only supports the memory/DTCM request mux.
 * The explicit request fields keep the API extensible without silently
 * accepting peripheral requests before they are implemented.
 */

enum bk7258_dma_request_e
{
  BK7258_DMA_REQ_MEMORY = 0
};

enum bk7258_dma_width_e
{
  BK7258_DMA_WIDTH_8BIT  = 1,
  BK7258_DMA_WIDTH_16BIT = 2,
  BK7258_DMA_WIDTH_32BIT = 4
};

struct bk7258_dma_config_s
{
  uintptr_t src_addr;
  uintptr_t dst_addr;
  size_t nbytes;
  enum bk7258_dma_request_e src_request;
  enum bk7258_dma_request_e dst_request;
  enum bk7258_dma_width_e src_width;
  enum bk7258_dma_width_e dst_width;
  bool src_increment;
  bool dst_increment;
};

/* Handles encode a channel index and an allocation generation.  They are
 * opaque values: callers must not dereference or retain them after dmafree.
 */

typedef uintptr_t BK7258_DMA_HANDLE;

struct bk7258_dma_diagnostics_s
{
  uint32_t prio_mode;
  uint32_t control;
  uint32_t dst_addr;
  uint32_t src_addr;
  uint32_t dst_loop_end;
  uint32_t dst_loop_start;
  uint32_t src_loop_end;
  uint32_t src_loop_start;
  uint32_t req_mux;
  uint32_t status;
  uint32_t secure_attr;
  uint32_t privileged_attr;
  uint32_t cpu0_int_enable;
  uint32_t irq_count;
  uint32_t last_events;
  bool running;
};

typedef CODE void (*bk7258_dma_callback_t)(BK7258_DMA_HANDLE handle,
                                            int result, FAR void *arg);

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

BK7258_DMA_HANDLE bk7258_dmachannel(void);
int bk7258_dmafree(BK7258_DMA_HANDLE handle);
int bk7258_dmasetup(BK7258_DMA_HANDLE handle,
                    FAR const struct bk7258_dma_config_s *config);
int bk7258_dmastart(BK7258_DMA_HANDLE handle,
                    bk7258_dma_callback_t callback, FAR void *arg);
int bk7258_dmastop(BK7258_DMA_HANDLE handle);
int bk7258_dma_get_diagnostics(
  BK7258_DMA_HANDLE handle, FAR struct bk7258_dma_diagnostics_s *diagnostics);

#endif /* __VENDOR_BEKEN_CHIP_BK7258_DMA_H */
