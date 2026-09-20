/*
 * chips/bk7258/wifi/hal_port/include/command_line.h
 *
 * Empty compatibility header. bk_wifi_adapter.c includes it and registers
 * tx_verify_test_call_back_wrapper into the capability table, but the wrapper
 * body calls tx_verify_test_call_back() only inside `#if CONFIG_ATE_TEST`
 * (bk_wifi_adapter.c:1184). CONFIG_ATE_TEST is absent from the prebuilt
 * baseline sdkconfig.h and from our sys_config.h, so that call is dead code and
 * the wrapper compiles to an empty function.
 *
 * Upstream's header is a CLI parser private header (CLI_PRT / CONFIG_SYS_CBSIZE
 * and friends); none of it belongs in the Wi-Fi glue.
 */

#ifndef __BK7258_WIFI_GLUE_COMMAND_LINE_H
#define __BK7258_WIFI_GLUE_COMMAND_LINE_H

#endif /* __BK7258_WIFI_GLUE_COMMAND_LINE_H */
