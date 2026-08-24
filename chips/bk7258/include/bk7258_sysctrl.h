#ifndef __VENDOR_BEKEN_CHIP_BK7258_SYSCTRL_H
#define __VENDOR_BEKEN_CHIP_BK7258_SYSCTRL_H

#include <stdbool.h>

int bk7258_mac_power(bool enable);
int bk7258_phy_power(bool enable);

/* Armino leaves both BK7258 reset operations unimplemented.  Callers must
 * handle -ENOTSUP; clock or power control is not a reset substitute. */

int bk7258_mac_reset(void);
int bk7258_phy_reset(void);

#endif /* __VENDOR_BEKEN_CHIP_BK7258_SYSCTRL_H */
