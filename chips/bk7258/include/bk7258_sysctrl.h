#ifndef __VENDOR_BEKEN_CHIP_BK7258_SYSCTRL_H
#define __VENDOR_BEKEN_CHIP_BK7258_SYSCTRL_H

#include <stdbool.h>

int bk7258_mac_power(bool enable);
int bk7258_phy_power(bool enable);
int bk7258_ofdm_power(bool enable);
int bk7258_bakp_power(bool enable);

/* Armino leaves both BK7258 reset operations unimplemented.  Callers must
 * handle -ENOTSUP; clock or power control is not a reset substitute. */

int bk7258_mac_reset(void);
int bk7258_phy_reset(void);

int bk7258_pmu_read(unsigned int reg, uint32_t *value);
int bk7258_pmu_write(unsigned int reg, uint32_t value);
int bk7258_analog_read(unsigned int reg, uint32_t *value);
int bk7258_analog_write(unsigned int reg, uint32_t value);
int bk7258_analog_update_bits(unsigned int reg, uint32_t mask,
                               uint32_t value);
int bk7258_pmu_get_chipid(uint32_t *value);
int bk7258_pmu_get_adc_cal(uint32_t *value);
int bk7258_pmu_get_bgcal(uint32_t *value);
int bk7258_sys_get_bgcalm(uint32_t *value);
int bk7258_sys_set_bgcalm(uint32_t value);
int bk7258_dpll_enable(bool enable);
int bk7258_cali_dpll(uint32_t param);

#endif /* __VENDOR_BEKEN_CHIP_BK7258_SYSCTRL_H */
