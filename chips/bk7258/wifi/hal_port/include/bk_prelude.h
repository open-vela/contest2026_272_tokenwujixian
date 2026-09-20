/*
 * chips/bk7258/wifi/hal_port/include/bk_prelude.h
 *
 * Establishes Armino's global build contract for the vendored translation
 * units: definitions upstream injects globally rather than through a header
 * the vendored sources include themselves.
 *
 * How it reaches the vendored units -- two mechanisms, both deliberate:
 *
 * 1. The wpa_supplicant closure (the bk7258_wpa aux library in
 *    wifi/CMakeLists.txt) is force-included per file with the compiler's
 *    -include flag.  Per-source COMPILE_OPTIONS on `arch` cannot express
 *    this on CMake 3.22 (source properties only apply in the directory
 *    scope that created the target), which is why that closure builds as
 *    its own object library.  Do NOT add the prelude to the `arch` target's
 *    own compile options either: that would alter every NuttX architecture
 *    translation unit.
 *
 * 2. The Armino wifi/phy units carry an explicit `#include <bk_prelude.h>`
 *    prologue at the top of each file, next to the reasoning that unit
 *    needs.  Exceptions are documented in the file that needs them:
 *    skbuff.c must NOT include it (the prelude installs NuttX's one-argument
 *    spin_lock_irqsave() while that file uses the Linux two-argument form),
 *    and rwnx_rx.c additionally includes "driver.h".
 *
 * Why a prelude instead of a normal header: upstream reaches BIT() via
 * common/bk_include.h -> soc/soc.h:85. We deliberately do not vendor
 * soc/soc.h (registers come from the team chip layer), and the headers that
 * need BIT() do not go through bk_include.h anyway --
 * wpa_supplicant/src/common/defs.h has no includes at all, and
 * rwnx_txq.h only includes sys_config.h/skbuff.h/bk_list.h. Defining BIT()
 * in bk_generic.h therefore would not reach them.
 *
 * Keep this file tiny and free of NuttX kernel headers: it lands in front of
 * every vendored source, so anything here is maximally load-bearing.
 */

#ifndef __BK7258_WIFI_HAL_PORT_BK_PRELUDE_H
#define __BK7258_WIFI_HAL_PORT_BK_PRELUDE_H

/* Armino's WPA port assumes these are supplied by the global build
 * environment before utils/common.h is parsed.  Keep the definitions in the
 * force-included prelude so every vendored translation unit sees the same ABI
 * and byte order, independent of its first include. */

#include <common/bk_typedef.h>
#include <components/system.h>
#include <os/os.h>
#include <wireless_ioctl_compat.h>
#include <wpa_compat/ip_addr.h>

#ifndef BK_SUPPLICANT
#  define BK_SUPPLICANT 1
#endif
#ifndef BK_MAC
#  define BK_MAC 1
#endif
#ifndef __LITTLE_ENDIAN
#  define __LITTLE_ENDIAN 1234
#endif
#ifndef __BIG_ENDIAN
#  define __BIG_ENDIAN 4321
#endif
#ifndef __BYTE_ORDER
#  define __BYTE_ORDER __LITTLE_ENDIAN
#endif

#ifndef __packed
#  define __packed __attribute__((__packed__))
#endif
#ifndef __PACKED
#  define __PACKED __attribute__((__packed__))
#endif

/* Armino's wifi_spinlock.h redefines NuttX lock macros with incompatible
 * arity. Include NuttX's real type/API, then consume the vendor header guard
 * before any quoted include can install its dummy replacements. */
#include <nuttx/spinlock.h>
#ifndef __SPIN_LOCK_H_
#  define __SPIN_LOCK_H_
#endif
#ifndef spin_lock_bh
#  define spin_lock_bh(lock)   spin_lock(lock)
#endif
#ifndef spin_unlock_bh
#  define spin_unlock_bh(lock) spin_unlock(lock)
#endif

