# Compile and link check of the web adapter for a target on each TLS profile. Never flashed or run.
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(crux_web_compile LANGUAGES C)
get_filename_component(CRUX_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
include(${CRUX_ROOT}/cmake/CruxWebRoutes.cmake)
target_sources(app PRIVATE src/main.c src/routes.c)
target_link_libraries(app PRIVATE crux_web_adapter)
target_compile_options(app PRIVATE -Wall -Wextra -Werror)
crux_web_routes(app SOURCES src/routes.c ZEPHYR_RESOURCES REVIEWED reviewed-routes.txt)

# The session module of #186 and its PSA ports, compiled for the target and not linked: an image also
# needs the Access core and its store, which a product composition brings.
include(${CRUX_ROOT}/components/web/session/sources.cmake)
# The PSA ports need a PSA provider, which the Mbed TLS profile has.
add_library(crux_web_session_compile OBJECT ${CRUX_WEB_SESSION_SOURCES} ${CRUX_WEB_SESSION_ROUTES})
if(CONFIG_MBEDTLS)
  target_sources(crux_web_session_compile PRIVATE ${CRUX_WEB_SESSION_PSA_PORT})
endif()
target_include_directories(crux_web_session_compile PRIVATE ${CRUX_WEB_SESSION_DIR}
  ${CRUX_ROOT}/components/web/src ${CRUX_ROOT}/components/web/adapter ${CRUX_ROOT}/components/web/include
  ${CRUX_ROOT}/components/access/include ${CRUX_ROOT}/components/access/adapter
  ${CRUX_ROOT}/components/base/include ${CRUX_ROOT}/components/sync/include
  ${CRUX_ROOT}/components/thread/include ${CRUX_ROOT}/components/time/include)
target_compile_definitions(crux_web_session_compile PRIVATE
  CRUX_WEB_BODY_BUFFER=${CONFIG_CRUX_WEB_BODY_BUFFER}u)
target_link_libraries(crux_web_session_compile PRIVATE zephyr_interface)
target_compile_options(crux_web_session_compile PRIVATE -Wall -Wextra -Werror)
