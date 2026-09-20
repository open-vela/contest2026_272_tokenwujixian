#ifndef __BK7258_MB_IPC_ROLE_CONFIG_H
#define __BK7258_MB_IPC_ROLE_CONFIG_H

#include <nuttx/config.h>

/* Keep this authority mailbox ABI switch private to its imported translation
 * units. Defining CONFIG_CPU_CNT globally would activate unrelated Wi-Fi and
 * SARADC branches before their full authority services are present. */
#ifndef CONFIG_CPU_CNT
#define CONFIG_CPU_CNT 2
#endif

/* The AP authority variant is itself an SMP image (physical CPU1+CPU2).
 * Its CONFIG_SOC_SMP route table intentionally exposes only CPU1-to-CPU0 for
 * this IPC instance; without it, the non-SMP branch references an unavailable
 * CPU1-to-CPU2 mailbox route. This does not add a CPU2 RPMsg peer. */
#ifdef CONFIG_BK7258_COMPONENT_AP
#ifndef CONFIG_SOC_SMP
#define CONFIG_SOC_SMP 1
#endif
#endif

/* Armino uses this only to place lock storage in an RTOS-specific section.
 * NuttX keeps ordinary static storage coherent for this CPU1+CPU2 image, so
 * the authority state variable needs no alternate linker section. */
#ifndef SPINLOCK_SECTION
#define SPINLOCK_SECTION
#endif

/* mb_ipc.c uses BK_ASSERT in the route-forwarding guard. The NuttX Wi-Fi
 * compatibility layer already owns that mapping; include it only for the
 * authority wrapper translation units. */
#include <common/bk_assert.h>

#endif /* __BK7258_MB_IPC_ROLE_CONFIG_H */
