# SPDX-License-Identifier: Apache-2.0
# The web adapter over HTTPS on native_sim, on the management composition (Issue #185). One image per
# provider profile, chosen with -DCRUX_WEB_PROFILE=mbed or wolf. The wolf profile needs a ZEPHYR_BASE with
# the socket patch: scripts/prepare-wolf-tls-zephyr.sh. It operates no hardware.
cmake_minimum_required(VERSION 3.20.0)
get_filename_component(CRUX_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(composition_dir "${CRUX_ROOT}/tests/native/management_composition")
set(CRUX_WEB_PROFILE mbed CACHE STRING "Provider profile: mbed or wolf")
set(profile_files "${CRUX_ROOT}/tests/native/tls_offload_probe/${CRUX_WEB_PROFILE}.conf"
  "${composition_dir}/profile-psa.conf")
if(CRUX_WEB_PROFILE STREQUAL "mbed")
  list(APPEND profile_files "${composition_dir}/mbed-threading.conf")
elseif(NOT CRUX_WEB_PROFILE STREQUAL "wolf")
  message(FATAL_ERROR "CRUX_WEB_PROFILE must be mbed or wolf")
endif()
list(APPEND profile_files "${CMAKE_CURRENT_LIST_DIR}/${CRUX_WEB_PROFILE}.conf")
set(EXTRA_CONF_FILE "${profile_files}")
set(DTC_OVERLAY_FILE "${composition_dir}/app.overlay")
list(APPEND ZEPHYR_EXTRA_MODULES "${CRUX_ROOT}")
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(crux_web_https_native LANGUAGES C)
if(NOT CONFIG_BOARD_NATIVE_SIM)
  message(FATAL_ERROR "This suite is restricted to native_sim, and it operates no hardware")
endif()
include("${composition_dir}/composition/composition.cmake")
crux_native_management_compose()
include(${CRUX_ROOT}/cmake/CruxConsumerBoundary.cmake)
include(${CRUX_ROOT}/cmake/CruxWebRoutes.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/nsos_rdhup.cmake)

# The plain HTTP suite's application, with the real Access core in place of its double.
set(web_http "${CRUX_ROOT}/tests/native/web_http")
add_library(web_project_routes STATIC ${web_http}/src/project_routes.c)
target_link_libraries(web_project_routes PRIVATE crux::web)
target_compile_options(web_project_routes PRIVATE -Wall -Wextra -Werror)
crux_assert_portable_consumer(web_project_routes)

target_sources(app PRIVATE src/main.c src/tls_probe.c ${web_http}/src/probe_routes.c
  ${web_http}/src/upload_probe.c ${web_http}/src/socket_shim.c)
target_include_directories(app PRIVATE ${web_http}/src)
target_sources(native_simulator INTERFACE ${web_http}/src/host_socket.c)
target_link_options(app PUBLIC -Wl,--wrap=z_impl_zsock_shutdown -Wl,--wrap=z_impl_zsock_sendto)
target_link_libraries(app PRIVATE crux_web_adapter web_project_routes)
crux_web_routes(app SOURCES ${web_http}/src/probe_routes.c ${web_http}/src/project_routes.c
  src/tls_probe.c ZEPHYR_RESOURCES REVIEWED reviewed-routes.txt)