/* Guarded: NuttX has its own BIT() in <nuttx/bits.h>, and wpa_supplicant's
 * src/utils/common.h:355 also defines it under the same guard. Value matches
 * upstream soc.h:85.
 */

#ifndef BIT
#  define BIT(i) (1 << (i))
#endif

/* likely/unlikely: NuttX spells these predict_true/predict_false
 * (include/nuttx/compiler.h:251), so map onto those rather than re-deriving
 * __builtin_expect -- that way a toolchain without the builtin degrades exactly
 * as NuttX intends. Used by rwnx_tx.c, rw_msdu.c and rwnx_misc.c.
 */

#include <nuttx/compiler.h>
#include <nuttx/nuttx.h>

#ifndef CONFIG_MSDU_RESV_HEAD_LENGTH
#  define CONFIG_MSDU_RESV_HEAD_LENGTH CONFIG_BK7258_WIFI_MSDU_RESV_HEAD_LENGTH
#endif
#ifndef CONFIG_MSDU_RESV_DESC_LENGTH
#  define CONFIG_MSDU_RESV_DESC_LENGTH CONFIG_BK7258_WIFI_MSDU_RESV_DESC_LENGTH
#endif

#ifndef MICROSECONDS
#  define MICROSECONDS 1000u
#endif

#ifndef os_sram_zalloc
#  define os_sram_zalloc(size) os_zalloc(size)
#endif

#ifndef likely
#  define likely(x)   predict_true(x)
#endif
#ifndef unlikely
#  define unlikely(x) predict_false(x)
#endif

#ifndef NULLPTR
#  define NULLPTR ((void *)0)
#endif
#ifndef CONFIG_TASK_WPAS_PRIO
#  ifdef CONFIG_BK7258_WIFI_WPA_TASK_PRIORITY
#    define CONFIG_TASK_WPAS_PRIO CONFIG_BK7258_WIFI_WPA_TASK_PRIORITY
#  else
#    define CONFIG_TASK_WPAS_PRIO 5
#  endif
#endif

/* BK_ASSERT reaches the vendored sources the same way BIT() does: the files
 * using it (bk_wifi_adapter.c, rw_ieee80211.c, rw_msg_rx.c, rw_msg_tx.c,
 * rwnx_rx.c, ...) never include common/bk_assert.h, so upstream must be
 * injecting it. Without it here the call parses as an implicit function and
 * fails at link, which is exactly what the first real link reported.
 *
 * Mapped to NuttX DEBUGASSERT, i.e. it follows CONFIG_DEBUG_ASSERTIONS: on a
 * release build the expression is evaluated for side effects and discarded,
 * matching NuttX's own contract rather than inventing a third behaviour.
 */

#include <assert.h>

#ifndef BK_ASSERT
#  define BK_ASSERT(exp)         DEBUGASSERT(exp)
#endif
#ifndef BK_ASSERT_EX
#  define BK_ASSERT_EX(exp, ...) DEBUGASSERT(exp)
#endif

/* getreg32 is a NuttX macro in arch/arm/src/common/arm_internal.h, not a
 * function; without the declaration in scope our own shims turned it into an
 * implicit call. Its sibling modifyreg32() is a real function, which is why only
 * getreg32 showed up undefined.
 */

#include <arm_internal.h>

#ifndef getreg32
#  define getreg32(a) (*(volatile uint32_t *)(a))
#endif

/* The vendor glue's rwm_proto.h owns its historical `struct ethhdr` layout
 * (dest/src arrays + proto). NuttX <netinet/if_ether.h> defines a different
 * ethhdr (embedded eth_addr structs), and a forced WPA IPv4/socket include can
 * otherwise make both appear in one translation unit. The vendor code does not
 * need NuttX's ethhdr type here, so keep that header from being re-entered in
 * vendor translation units; the NuttX netdev path uses its own headers in its
 * own translation units.
 */

#ifndef __INCLUDE_NETINET_IF_ETHER_H
#  define __INCLUDE_NETINET_IF_ETHER_H 1
#endif

