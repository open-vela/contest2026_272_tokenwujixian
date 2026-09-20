/****************************************************************************
 * board/bk7258-devkit/src/bk7258_partition.c
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stddef.h>
#include <stdint.h>

#include <arch/chip/bk7258_flash.h>

#include <bk7258_partition.h>

int bk7258_partition_get_info(unsigned int partition, uint32_t *start,
                              uint32_t *length)
{
  if (start == NULL || length == NULL)
    {
      return -EINVAL;
    }

  switch (partition)
    {
      case BK7258_PARTITION_SYS_RF:
        *start = BK7258_PART_SYS_RF_OFFSET;
        *length = BK7258_PART_SYS_RF_SIZE;
        return OK;

      case BK7258_PARTITION_SYS_NET:
        *start = BK7258_PART_SYS_NET_OFFSET;
        *length = BK7258_PART_SYS_NET_SIZE;
        return OK;

      default:
        return -ENOENT;
    }
}

int bk7258_partition_read(unsigned int partition, void *buffer,
                          uint32_t offset, size_t length)
{
  uint32_t start;
  uint32_t partition_length;
  int ret;

  if (length != 0 && buffer == NULL)
    {
      return -EINVAL;
    }

  ret = bk7258_partition_get_info(partition, &start, &partition_length);
  if (ret < 0)
    {
      return ret;
    }

  if (offset > partition_length || length > partition_length - offset)
    {
      return -ERANGE;
    }

  ret = bk7258_flash_initialize();
  if (ret < 0)
    {
      return ret;
    }

  return bk7258_flash_read(start + offset, buffer, length);
}
