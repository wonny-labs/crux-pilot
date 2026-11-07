# SPDX-License-Identifier: Apache-2.0
# One native_sim image with HTTPS web management, the command line over SSH and SFTP (Issue #197). It is the
# services image of tests/native/management_services, with the wolfSSH provider beside the HTTPS listener,
# all on the real PSA providers of the Wolf profile: wolfSSH needs wolfSSL, and the Mbed profile has no SSH
# provider. It needs a ZEPHYR_BASE with the socket patch: scripts/prepare-wolf-tls-zephyr.sh, and the
# crux-secure-management west group. scripts/test-ssh-integrated.sh builds it and drives it with OpenSSH
# and curl. TEST ONLY: the entropy is test-only, the listener's certificate key is public, and the host key
# is held in test form. The image declares itself a native_sim test image, which no other build may do. It
# operates no hardware. See docs/ssh-integrated-image.md.
cmake_minimum_required(VERSION 3.20.0)
get_filename_component(CRUX_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(composition_dir "${CRUX_ROOT}/tests/native/management_composition")
set(services_dir "${CRUX_ROOT}/tests/native/management_services")
# The Wolf profile only, as the SSH composition states.
set(profile_files
    "${services_dir}/prj.conf"
    "${CRUX_ROOT}/tests/native/tls_offload_probe/wolf.conf"
    "${composition_dir}/profile-psa.conf"
    "${CRUX_ROOT}/tests/native/web_https/wolf.conf"
    "${CRUX_ROOT}/components/ssh/ports/zephyr/wolf-ssh.conf"
    # Last, so that its arena replaces the HTTPS image's.
    "${CMAKE_CURRENT_LIST_DIR}/ssh.conf")
# Only for the controls of scripts/test-ssh-integrated.sh: a file that must stop the configuration.
if(CRUX_SSH_TEST_EXTRA_CONF)
  list(APPEND profile_files "${CRUX_SSH_TEST_EXTRA_CONF}")
endif()
set(EXTRA_CONF_FILE "${profile_files}")
set(DTC_OVERLAY_FILE "${composition_dir}/app.overlay")
list(APPEND ZEPHYR_EXTRA_MODULES "${CRUX_ROOT}")
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(crux_native_management_ssh LANGUAGES C)
if(NOT CONFIG_BOARD_NATIVE_SIM)
  message(FATAL_ERROR "This image is restricted to native_sim, and it operates no hardware")
endif()
# This image declares itself a native_sim test image, which is what lets test entropy through the services
# composition's guard and the test host key through the SSH composition's. No other build may set it.
set(CRUX_NATIVE_MANAGEMENT_TEST_IMAGE ON)
include("${composition_dir}/composition/composition.cmake")
include("${composition_dir}/composition/services.cmake")
include("${composition_dir}/composition/ssh.cmake")
crux_native_management_compose()
crux_native_management_compose_services()
crux_native_management_compose_ssh()
include(${CRUX_ROOT}/cmake/CruxWebAssets.cmake)
include(${CRUX_ROOT}/cmake/CruxWebRoutes.cmake)
# The offload's FIN fix of #185, test image only, as in the services image.
include(${CRUX_ROOT}/tests/native/web_https/nsos_rdhup.cmake)

set(route_sources ${CRUX_WEB_SESSION_ROUTES} ${CRUX_WEB_MANAGEMENT_ROUTES}
  ${CRUX_WEB_MANAGEMENT_NETWORK_ROUTES})
# The services image's main.c, which starts SSH when CRUX_NATIVE_MANAGEMENT_SSH is defined, so that every
# case and route of that image runs here too, with the SSH listener and threads present.
target_sources(app PRIVATE "${services_dir}/src/main.c" ${route_sources})
# Its own small folder: a login form, the status and one configuration value through the real API, for a person
# to watch in a browser. As the services image, it has no test route.
crux_web_assets(app FOLDER ${CMAKE_CURRENT_LIST_DIR}/site STYLE strict)
# Exactly the components' routes: SSH adds none.
crux_web_routes(app SOURCES ${route_sources} ZEPHYR_RESOURCES REVIEWED ${services_dir}/reviewed-routes.txt)
