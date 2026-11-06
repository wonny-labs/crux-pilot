# SPDX-License-Identifier: Apache-2.0
# The services image of Issue #242 with a built web example packaged in it, for the browser cases of
# Issue #220. The same composition, providers, routes and HTTPS listener as ../CMakeLists.txt; what
# differs is declared in src/main.c and e2e.conf: the accounts and parameters that the shared browser
# cases were written for, a shortened idle limit, the default response deadline, and the folder.
#
#   -DCRUX_WEB_PROFILE=mbed|wolf   the provider profile, as for the services image
#   -DCRUX_E2E_FOLDER=<dir>        the built example's folder, which the runner names
#   -DCRUX_E2E_STYLE=strict|inline the content policy: strict for Preact, inline for Angular
#
# scripts/test-native-composition.sh e2e-mbed and e2e-wolf build and run it. Test only: the entropy
# is test-only and the listener's certificate key is public. It operates no hardware.
cmake_minimum_required(VERSION 3.20.0)
get_filename_component(CRUX_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
set(composition_dir "${CRUX_ROOT}/tests/native/management_composition")
set(CRUX_WEB_PROFILE mbed CACHE STRING "Provider profile: mbed or wolf")
set(CRUX_E2E_FOLDER "" CACHE PATH "The built web example to package")
set(CRUX_E2E_STYLE strict CACHE STRING "Content policy for styles: strict or inline")
# Declared for this image only: a session idle for 30 s ends, so that the browser cases can meet the
# real session clock. The composition's own limit is 15 minutes.
set(CRUX_E2E_SESSION_IDLE_S 30 CACHE STRING "Idle limit of a session, in seconds")
if(NOT CRUX_E2E_FOLDER)
  message(FATAL_ERROR "Name the built example with -DCRUX_E2E_FOLDER=<dir>")
endif()
set(profile_files "${CRUX_ROOT}/tests/native/tls_offload_probe/${CRUX_WEB_PROFILE}.conf"
  "${composition_dir}/profile-psa.conf")
if(CRUX_WEB_PROFILE STREQUAL "mbed")
  list(APPEND profile_files "${composition_dir}/mbed-threading.conf")
elseif(NOT CRUX_WEB_PROFILE STREQUAL "wolf")
  message(FATAL_ERROR "CRUX_WEB_PROFILE must be mbed or wolf")
endif()
list(APPEND profile_files "${CRUX_ROOT}/tests/native/web_https/${CRUX_WEB_PROFILE}.conf")
set(CONF_FILE "${CMAKE_CURRENT_LIST_DIR}/../prj.conf;${CMAKE_CURRENT_LIST_DIR}/e2e.conf")
set(EXTRA_CONF_FILE "${profile_files}")
set(DTC_OVERLAY_FILE "${composition_dir}/app.overlay")
list(APPEND ZEPHYR_EXTRA_MODULES "${CRUX_ROOT}")
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(crux_native_management_e2e LANGUAGES C)
if(NOT CONFIG_BOARD_NATIVE_SIM)
  message(FATAL_ERROR "This image is restricted to native_sim, and it operates no hardware")
endif()
set(CRUX_NATIVE_MANAGEMENT_TEST_IMAGE ON)
include("${composition_dir}/composition/composition.cmake")
include("${composition_dir}/composition/services.cmake")
crux_native_management_compose()
crux_native_management_compose_services()
include(${CRUX_ROOT}/cmake/CruxWebAssets.cmake)
include(${CRUX_ROOT}/cmake/CruxWebRoutes.cmake)
include(${CRUX_ROOT}/tests/native/web_https/nsos_rdhup.cmake)

set(route_sources ${CRUX_WEB_SESSION_ROUTES} ${CRUX_WEB_MANAGEMENT_ROUTES}
  ${CRUX_WEB_MANAGEMENT_NETWORK_ROUTES})
target_sources(app PRIVATE src/main.c src/offload_shutdown.c ${route_sources})
# The offload has no shutdown, so the inactivity timer would do nothing here. See src/offload_shutdown.c.
target_sources(native_simulator INTERFACE ${CMAKE_CURRENT_LIST_DIR}/src/offload_shutdown_host.c)
target_link_options(app PUBLIC -Wl,--wrap=z_impl_zsock_shutdown)
target_compile_definitions(app PRIVATE CRUX_E2E_SESSION_IDLE_S=${CRUX_E2E_SESSION_IDLE_S}u)
# The built example, as a product would name it. Nothing else differs between the two examples.
crux_web_assets(app FOLDER ${CRUX_E2E_FOLDER} STYLE ${CRUX_E2E_STYLE})
# Only the components' route sources: check-composition-image.py --services holds the linked table to
# exactly these and the packaged folder, as for the services image.
crux_web_routes(app SOURCES ${route_sources} ZEPHYR_RESOURCES REVIEWED reviewed-routes.txt)
