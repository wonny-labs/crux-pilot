# SPDX-License-Identifier: Apache-2.0
# The flash-API device over a disk. Off by default (CONFIG_CRUX_FLASH_DISK).

# The guard of the eMMC flush correction: the trailer writes of MCUboot are ordered by flush.
if(NOT CONFIG_CRUX_FLASH_DISK_READ_ONLY AND
   NOT CONFIG_CRUX_MMC_CACHE_FLUSH AND NOT CONFIG_CRUX_FLASH_DISK_FLUSH_CONTROL)
    message(FATAL_ERROR "CONFIG_CRUX_FLASH_DISK writing to an MMC disk requires "
        "CONFIG_CRUX_MMC_CACHE_FLUSH=y, because without it a disk sync does not flush the eMMC "
        "cache. Select CONFIG_CRUX_FLASH_DISK_READ_ONLY=y for a device that only reads.")
endif()

zephyr_library_named(crux_flash_disk)
zephyr_library_sources(flash_disk.c)
