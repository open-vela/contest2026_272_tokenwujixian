/*
 * chips/bk7258/wifi/hal_port/include/bk_general_dma.h
 *
 * DMA-accelerated copy used on the vendored TX path (rwnx_tx.c:247 and :256).
 *
 * Previously an empty header here, on the grounds that both call sites sit
 * inside `#if CONFIG_GENERAL_DMA`. That reasoning expired when sys_config.h was
 * realigned to the prebuilt baseline, where CONFIG_GENERAL_DMA is 1 -- the
 * calls are live now.
 *
 * hal_port/misc_shim.c implements it with memcpy(): dma_memcpy() is semantically a
 * copy, and upstream uses the DMA engine only to save CPU cycles, so a plain
 * copy is functionally equivalent (slower on the TX path). Two caveats for
 * whoever ports the real DMA driver:
 *   - upstream's DMA writes bypass the data cache; that difference is currently
 *     masked because CONFIG_CACHE_ENABLE is off. Re-examine both together.
 *   - dma_memcpy_by_chnl() is not declared: nothing in the compiled set uses it.
 */

#ifndef __BK7258_WIFI_GLUE_BK_GENERAL_DMA_H
#define __BK7258_WIFI_GLUE_BK_GENERAL_DMA_H

#include <stdint.h>

#include <common/bk_err.h>

#ifdef __cplusplus
extern "C" {
#endif

bk_err_t dma_memcpy(void *out, const void *in, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* __BK7258_WIFI_GLUE_BK_GENERAL_DMA_H */
