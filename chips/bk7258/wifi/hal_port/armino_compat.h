/* armino_compat.h - Armino SoC register primitives ported verbatim for the
 * BK7258 Wi-Fi bring-up HAL alignment work.
 *
 * Sources (cp/include/soc/bk7258/, cp/middleware/soc/bk7258/):
 *   - reg_base.h:      SOC_SYS_REG_BASE / SOC_AON_PMU_REG_BASE
 *   - soc.h:           REG_READ/REG_WRITE, GET_SYS_ANALOG_REG_IDX,
 *                      SYS_ANALOG_REG_SPI_STATE_* (non-poll_reg_b variant)
 *   - soc/sys_struct.h: sys_ana_reg0_t
 *   - soc/aon_pmu_struct.h: aon_pmu_r41_t
 *
 * OS primitives map to NuttX: the original GLOBAL_INT_DISABLE/
 * sys_drv_enter_critical callers use irqstate_t + enter_critical_section
 * at their call sites instead, so nothing Armino-specific survives here.
 */
#ifndef __BK7258_WIFI_HAL_PORT_ARMINO_COMPAT_H
#define __BK7258_WIFI_HAL_PORT_ARMINO_COMPAT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* reg_base.h (identical values to chips/bk7258/include/bk7258_memorymap.h) */
#define HP_SOC_SYS_REG_BASE       UINT32_C(0x44010000)
#define HP_SOC_AON_PMU_REG_BASE   UINT32_C(0x44000000)

/* soc.h register access primitives (statement-expression macros flattened
 * to plain expressions so they also work from C++) */
#define HP_REG_WRITE(_r, _v)  (*(volatile uint32_t *)(_r) = (_v))
#define HP_REG_READ(_r)       (*(volatile uint32_t *)(_r))
#define HP_REG_GET_BIT(_r, _b)  ((HP_REG_READ(_r) >> (_b)) & UINT32_C(0x1))

/* sys_ll.h analog window: ANA_REG0 lives at SYS_REG_BASE + 0x40<<2; the
 * analog bus writes go through an SPI bridge whose per-index busy bit sits
 * in the SYS 0x3a<<2 register.  (Non-CONFIG_ANA_REG_WRITE_POLL_REG_B
 * variant, matching the bk7258 build.) */
#define HP_SYS_ANA_REG0_ADDR             (HP_SOC_SYS_REG_BASE + (0x40u << 2))
#define HP_SYS_ANALOG_REG_SPI_STATE_REG  (HP_SOC_SYS_REG_BASE + (0x3au << 2))
#define HP_SYS_ANALOG_REG_SPI_STATE_POS(idx)  (idx)
#define HP_GET_SYS_ANALOG_REG_IDX(addr)  ((uint32_t)((addr) - HP_SYS_ANA_REG0_ADDR) >> 2)

/* soc/sys_struct.h: sys_ana_reg0_t (bit layout as upstream). */
typedef volatile union
{
  struct
  {
    uint32_t dpll_tsten    : 1;  /* bit[0] */
    uint32_t cp            : 3;  /* bit[1:3] */
    uint32_t spideten      : 1;  /* bit[4] */
    uint32_t hvref         : 2;  /* bit[5:6] */
    uint32_t lvref         : 2;  /* bit[7:8] */
    uint32_t rzctrl26m     : 1;  /* bit[9] */
    uint32_t looprzctrl    : 4;  /* bit[10:13] */
    uint32_t rpc           : 2;  /* bit[14:15] */
    uint32_t openloop_en   : 1;  /* bit[16] */
    uint32_t cksel         : 2;  /* bit[17:18] */
    uint32_t spitrig       : 1;  /* bit[19] */
    uint32_t band0         : 1;  /* bit[20] */
    uint32_t band1         : 1;  /* bit[21] */
    uint32_t band          : 3;  /* bit[22:24] */
    uint32_t bandmanual    : 1;  /* bit[25] */
    uint32_t dsptrig       : 1;  /* bit[26] */
    uint32_t lpen_dpll     : 1;  /* bit[27] */
    uint32_t nc_28_30      : 3;  /* bit[28:30] */
    uint32_t vctrl_dpllldo : 1;  /* bit[31] */
  };
  uint32_t v;
} hp_sys_ana_reg0_t;

/* soc/aon_pmu_struct.h: aon_pmu_r41_t (bit layout as upstream). */
typedef volatile union
{
  struct
  {
    uint32_t lpo_config         : 2;  /* bit[0:1]  DIVD=0 X32K=1 ROSC=2 */
    uint32_t flshsck_iocap      : 2;  /* bit[2:3] */
    uint32_t wakeup_ena         : 6;  /* bit[4:9] GPIO=0 RTC=1 WIFI=2 BT=3 */
    uint32_t io_drv             : 2;  /* bit[10:11] */
    uint32_t reserved_bit_12_13 : 2;  /* bit[12:13] */
    uint32_t xtal_sel           : 1;  /* bit[14] */
    uint32_t reserved_bit_15_23 : 9;  /* bit[15:23] */
    uint32_t halt_lpo           : 1;  /* bit[24] */
    uint32_t halt_busrst        : 1;  /* bit[25] */
    uint32_t halt_busiso        : 1;  /* bit[26] */
    uint32_t halt_buspwd        : 1;  /* bit[27] */
    uint32_t halt_blpiso        : 1;  /* bit[28] */
    uint32_t halt_blppwd        : 1;  /* bit[29] */
    uint32_t halt_wlpiso        : 1;  /* bit[30] */
    uint32_t halt_wlppwd        : 1;  /* bit[31] */
  };
  uint32_t v;
} hp_aon_pmu_r41_t;

#ifdef __cplusplus
}
#endif

#endif /* __BK7258_WIFI_HAL_PORT_ARMINO_COMPAT_H */
