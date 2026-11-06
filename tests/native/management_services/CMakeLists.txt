# SPDX-License-Identifier: Apache-2.0
# Every management service on native_sim, on real PSA providers (Issue #242): Access, the credential
# store, security events, status and configuration from tests/native/management_composition, and the
# services of composition/services.cmake, behind the HTTPS adapter of #185. One image per provider
# profile, chosen with -DCRUX_WEB_PROFILE=mbed or wolf. The wolf profile needs a ZEPHYR_BASE with the
# socket patch: scripts/prepare-wolf-tls-zephyr.sh. scripts/test-native-composition.sh builds and runs
# it with driver.py. Test only: the entropy is test-only and the listener's certificate key is
# public. It operates no hardware.
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
# The HTTPS image's per-profile heap and arena, which its measurements sized.
list(APPEND profile_files "${CRUX_ROOT}/tests/native/web_https/${CRUX_WEB_PROFILE}.conf")
set(EXTRA_CONF_FILE "${profile_files}")
set(DTC_OVERLAY_FILE "${composition_dir}/app.overlay")
list(APPEND ZEPHYR_EXTRA_MODULES "${CRUX_ROOT}")
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(crux_native_management_services LANGUAGES C)
if(NOT CONFIG_BOARD_NATIVE_SIM)
  message(FATAL_ERROR "This image is restricted to native_sim, and it operates no hardware")
endif()
# This image declares itself a native_sim test image, which is what lets test entropy through the
# services composition's guard. No other build may set it.
set(CRUX_NATIVE_MANAGEMENT_TEST_IMAGE ON)
include("${composition_dir}/composition/composition.cmake")
include("${composition_dir}/composition/services.cmake")
crux_native_management_compose()
crux_native_management_compose_services()
include(${CRUX_ROOT}/cmake/CruxWebAssets.cmake)
include(${CRUX_ROOT}/cmake/CruxWebRoutes.cmake)
# The offload's FIN fix of #185, test image only: without it a client that closes TCP with no
# close_notify leaves the Mbed profile's server thread spinning. See docs/https-composition.md.
include(${CRUX_ROOT}/tests/native/web_https/nsos_rdhup.cmake)

set(route_sources ${CRUX_WEB_SESSION_ROUTES} ${CRUX_WEB_MANAGEMENT_ROUTES}
  ${CRUX_WEB_MANAGEMENT_NETWORK_ROUTES})
target_sources(app PRIVATE src/main.c ${route_sources})
# A one-page folder, as a product's frontend tool would leave it.
crux_web_assets(app FOLDER ${CMAKE_CURRENT_LIST_DIR}/site STYLE strict)
# Only the components' route sources: no test route. check-composition-image.py --services holds the
# linked table to exactly the routes of these three sources and the packaged folder.
crux_web_routes(app SOURCES ${route_sources} ZEPHYR_RESOURCES REVIEWED reviewed-routes.txt)
