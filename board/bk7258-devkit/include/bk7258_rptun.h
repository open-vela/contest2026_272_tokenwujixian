/****************************************************************************
 * board/bk7258-devkit/include/bk7258_rptun.h
 ****************************************************************************/

#ifndef __VENDOR_BEKEN_BOARD_BK7258_DEVKIT_INCLUDE_BK7258_RPTUN_H
#define __VENDOR_BEKEN_BOARD_BK7258_DEVKIT_INCLUDE_BK7258_RPTUN_H

#include <nuttx/config.h>

#ifdef CONFIG_RPTUN
int bk7258_rptun_initialize(void);
#endif

#ifdef CONFIG_BK7258_COMPONENT_CP
/* Assert the AP reset boundary without touching the CP core. */
int bk7258_ap_reset_for_rptun(void);
#endif

#endif /* __VENDOR_BEKEN_BOARD_BK7258_DEVKIT_INCLUDE_BK7258_RPTUN_H */
