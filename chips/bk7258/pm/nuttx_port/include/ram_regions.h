/* ram_regions.h - NuttX stand-in for the Armino build-time RAM region header.
 *
 * In the authority tree this file does not exist in source form: it is
 * generated during the build by
 * tools/build_tools/build_process/bk_build_ram_regions.py, which reads
 * projects/app/partitions/bk7258/ram_regions.csv and emits, through
 * tools/env_tools/bk_py_libs/bk_ram_region/bk_ram_region.py:150, one
 * `#define CONFIG_<NAME>_ADDR` / `_SIZE` pair per region plus
 * CONFIG_SRAM_BASE / CONFIG_SRAM_CAPACITY / CONFIG_PSRAM_BASE /
 * CONFIG_PSRAM_CAPACITY.  driver/pwr_clk.h:20 includes it unconditionally.
 *
 * This port expresses the same partitioning through the linker script and
 * Kconfig instead of a CSV, so the region addresses already exist here as
 * BK7258_* constants in bk7258_memorymap.h.  This header only re-exports them
 * under the names the generator would have produced; it defines no new
 * numbers.  The two layouts agree exactly -- walking the authority CSV from
 * the SRAM base gives:
 *
 *   AP_SPINLOCK  0x28000000 +0x010000 -> 0x28010000
 *   AP_RAM       0x28010000 +0x054000 -> 0x28064000   (BK7258_AP_RAM_BASE/SIZE)
 *   CP_RAM       0x28064000 +0x03b700 -> 0x2809F700
 *   PWR_MNG      0x2809F700 +0x000100 -> 0x2809F800   (BK7258_PWR_MNG_BASE/SIZE)
 *   SWAP         0x2809F800 +0x000800 -> 0x280A0000   (BK7258_SWAP_BASE/SIZE)
 *
 * and 0x280A0000 is exactly 0x28000000 + 640K, the SRAM capacity named in the
 * CSV header comment.  Our CP window ends where PWR_MNG starts, enforced at
 * compile time by bk7258_allocateheap.c:50
 * (static_assert(CONFIG_RAM_END == BK7258_PWR_MNG_BASE)) and again in the
 * linker script (flash.ld:115 asserts the SRAM region ends at 0x2809f700), so
 * the cross-domain regions below are a real hole in both trees rather than
 * memory some allocator also hands out.
 *
 * Only PWR_MNG is defined.  It is the sole region the imported PM sources
 * reference: pwr_clk.h:49-92 builds the CP/AP shared power-management ABI --
 * sleep votes, clock votes, PSRAM use counts, per-core wakeup counters -- out
 * of CONFIG_PWR_MNG_ADDR, and its compile-time bounds check at :64 needs
 * CONFIG_PWR_MNG_SIZE.  The authority CSV also carves several PSRAM slabs
 * (PSRAM_MEM_SLAB_AUDIO / _ENCODE / _DISPLAY, CP_PSRAM_HEAP, AP_PSRAM_HEAP,
 * AP_PSRAM_SECTION) for its video and audio pipelines.  Those are not
 * restated here: this port has no equivalent partitioning for them, and
 * writing plausible-looking numbers for regions nothing consumes would be
 * inventing layout rather than aligning to it.  Add a region here when
 * imported authority code actually references it, taking the value from
 * bk7258_memorymap.h.
 */

#ifndef __BK7258_PM_NUTTX_RAM_REGIONS_H
#define __BK7258_PM_NUTTX_RAM_REGIONS_H

#include <bk7258_memorymap.h>

#define CONFIG_PWR_MNG_ADDR                  BK7258_PWR_MNG_BASE
#define CONFIG_PWR_MNG_SIZE                  BK7258_PWR_MNG_SIZE

#endif /* __BK7258_PM_NUTTX_RAM_REGIONS_H */
