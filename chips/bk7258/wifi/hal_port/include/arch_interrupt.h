/*
 * chips/bk7258/wifi/hal_port/include/arch_interrupt.h
 *
 * Empty compatibility header: bk_wifi_adapter.c includes it but references
 * none of its symbols (verified against upstream's declarations --
 * arch_interrupt_register_int / _unregister_int / _set_priority,
 * arch_int_enable_irq / _disable_irq / _get_enable_irq, and the
 * IRQ_DEFAULT_PRIORITY / INT_ID_MAX macros).
 *
 * Wi-Fi interrupt routing goes through the team chip layer instead: the enable
 * bits live in bk7258_memorymap.h and are driven by sys_drv_int_enable() /
 * sys_drv_int_group2_enable() in hal_port/hw_driver_shim.c, with the MAC/PHY IRQ
 * numbers in chips/bk7258/include/irq.h.
 */

#ifndef __BK7258_WIFI_GLUE_ARCH_INTERRUPT_H
#define __BK7258_WIFI_GLUE_ARCH_INTERRUPT_H

#endif /* __BK7258_WIFI_GLUE_ARCH_INTERRUPT_H */
