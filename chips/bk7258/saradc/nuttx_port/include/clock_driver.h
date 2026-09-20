/* NuttX SoC adaptation for the authority ADC HAL clock API. */

#ifndef __BK7258_AUTHORITY_NUTTX_CLOCK_DRIVER_H
#define __BK7258_AUTHORITY_NUTTX_CLOCK_DRIVER_H

#include <arm_internal.h>
#include <bk7258_memorymap.h>

#define clk_enable_saradc_audio_pll()  do { } while (0)
#define clk_disable_saradc_audio_pll() do { } while (0)
#define clk_set_saradc_clk_26m() \
  do { modifyreg32(BK7258_SYS_CLKDIV1, BK7258_SYS_SADC_CLK_DCO, 0); } while (0)
#define clk_set_saradc_clk_dco() \
  do { modifyreg32(BK7258_SYS_CLKDIV1, 0, BK7258_SYS_SADC_CLK_DCO); } while (0)

#endif /* __BK7258_AUTHORITY_NUTTX_CLOCK_DRIVER_H */
