/* armino_soc_compat.h - minimal soc.h/reg_base.h replacement so the
 * verbatim-copied Armino sys_ll.h / aon_pmu_ll.h compile in NuttX.
 * Provides only the macros that the LL headers actually reference.
 * Values match chips/bk7258/include/bk7258_memorymap.h. */
#ifndef __BK7258_WIFI_HAL_PORT_ARMINO_SOC_COMPAT_H
#define __BK7258_WIFI_HAL_PORT_ARMINO_SOC_COMPAT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* reg_base.h */
#ifndef SOC_ADDR_OFFSET
#  define SOC_ADDR_OFFSET          0
#endif
#ifndef SOC_SYS_REG_BASE
#  define SOC_SYS_REG_BASE         (0x44010000 + SOC_ADDR_OFFSET)
#endif
#ifndef SOC_AON_PMU_REG_BASE
#  define SOC_AON_PMU_REG_BASE     (0x44000000 + SOC_ADDR_OFFSET)
#endif

/* soc.h generic register access (used by sys_ll analog functions) */
#define HP_REG_WRITE(_r, _v)  (*(volatile uint32_t *)(_r) = (_v))
#define HP_REG_READ(_r)       (*(volatile uint32_t *)(_r))
#define HP_REG_GET_BIT(_r, _b)  ((HP_REG_READ(_r) >> (_b)) & UINT32_C(0x1))
#ifndef REG_WRITE
#  define REG_WRITE(_r, _v)  (*(volatile uint32_t *)(_r) = (_v))
#endif
#ifndef REG_READ
#  define REG_READ(_r)       (*(volatile uint32_t *)(_r))
#endif
#ifndef REG_GET_BIT
#  define REG_GET_BIT(_r, _b)  ((REG_READ(_r) >> (_b)) & UINT32_C(0x1))
#endif

/* sys_ll.h analog SPI bridge constants */
#ifndef SYS_ANALOG_REG_SPI_STATE_REG
#  define SYS_ANALOG_REG_SPI_STATE_REG (SOC_SYS_REG_BASE + (0x3au << 2))
#endif
#ifndef SYS_ANALOG_REG_SPI_STATE_POS
#  define SYS_ANALOG_REG_SPI_STATE_POS(idx) (idx)
#endif
#ifndef GET_SYS_ANALOG_REG_IDX
#  define GET_SYS_ANALOG_REG_IDX(addr) ((uint32_t)((addr) - (SOC_SYS_REG_BASE + (0x40u << 2))) >> 2)
#endif
#ifndef SYS_ANA_REG0_ADDR
#  define SYS_ANA_REG0_ADDR (SOC_SYS_REG_BASE + (0x40u << 2))
#endif

#ifdef __cplusplus
}
#endif

#endif /* __BK7258_WIFI_HAL_PORT_ARMINO_SOC_COMPAT_H */
