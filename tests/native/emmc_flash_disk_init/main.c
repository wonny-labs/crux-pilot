/* SPDX-License-Identifier: Apache-2.0 */
/*
 * A partition that ends beyond the disk (the overlay gives the second slot 512 MiB on a 4 MiB
 * simulated medium). The disk size is known only at run time, so the build accepts it and init
 * refuses it: the device is not ready, so a flash map cannot open the slot, and every call that
 * reaches the driver anyway is refused as out of range.
 */
#include <errno.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/storage/disk_access.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/ztest.h>

#define FLASH_DEV DEVICE_DT_GET(DT_NODELABEL(mmcflash))

ZTEST(emmc_flash_disk_init, test_a_partition_beyond_the_disk_makes_init_fail)
{
    const struct flash_area *fa;
    uint8_t byte;

    /* The disk itself came up, so the refusal is the driver's, not a failed disk. */
    zassert_equal(disk_access_status("MMC"), DISK_STATUS_OK);
    zassert_false(device_is_ready(FLASH_DEV));
    zassert_true(flash_area_open(PARTITION_ID(emmc_slot1), &fa) != 0);
    zassert_equal(flash_read(FLASH_DEV, 0, &byte, 1), -EINVAL, "the disk size stays zero");
}

ZTEST_SUITE(emmc_flash_disk_init, NULL, NULL, NULL, NULL, NULL);