/* Select wpa_supplicant's full diagnostics instead of Beken's reduced stand-in.
 *
 * This is the vendor's own switch, used in two places:
 *
 *   1. build_config.h:174 -- with CONFIG_WPA_LOG undefined the preprocessor
 *      evaluates `#if !CONFIG_WPA_LOG` as true and defines
 *      CONFIG_NO_STDOUT_DEBUG, CONFIG_NO_HOSTAPD_LOGGER and CONFIG_NO_WPA_MSG.
 *      Those collapse wpa_printf(), wpa_dbg(), wpa_msg() and the six
 *      wpa_hexdump*() variants into `do { } while (0)` (wpa_debug.h:38-57,
 *      :170-176) and keep the real implementations out of the image entirely --
 *      `nm` found no wpa_dbg, no wpa_msg and no _wpa_hexdump in the ELF.
 *
 *   2. Double-written call sites such as wpa_supplicant.c:3894-3902, which pick
 *      between the native `wpa_msg(wpa_s, MSG_INFO, ...)` and a `WPA_LOGD(...)`
 *      stand-in that routes through Beken's BK_LOG* macros.
 *
 * So the port has been running on the stand-in tier all along, and this flips
 * it to the full tier.  Nothing downstream is edited: wpa_debug.c and
 * wpa_debug.h remain byte-identical to the authority copies.
 *
 * WHY: the association path is diagnosed almost entirely through these macros,
 * so a failure there is silent by construction.  EVENT_ASSOC (events.c:5514)
 * can drop an association without a trace --
 *   if (wpa_s->disconnected)                  -> wpa_printf(MSG_INFO, "Ignore unexpected EVENT_ASSOC...")
 *   if (wpa_s->wpa_state == WPA_DISCONNECTED) -> wpa_printf(MSG_INFO, "Rx EVENT_ASSOC when WPAS is in disconnected state...")
 * -- and both lines were compiled out.  The board reaches `mm_set_vif_state ...
 * is_active=1, aid=0x1`, CONNECT_IND is confirmed delivered to the supplicant
 * queue, yet wpa_supplicant never leaves ASSOCIATING; telling "the handler never
 * ran" apart from "it ran and took a silent early return" is impossible while
 * its own log statements do not exist in the binary.
 *
 * The run-time level must stay MSG_DEBUG (main_supplicant.c:132).  Raising it to
 * MSG_INFO to cut volume would be a regression: after this flip `State: %s ->
 * %s` (wpa_supplicant.c:1056-1064) is emitted by wpa_dbg(MSG_DEBUG), so an INFO
 * threshold would discard the state transitions that are currently readable.
 *
 * DELIBERATE DEVIATION from the authority, which leaves CONFIG_WPA_LOG unset
 * (its sdkconfig.h has no such define, so cp/.../build_config.h:163 takes the
 * same suppressing branch).  Accepted because the authority does not need these
 * messages -- its association path works -- while ours is being brought up.  It
 * is strictly extra output, never less, and it changes no control flow: every
 * restored statement is a print.
 *
 * Cost, measured rather than assumed: the compiled WPA set (60 files listed in
 * CMakeLists.txt) holds 1398 wpa_printf/wpa_dbg call sites, so format strings
 * grow the image.  CP has room -- partition 0x165000 (1462272 B) against
 * 1084908 B in use, ~368 KiB free.
 *
 * Credentials stay protected through the upstream mechanism rather than a local
 * one: main_supplicant.c now passes wpa_debug_show_keys = 0, which makes every
 * wpa_hexdump_key()/wpa_hexdump_ascii_key() site print "[REMOVED]" in place of
 * the PMK/PTK/TK bytes (wpa_debug.c:113-127).
 *
 * REMOVE once the association path is closed: this is bring-up instrumentation,
 * and dropping it restores parity with the authority's logging.
 */

#ifndef CONFIG_WPA_LOG
#  define CONFIG_WPA_LOG 1
#endif

#endif /* __BK7258_WIFI_GLUE_BK_PRELUDE_H */
