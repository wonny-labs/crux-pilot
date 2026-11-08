# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(crux_web_assets_native LANGUAGES C)
if(NOT CONFIG_BOARD_NATIVE_SIM)
  message(FATAL_ERROR "This suite is restricted to native_sim, and it operates no hardware")
endif()
get_filename_component(CRUX_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
include(${CRUX_ROOT}/cmake/CruxWebAssets.cmake)
include(${CRUX_ROOT}/cmake/CruxWebRoutes.cmake)

# The built folder, which a product's frontend tool would make. Here a script does, with a file that
# fits the response deadline, one that does not compress, and a large one that nearly fills it.
set(WEB_FOLDER ${CMAKE_CURRENT_BINARY_DIR}/site)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
execute_process(COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/make_site.py ${WEB_FOLDER}
  RESULT_VARIABLE site_result)
if(NOT site_result EQUAL 0)
  message(FATAL_ERROR "make_site.py failed")
endif()

# The sources of tests/native/web_http that this image shares: the Access double, and the socket shim
# that gives the server's inactivity timer its effect on the native socket offload.
set(WEB_HTTP_SRC ${CMAKE_CURRENT_LIST_DIR}/../web_http/src)
target_sources(app PRIVATE src/main.c src/api_route.c src/probe_route.c ${WEB_HTTP_SRC}/access_double.c
  ${WEB_HTTP_SRC}/socket_shim.c)
target_sources(native_simulator INTERFACE ${WEB_HTTP_SRC}/host_socket.c)
target_include_directories(app PRIVATE ${WEB_HTTP_SRC})
target_link_options(app PUBLIC -Wl,--wrap=z_impl_zsock_shutdown -Wl,--wrap=z_impl_zsock_sendto)
target_link_libraries(app PRIVATE crux_web_adapter)
target_compile_options(app PRIVATE -Wall -Wextra -Werror)

# All a project writes for its interface: the folder, and a content policy if it needs one. main.c holds
# no asset code. The deadline comes from CONFIG_CRUX_WEB_RESPONSE_DEADLINE_MS.
crux_web_assets(app FOLDER ${WEB_FOLDER} STYLE strict)
crux_web_routes(app SOURCES src/api_route.c src/probe_route.c ZEPHYR_RESOURCES REVIEWED reviewed-routes.txt)
