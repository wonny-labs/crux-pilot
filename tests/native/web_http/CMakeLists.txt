# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(crux_web_http_native LANGUAGES C)
if(NOT CONFIG_BOARD_NATIVE_SIM)
  message(FATAL_ERROR "This suite is restricted to native_sim, and it operates no hardware")
endif()
get_filename_component(CRUX_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
include(${CRUX_ROOT}/cmake/CruxConsumerBoundary.cmake)
include(${CRUX_ROOT}/cmake/CruxWebRoutes.cmake)

# A project's route: it compiles against crux::web alone, with no Zephyr requirement.
add_library(web_project_routes STATIC src/project_routes.c)
target_link_libraries(web_project_routes PRIVATE crux::web)
target_compile_options(web_project_routes PRIVATE -Wall -Wextra -Werror)
crux_assert_portable_consumer(web_project_routes)

target_sources(app PRIVATE src/main.c src/probe_routes.c src/access_double.c src/upload_probe.c
  src/socket_shim.c)
target_sources(native_simulator INTERFACE ${CMAKE_CURRENT_LIST_DIR}/src/host_socket.c)
target_link_options(app PUBLIC -Wl,--wrap=z_impl_zsock_shutdown -Wl,--wrap=z_impl_zsock_sendto)
target_link_libraries(app PRIVATE crux_web_adapter web_project_routes)
target_compile_options(app PRIVATE -Wall -Wextra -Werror)
crux_web_routes(app SOURCES src/probe_routes.c src/project_routes.c ZEPHYR_RESOURCES
  REVIEWED reviewed-routes.txt)
