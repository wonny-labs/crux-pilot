# SPDX-License-Identifier: Apache-2.0
# RT1060-EVKB bench image of the integrated management composition (HTTPS web, the command line over SSH and
# SFTP), on the Wolf profile: docs/rt1060-management-image-feasibility.md. Row C1 promoted the compile vehicle of
# stage 0 to a bench composition with a declaration (bench_declaration.json) and guards at three levels
# (bench_guard.cmake, src/bench_guard.h, Kconfig.bench). The directory keeps its name because the evidence of
# stage 0 names it. IT REFUSES TO START: CRUX_BENCH_RUN_REFUSED below is the switch, and nothing here is approved
# to run on a board. Its test certificate is not used (the certificate is generated), its SSH host key is held in
# test form (D5), and its first account has no implementation until row C4.
# It needs the crux-secure-management west group, and a ZEPHYR_BASE with the TLS socket patch
# (scripts/prepare-wolf-tls-zephyr.sh), which the guard checks. It reuses the source lists of
# tests/native/management_composition, which refuses a board on purpose, so those lists are repeated in
# management_target.cmake and are checked there.
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
# The declaration, which no native image sets. bench_guard.cmake refuses a build without it.
set(CRUX_MANAGEMENT_BENCH_IMAGE ON)
# The refusal in main(). Removing it is a reviewed change after the approval of the board stage.
set(CRUX_BENCH_RUN_REFUSED ON)
project(crux_management_rt1060_compile LANGUAGES C)
include("${CMAKE_CURRENT_LIST_DIR}/management_target.cmake")
crux_management_target_compose()
include(${CRUX_ROOT}/cmake/CruxWebAssets.cmake)
include(${CRUX_ROOT}/cmake/CruxWebRoutes.cmake)
set(route_sources ${CRUX_WEB_SESSION_ROUTES} ${CRUX_WEB_MANAGEMENT_ROUTES}
    ${CRUX_WEB_MANAGEMENT_NETWORK_ROUTES})
target_sources(app PRIVATE src/main.c src/ssh_target.c src/bench_first_account.c ${route_sources})
if(CRUX_BENCH_RUN_REFUSED)
    target_compile_definitions(app PRIVATE CRUX_BENCH_RUN_REFUSED=1)
else()
    target_compile_definitions(app PRIVATE CRUX_BENCH_RUN_REFUSED=0)
endif()
target_include_directories(app PRIVATE src)
crux_web_assets(app FOLDER ${services_dir}/site STYLE strict)
crux_web_routes(app SOURCES ${route_sources} ZEPHYR_RESOURCES REVIEWED ${services_dir}/reviewed-routes.txt)
crux_bench_manifest()
