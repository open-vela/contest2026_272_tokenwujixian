/*
 * Minimal flash-operation status contract used by vendor PHY notification
 * glue. This does not declare write, erase, or protection APIs: the BK7258
 * integration remains read-only at this stage.
 */

#ifndef __BK7258_WIFI_GLUE_FLASH_DRIVER_H
#define __BK7258_WIFI_GLUE_FLASH_DRIVER_H

typedef enum
{
  FLASH_OP_STATUS_IDLE = 0,
  FLASH_OP_STATUS_BUSY,
} flash_op_status_t;

#endif /* __BK7258_WIFI_GLUE_FLASH_DRIVER_H */
