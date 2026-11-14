# SPDX-License-Identifier: Apache-2.0
# Reliance Edge as a Zephyr VFS file system. The file system source is never part of this
# repository: it comes from the optional west group crux-reliance, or from a kit named here.

set(CRUX_RELIANCE_EDGE_DIR "${ZEPHYR_BASE}/../modules/fs/reliance-edge" CACHE PATH
    "Reliance Edge source tree")
set(CRUX_RELIANCE_EDGE_CONFIG_DIR "${CMAKE_CURRENT_LIST_DIR}/config" CACHE PATH
    "Directory with redconf.h, redconf.c, redosconf.h, redtypes.h and redostypes.h")
set(CRUX_EMMC_FLUSH_CONTROL OFF CACHE BOOL
    "Test fixtures only: allow an MMC disk without the cache flush correction on native_sim")

# The guard of the eMMC flush correction: this file system's commit relies on flush as a barrier.
if(CRUX_EMMC_FLUSH_CONTROL AND NOT CONFIG_BOARD_NATIVE_SIM)
    message(FATAL_ERROR "CRUX_EMMC_FLUSH_CONTROL is a native_sim test fixture setting and is "
        "refused on any other board")
endif()
if(CONFIG_DISK_DRIVER_MMC AND NOT CONFIG_CRUX_MMC_CACHE_FLUSH AND NOT CRUX_EMMC_FLUSH_CONTROL)
    message(FATAL_ERROR "Reliance Edge on an MMC disk requires CONFIG_CRUX_MMC_CACHE_FLUSH=y, "
        "because without it a disk sync does not flush the eMMC cache")
endif()

get_filename_component(crux_red_dir "${CRUX_RELIANCE_EDGE_DIR}" ABSOLUTE)
if(NOT EXISTS "${crux_red_dir}/include/redposix.h")
    message(FATAL_ERROR "Reliance Edge source not found at ${crux_red_dir}. Enable the optional "
        "west group crux-reliance (west config manifest.group-filter -- +crux-reliance, then "
        "west update reliance-edge), or set CRUX_RELIANCE_EDGE_DIR")
endif()
foreach(crux_red_config redconf.h redconf.c redosconf.h redtypes.h redostypes.h)
    if(NOT EXISTS "${CRUX_RELIANCE_EDGE_CONFIG_DIR}/${crux_red_config}")
        message(FATAL_ERROR "${crux_red_config} missing from CRUX_RELIANCE_EDGE_CONFIG_DIR "
            "(${CRUX_RELIANCE_EDGE_CONFIG_DIR})")
    endif()
endforeach()

# Relative entries are taken from CRUX_RELIANCE_EDGE_DIR. A kit with other files replaces the list.
if(NOT DEFINED CRUX_RELIANCE_EDGE_SOURCES)
    set(CRUX_RELIANCE_EDGE_SOURCES
        core/driver/blockio.c
        core/driver/buffer.c
        core/driver/buffercmn.c
        core/driver/core.c
        core/driver/dir.c
        core/driver/format.c
        core/driver/imap.c
        core/driver/imapextern.c
        core/driver/imapinline.c
        core/driver/inode.c
        core/driver/inodedata.c
        core/driver/volume.c
        posix/path.c
        posix/posix.c
        util/bitmap.c
        util/crc.c
        util/endian.c
        util/ftype.c
        util/heap.c
        util/memory.c
        util/namelen.c
        util/perm.c
        util/sign.c
        util/string.c
        bdev/bdev.c)
endif()
set(crux_red_sources "")
foreach(crux_red_source IN LISTS CRUX_RELIANCE_EDGE_SOURCES)
    if(NOT IS_ABSOLUTE "${crux_red_source}")
        set(crux_red_source "${crux_red_dir}/${crux_red_source}")
    endif()
    list(APPEND crux_red_sources "${crux_red_source}")
endforeach()

zephyr_library_named(crux_fs_reliance_edge)
zephyr_library_include_directories(
    "${CRUX_RELIANCE_EDGE_CONFIG_DIR}"
    "${crux_red_dir}/include"
    "${crux_red_dir}/core/include")
zephyr_library_sources(${crux_red_sources} "${CRUX_RELIANCE_EDGE_CONFIG_DIR}/redconf.c")

# The adapter and its OS services are this repository's code and build without warnings.
set(crux_red_adapter_sources
    "${CMAKE_CURRENT_LIST_DIR}/fs_reliance_edge.c"
    "${CMAKE_CURRENT_LIST_DIR}/os_bdev.c"
    "${CMAKE_CURRENT_LIST_DIR}/os_task.c"
    "${CMAKE_CURRENT_LIST_DIR}/os_mutex.c"
    "${CMAKE_CURRENT_LIST_DIR}/os_clock.c")
zephyr_library_sources(${crux_red_adapter_sources})
set_source_files_properties(${crux_red_adapter_sources} PROPERTIES
    COMPILE_OPTIONS "-Wall;-Wextra;-Werror")
