# SPDX-License-Identifier: Apache-2.0
# Row C3 of docs/rt1060-management-image-feasibility.md: the RAM disk of the RT1060-EVKB bench image, on native_sim.
# The disk is the node of the bench image's own overlay, so the geometry is the one the board image has, and what is
# measured here is what the board image allocates. The test links no Crux component: it asks FatFs, on that disk,
# whether the largest content that the bench image's File service and credential store allow fits.
cmake_minimum_required(VERSION 3.20.0)
get_filename_component(CRUX_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(DTC_OVERLAY_FILE "${CRUX_ROOT}/tests/zephyr/management_rt1060_compile/ram_disk.overlay")
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(crux_management_rt1060_ram_disk C)
target_sources(app PRIVATE main.c)
target_include_directories(app PRIVATE "${CRUX_ROOT}/components/credential/src")
