# SPDX-License-Identifier: Apache-2.0
# Login and sessions (Issue #186) on native_sim: the session module and the web adapter over plain HTTP
# on loopback, on the management composition of #242 with the real Access core and its real PSA ports.
# The profile comes from EXTRA_CONF_FILE, as for the composition:
#   ../tls_offload_probe/mbed.conf;../management_composition/profile-psa.conf;
#   ../management_composition/mbed-threading.conf
# scripts/test-web-session.sh builds and runs it. TLS is Issue #185, and this image has none in use.
cmake_minimum_required(VERSION 3.20.0)
get_filename_component(CRUX_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
# The composition's file-backed disk.
set(DTC_OVERLAY_FILE "${CMAKE_CURRENT_LIST_DIR}/../management_composition/app.overlay")
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(crux_web_session_native LANGUAGES C)
if(NOT CONFIG_BOARD_NATIVE_SIM)
  message(FATAL_ERROR "This image is restricted to native_sim: its entropy is test-only")
endif()
include(${CRUX_ROOT}/cmake/CruxWebRoutes.cmake)
include(${CRUX_ROOT}/components/web/session/sources.cmake)
include(${CRUX_ROOT}/tests/native/management_composition/composition/composition.cmake)
crux_native_management_compose()
target_sources(app PRIVATE src/main.c src/test_routes.c ${CRUX_WEB_SESSION_SOURCES}
  ${CRUX_WEB_SESSION_ROUTES} ${CRUX_WEB_SESSION_PSA_PORT})
target_include_directories(app PRIVATE ${CRUX_WEB_SESSION_DIR} ${CRUX_ROOT}/components/web/src)
target_link_libraries(app PRIVATE crux_web_adapter)
if(CONFIG_CRUX_WEB_PLAIN_HTTP_FOR_TESTS)
  target_compile_definitions(app PRIVATE CRUX_WEB_SESSION_PLAIN_HTTP_FOR_TESTS=1)
endif()
crux_web_routes(app SOURCES ${CRUX_WEB_SESSION_ROUTES} src/test_routes.c ZEPHYR_RESOURCES
  REVIEWED reviewed-routes.txt)
