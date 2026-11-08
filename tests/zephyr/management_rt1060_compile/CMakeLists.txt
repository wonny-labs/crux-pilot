# SPDX-License-Identifier: Apache-2.0
# Feasibility vehicle for docs/rt1060-management-image-feasibility.md: the integrated management composition
# (HTTPS web, the command line over SSH and SFTP) for RT1060-EVKB rev B, on the Wolf profile. COMPILE AND LINK
# ONLY. It is never flashed or run, and it is not a product composition: its test certificate key is public, its
# SSH host key is held in test form, its first account is a declared test step, and nothing here is qualified.
# It needs the crux-secure-management west group, and a ZEPHYR_BASE with the TLS socket patch
# (scripts/prepare-wolf-tls-zephyr.sh). It reuses the source lists of tests/native/management_composition, which
# refuses a board on purpose, so those lists are repeated in management_target.cmake and are checked there.
cmake_minimum_required(VERSION 3.20.0)
get_filename_component(CRUX_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(composition_dir "${CRUX_ROOT}/tests/native/management_composition")
set(services_dir "${CRUX_ROOT}/tests/native/management_services")
set(EXTRA_CONF_FILE
    "${CRUX_ROOT}/tests/native/tls_offload_probe/wolf.conf"
    "${composition_dir}/profile-psa.conf"
    "${CRUX_ROOT}/components/ssh/ports/zephyr/wolf-ssh.conf"
    "${CMAKE_CURRENT_LIST_DIR}/target.conf")
set(DTC_OVERLAY_FILE
    "${CRUX_ROOT}/samples/controller/zephyr/m3-network.overlay"
    "${CMAKE_CURRENT_LIST_DIR}/target.overlay")
list(APPEND ZEPHYR_EXTRA_MODULES "${CRUX_ROOT}")
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(crux_management_rt1060_compile LANGUAGES C)
include("${CMAKE_CURRENT_LIST_DIR}/management_target.cmake")
crux_management_target_compose()
include(${CRUX_ROOT}/cmake/CruxWebAssets.cmake)
include(${CRUX_ROOT}/cmake/CruxWebRoutes.cmake)
set(route_sources ${CRUX_WEB_SESSION_ROUTES} ${CRUX_WEB_MANAGEMENT_ROUTES}
    ${CRUX_WEB_MANAGEMENT_NETWORK_ROUTES})
target_sources(app PRIVATE src/main.c src/ssh_target.c ${route_sources})
target_include_directories(app PRIVATE src)
crux_web_assets(app FOLDER ${services_dir}/site STYLE strict)
crux_web_routes(app SOURCES ${route_sources} ZEPHYR_RESOURCES REVIEWED ${services_dir}/reviewed-routes.txt)
