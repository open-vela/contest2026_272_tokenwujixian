/* gpio_nuttx_port.c - state anchor for the imported authority GPIO driver.
 *
 * The authority splits its GPIO driver in two: the chip-specific half
 * (cp/middleware/driver/bk7258/gpio_driver.c, imported verbatim under
 * authority/) and a shared half (cp/middleware/driver/gpio/gpio_driver_base.c,
 * 1249 lines) that owns the s_gpio instance the chip half references through
 * `extern gpio_driver_t s_gpio`.
 *
 * Only the instance is needed here.  The rest of gpio_driver_base.c is
 * wakeup/retention/dynamic-keep-status bookkeeping behind CONFIG_GPIO_*
 * options this profile does not set, plus a gpio_driver_init() whose only
 * unconditional external dependencies are amp_res_init(AMP_RES_ID_GPIO)
 * (base:165) and bk_int_isr_register(INT_SRC_GPIO*, ...) (base:171-173).
 * Interrupt registration is a NuttX integration point owned by
 * chips/bk7258/bk7258_gpio.c, so importing that init would mean importing a
 * second, competing interrupt path for the same peripheral.
 *
 * The definition below is byte-identical to gpio_driver_base.c:44-46 --
 * copied, not reimplemented.  gpio_driver_t is `{ gpio_hal_t hal; }` and
 * gpio_hal_t is `{ gpio_hw_t *hw; gpio_id_t gpio_id; }`, so this is a base
 * pointer and nothing else: no accumulated state, no init ordering contract.
 */

#include "gpio_hal.h"
#include "gpio_driver_base.h"

/* gpio_driver_base.c:44-46, verbatim. */

gpio_driver_t s_gpio = {
	.hal.hw = (gpio_hw_t *)GPIO_LL_REG_BASE,
};
