#ifndef __VENDOR_BEKEN_CHIP_BK7258_CLOCK_H
#define __VENDOR_BEKEN_CHIP_BK7258_CLOCK_H

#include <stdbool.h>

int bk7258_mac_clock(bool enable);
int bk7258_phy_clock(bool enable);

#endif /* __VENDOR_BEKEN_CHIP_BK7258_CLOCK_H */
