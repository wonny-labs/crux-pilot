/* SPDX-License-Identifier: Apache-2.0 */
/* The one volume of the Crux profile: see redconf.h and the Kconfig help. */
#include <redfs.h>
#include <redvolume.h>

/* A fixed sector count needs a fixed sector size. With both automatic, the volume takes the disk's
 * geometry from the offset onwards. */
#if CONFIG_CRUX_FS_RELIANCE_EDGE_SECTOR_COUNT == 0
#define CRUX_RED_SECTOR_SIZE SECTOR_SIZE_AUTO
#else
#define CRUX_RED_SECTOR_SIZE 512U
#endif

VOLCONF_CONST VOLCONF gaRedVolConf[REDCONF_VOLUME_COUNT] = {
    {
        .ulSectorSize = CRUX_RED_SECTOR_SIZE,
        .ullSectorCount = CONFIG_CRUX_FS_RELIANCE_EDGE_SECTOR_COUNT,
        .ullSectorOffset = CONFIG_CRUX_FS_RELIANCE_EDGE_SECTOR_OFFSET,
        .fAtomicSectorWrite = false,
        .ulInodeCount = INODE_COUNT_AUTO,
        .bBlockIoRetries = 0U,
        .pszPathPrefix = "",
    },
};
