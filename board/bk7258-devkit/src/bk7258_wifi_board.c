/*
 * board/bk7258-devkit/src/bk7258_wifi_board.c
 *
 * BK7258 DevKit board-specific Wi-Fi configuration.
 *
 * Skeleton status: MAC source, calibration/regulatory inputs and external
 * PA/LNA are board-level concerns per plan §11.2. Values are recorded as
 * contracts; the stable-MAC reader and country-code source are wired once the
 * board storage layout is confirmed on hardware. GPIO26=TX_EN / GPIO28=RX_EN
 * stay unmapped (EPA disabled) on this board unless an external PA/LNA is
 * actually fitted.
 */

#include <nuttx/config.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <bk7258_partition.h>

#include "bk7258_wifi_internal.h"

int bk7258_wifi_board_init(void)
{
  return 0;
}

int bk7258_wifi_board_get_mac(uint8_t mac[6])
{
  uint8_t candidate[6];
  bool all_zero = true;
  bool all_ff = true;
  unsigned int i;
  int ret;

  if (mac == NULL)
    {
      return -EINVAL;
    }

  ret = bk7258_partition_read(BK7258_PARTITION_SYS_NET, candidate,
                              BK7258_SYS_NET_MAC_OFFSET,
                              sizeof(candidate));
  if (ret < 0)
    {
      memset(mac, 0, sizeof(candidate));
      return ret;
    }

  for (i = 0; i < sizeof(candidate); i++)
    {
      all_zero &= candidate[i] == 0;
      all_ff &= candidate[i] == UINT8_MAX;
    }

  if (all_zero || all_ff || (candidate[0] & 1) != 0)
    {
      memset(mac, 0, sizeof(candidate));
      return -ENODEV;
    }

  memcpy(mac, candidate, sizeof(candidate));
  return 0;
}
