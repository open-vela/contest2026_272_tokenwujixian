/****************************************************************************
 * chips/bk7258/include/bk7258_reset.h
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

#ifndef __CHIPS_BK7258_INCLUDE_BK7258_RESET_H
#define __CHIPS_BK7258_INCLUDE_BK7258_RESET_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdbool.h>

#ifndef CONFIG_BK7258_COMPONENT_AP

#ifdef __cplusplus
extern "C"
{
#endif

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/* Reset the whole chip through the AON WDT.  Never returns.
 *
 * SYSRESETREQ (up_systemreset) is not usable on this AMP part: it resets
 * CPU0 only and leaves CPU1/RPTUN/shared-device state behind.  The AON WDT
 * resets at the PMU level like the reset button, so both cores restart.
 *
 * enter_bootloader == false: planned reboot.  The warm tag makes the BootROM
 *   fast-jump back into the running app and RESET_SOURCE_REBOOT is left in
 *   the AON PMU for the next boot to report.
 * enter_bootloader == true: clear the warm tag and record
 *   RESET_SOURCE_WATCHDOG so the BootROM runs the full cold chain and BL2
 *   opens the serial download window.
 *
 * The AON PMU survives the chip reset, so what is recorded here is what
 * BL2/the next boot reads: CP reason in R0 bits[4:11], AP in bits[24:30].
 */

void bk7258_chip_reset(bool enter_bootloader);

/* Reset-reason symbols the authority never declared anywhere: the cp-domain
 * setter and the persist-memory trio were plain globals in reset_reason.c.
 * Their behaviour is unchanged; only the missing prototype is added here,
 * because they are part of the export set we took over.
 */

void bk_misc_set_cp_reset_reason(uint32_t type);
void persist_memory_init(void);
uint32_t persist_memory_get(void);
bool persist_memory_is_lost(void);

/****************************************************************************
 * Probe hooks
 *
 * The command front end lives in app/bk7258_platform_test (the platform probe
 * app), which cannot include the baseline headers, so the register/HAL work
 * stays here and the app only calls these thin entry points.
 ****************************************************************************/

/* Print AON PMU R0, R7A and the cached reset reason. */

void bk7258_wdt_test_peek(void);

/* Round-trip the interrupt watchdog: 0 on success, negative errno. */

int bk7258_wdt_test_stop(void);
int bk7258_wdt_test_start(uint32_t timeout_ms);
int bk7258_wdt_test_feed(void);

/* Record a reset reason in the AON PMU (CP and AP domains). */

int bk7258_wdt_test_setreason(uint32_t reason);

/* Take the vendor force-reboot path; never returns. */

void bk7258_wdt_test_force_reset(void);

/* Read the sensor store and sample the ADC once.  0 on success. */

int bk7258_wdt_test_sensor(void);

/****************************************************************************
 * NuttX faces
 ****************************************************************************/

/* Register /dev/watchdog0 over the interrupt watchdog.  Requires CONFIG_WATCHDOG.
 * Same bodies as the Armino symbols in bk7258_wdt.c -- one implementation, two
 * faces.
 */

int bk7258_wdg_initialize(void);

#ifdef __cplusplus
}
#endif

#endif /* !CONFIG_BK7258_COMPONENT_AP */

#endif /* __CHIPS_BK7258_INCLUDE_BK7258_RESET_H */