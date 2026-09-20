/*
 * chips/bk7258/wifi/hal_port/net_param_shim.c
 *
 * NuttX-owned compatibility providers for Armino's legacy network parameter
 * API.  The Armino implementations are intentionally no-ops on this port:
 * persistent network configuration belongs to the NuttX network stack.
 */

#include <bk_prelude.h>
#include <bk_wifi.h>

UINT32 save_info_item(NET_INFO_ITEM item, UINT8 *ptr0, UINT8 *ptr1,
                      UINT8 *ptr2)
{
  (void)item;
  (void)ptr0;
  (void)ptr1;
  (void)ptr2;
  return 0;
}

UINT32 get_info_item(NET_INFO_ITEM item, UINT8 *ptr0, UINT8 *ptr1,
                     UINT8 *ptr2)
{
  (void)item;
  (void)ptr0;
  (void)ptr1;
  (void)ptr2;
  return 0;
}
