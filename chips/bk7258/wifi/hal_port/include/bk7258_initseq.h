/*
 * chips/bk7258/wifi/hal_port/include/bk7258_initseq.h
 *
 * TEMPORARY BRING-UP DIAGNOSTIC -- REMOVE once the bk_wifi_init() stall is
 * localized.  Emits one "[initseq]" line per crossed init boundary so a single
 * UART capture identifies the exact function that never returns.
 *
 * Why this exists
 * ---------------
 * bk_wifi_init() stops producing output after calibration_init()'s last
 * "[cal] idx:41=..." line and never returns (no nsh> prompt for 46 s).  Only a
 * blocking call ON THE CALLING THREAD can produce that, which narrows the
 * candidates to cfg_param_init(), the four OSAL create/init calls inside
 * rwnx_intf_init()/core_thread_init(), and rwnxl_sleep().  A thread body that
 * hangs would NOT hold up bk_wifi_init()'s return, so the markers deliberately
 * bracket the creator calls rather than the created threads.
 *
 * Why LOG_WARNING and not LOG_INFO
 * --------------------------------
 * NuttX gates every record on `mask & LOG_MASK(priority)`
 * (drivers/syslog/vsyslog.c:292 for nx_syslog, libs/libc/syslog/lib_syslog.c:69
 * for the per-task copy seeded at sched/tls/task_initinfo.c:154).  LOG_MASK(p)
 * is `1 << p`, so LOG_INFO is bit6 (0x40) and LOG_DEBUG is bit7 (0x80).  A mask
 * of 0xbf clears exactly bit6, which silences every LOG_INFO record while
 * letting the noisier LOG_DEBUG through.  LOG_WARNING is bit4 (0x10) and passes
 * under both 0xbf and the 0xff default, so these markers stay visible even if
 * the mask is tightened again.
 *
 * Each syslog() call flushes its own stream before returning
 * (lib_syslograwstream_close() in nx_vsyslog), so the last line printed before
 * a hang is not lost in a buffer.  That property is what makes these markers
 * usable as a bisect.
 *
 * How to remove
 * -------------
 * Delete this header, the `#include <bk7258_initseq.h>` lines, and every
 * BK7258_INITSEQ*() statement.  The markers are pure statements with no side
 * effects on control flow, so removal cannot change behavior.  Call sites are
 * greppable with: git grep -n BK7258_INITSEQ
 */

#ifndef __BK7258_WIFI_GLUE_BK7258_INITSEQ_H
#define __BK7258_WIFI_GLUE_BK7258_INITSEQ_H

#include <syslog.h>

/* Plain boundary marker: "[initseq] <text>". */

#define BK7258_INITSEQ(text) \
  syslog(LOG_WARNING, "[initseq] " text "\n")

/* Boundary marker carrying one integer result, for the "returned, but with
 * what?" boundaries (an OSAL call that fails is a different diagnosis than one
 * that blocks).
 */

#define BK7258_INITSEQ_RET(text, value) \
  syslog(LOG_WARNING, "[initseq] " text " ret=%d\n", (int)(value))

/* Markers carrying the OSAL object name.
 *
 * The Armino FreeRTOS OSAL emitted one "os:D(100):create <name>" line per
 * thread.  This port replaced that layer with NuttX pthreads, so those lines
 * never existed here -- their absence in our captures is not evidence of a
 * failed thread creation.  These macros restore the same per-object visibility
 * from inside our own shim, which is why the four OSAL creators reached from
 * rwnx_intf_init() need no markers at their authority call sites.
 */

#define BK7258_INITSEQ_NAMED(text, name) \
  syslog(LOG_WARNING, "[initseq] " text " '%s'\n", \
         (name) != NULL ? (name) : "?")

#define BK7258_INITSEQ_NAMED_RET(text, name, value) \
  syslog(LOG_WARNING, "[initseq] " text " '%s' ret=%d\n", \
         (name) != NULL ? (name) : "?", (int)(value))

#endif /* __BK7258_WIFI_GLUE_BK7258_INITSEQ_H */
