/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Block device service on a Zephyr disk_access disk. The composition has initialised the disk
 * before it mounts or formats, so opening the device does not initialise it again. Sector numbers
 * arrive with the volume offset already applied.
 */
#include "fs_reliance_edge_priv.h"

#include <stdint.h>
#include <zephyr/storage/disk_access.h>
#include <zephyr/sys/util.h>

#include <redfs.h>

/* Set while no volume is mounted, and read by the services below during file system calls. */
static const char *crux_red_disk;

void crux_red_bdev_select(const char *disk_name)
{
    crux_red_disk = disk_name;
}

static bool crux_red_disk_usable(uint8_t bVolNum)
{
    return bVolNum == 0U && crux_red_disk != NULL;
}

/* disk_access counts sectors in 32 bits; refuse a range that would not fit. */
static bool crux_red_range_fits(uint64_t ullSectorStart, uint32_t ulSectorCount)
{
    return ullSectorStart <= UINT32_MAX && ulSectorCount <= UINT32_MAX - ullSectorStart;
}

REDSTATUS RedOsBDevConfig(uint8_t bVolNum, REDBDEVCTX context)
{
    ARG_UNUSED(bVolNum);
    ARG_UNUSED(context);
    return 0;
}

REDSTATUS RedOsBDevOpen(uint8_t bVolNum, BDEVOPENMODE mode)
{
    ARG_UNUSED(mode);
    return crux_red_disk_usable(bVolNum) ? 0 : -RED_EINVAL;
}

REDSTATUS RedOsBDevClose(uint8_t bVolNum)
{
    ARG_UNUSED(bVolNum);
    return 0;
}

REDSTATUS RedOsBDevGetGeometry(uint8_t bVolNum, BDEVINFO *pInfo)
{
    uint32_t sector_size = 0;
    uint32_t sector_count = 0;

    if (!crux_red_disk_usable(bVolNum)) {
        return -RED_EINVAL;
    }
    if (disk_access_ioctl(crux_red_disk, DISK_IOCTL_GET_SECTOR_SIZE, &sector_size) != 0 ||
        disk_access_ioctl(crux_red_disk, DISK_IOCTL_GET_SECTOR_COUNT, &sector_count) != 0) {
        return -RED_EIO;
    }
    pInfo->ulSectorSize = sector_size;
    pInfo->ullSectorCount = sector_count;
    return 0;
}

REDSTATUS RedOsBDevRead(uint8_t bVolNum, uint64_t ullSectorStart, uint32_t ulSectorCount,
                        void *pBuffer)
{
    if (!crux_red_disk_usable(bVolNum) || !crux_red_range_fits(ullSectorStart, ulSectorCount)) {
        return -RED_EINVAL;
    }
    /* The SD stack can return a positive value after a failed transfer, so only 0 is success. */
    if (disk_access_read(crux_red_disk, pBuffer, (uint32_t)ullSectorStart, ulSectorCount) != 0) {
        return -RED_EIO;
    }
    return 0;
}

REDSTATUS RedOsBDevWrite(uint8_t bVolNum, uint64_t ullSectorStart, uint32_t ulSectorCount,
                         const void *pBuffer)
{
    if (!crux_red_disk_usable(bVolNum) || !crux_red_range_fits(ullSectorStart, ulSectorCount)) {
        return -RED_EINVAL;
    }
    if (disk_access_write(crux_red_disk, pBuffer, (uint32_t)ullSectorStart, ulSectorCount) != 0) {
        return -RED_EIO;
    }
    return 0;
}

REDSTATUS RedOsBDevFlush(uint8_t bVolNum)
{
    if (!crux_red_disk_usable(bVolNum)) {
        return -RED_EINVAL;
    }
    return (disk_access_ioctl(crux_red_disk, DISK_IOCTL_CTRL_SYNC, NULL) != 0) ? -RED_EIO : 0;
}
