# SPDX-License-Identifier: Apache-2.0
# A native_sim application on the Wolf profile (wolfSSL's wolfCrypt under wolfPSA) whose entropy device can be made
# to fail, to show what the Crux PSA ports and the boot check of tests/zephyr/entropy_boot do with a failed read.
# It is the counterpart of tests/psa_entropy/zephyr, which does the same for the Mbed profile. wolfSSL is GPL,
# so this test stays out of default CI, as scripts/check-ssh-boundaries.py requires: scripts/test-wolf-entropy.sh
# builds and runs it. See docs/rt1060-wolf-entropy.md.
#
# CRUX_WOLF_ENTROPY_VARIANT (a -D at configuration):
#   runtime     the device is the chosen entropy source and can be made to fail at any moment (the default)
#   init-fails  the same, and the device does not initialise, so it is not ready
#   fallback    CONTROL: no entropy driver and a test generator. wolfCrypt then reads sys_rand_get().
#   guard       CONTROL: the same, with the boot check compiled in. It must NOT compile.
cmake_minimum_required(VERSION 3.20.0)
get_filename_component(CRUX_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
if(NOT CRUX_WOLF_ENTROPY_VARIANT)
    set(CRUX_WOLF_ENTROPY_VARIANT runtime)
endif()
set(EXTRA_CONF_FILE "${CRUX_ROOT}/tests/psa_crypto/zephyr/wolfpsa.conf")
if(CRUX_WOLF_ENTROPY_VARIANT STREQUAL runtime)
    set(DTC_OVERLAY_FILE "${CMAKE_CURRENT_LIST_DIR}/overlays/failing-device.overlay")
elseif(CRUX_WOLF_ENTROPY_VARIANT STREQUAL init-fails)
    set(DTC_OVERLAY_FILE "${CMAKE_CURRENT_LIST_DIR}/overlays/failing-device.overlay")
    list(APPEND EXTRA_CONF_FILE "${CMAKE_CURRENT_LIST_DIR}/init-fails.conf")
elseif(CRUX_WOLF_ENTROPY_VARIANT STREQUAL fallback OR CRUX_WOLF_ENTROPY_VARIANT STREQUAL guard)
    set(DTC_OVERLAY_FILE "${CMAKE_CURRENT_LIST_DIR}/overlays/no-chosen-entropy.overlay")
    list(APPEND EXTRA_CONF_FILE "${CMAKE_CURRENT_LIST_DIR}/control-fallback.conf")
else()
    message(FATAL_ERROR "CRUX_WOLF_ENTROPY_VARIANT is runtime, init-fails, fallback or guard")
endif()
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(crux_wolf_entropy LANGUAGES C)
if(NOT CONFIG_BOARD_NATIVE_SIM OR NOT CONFIG_WOLFPSA)
    message(FATAL_ERROR "native_sim and wolfPSA only: this is a test of the Wolf profile that never runs on a board")
endif()
include("${CRUX_ROOT}/tests/psa_crypto/provider_declarations.cmake")
set(component "${CRUX_ROOT}/components")
target_sources(app PRIVATE src/main.c src/test_entropy.c
    "${CRUX_ROOT}/tests/psa_ports/device_id_double.c"
    "${component}/access/ports/psa/access_psa.c"
    "${component}/credential/ports/psa/credential_psa.c"
    "${component}/tls_certificate/ports/psa/tls_certificate_psa.c"
    "${component}/web/ports/psa/web_session_psa.c")
if(NOT CRUX_WOLF_ENTROPY_VARIANT STREQUAL fallback)
    target_sources(app PRIVATE "${CRUX_ROOT}/tests/zephyr/entropy_boot/entropy_boot.c")
endif()
target_include_directories(app PRIVATE
    "${CRUX_ROOT}/tests/psa_ports" "${CRUX_ROOT}/tests/zephyr/entropy_boot"
    "${component}/base/include"
    "${component}/access/include" "${component}/access/adapter" "${component}/access/src"
    "${component}/credential/include" "${component}/credential/adapter" "${component}/credential/src"
    "${component}/credential/ports/psa" "${component}/file/include"
    "${component}/tls_certificate/include" "${component}/tls_certificate/adapter"
    "${component}/tls_certificate/src"
    "${component}/web/session" "${component}/web/adapter" "${component}/web/src")
target_compile_options(app PRIVATE -Wall -Wextra -Werror)
target_compile_definitions(app PRIVATE CRUX_WOLF_ENTROPY_VARIANT_NAME="${CRUX_WOLF_ENTROPY_VARIANT}")
if(CRUX_WOLF_ENTROPY_VARIANT STREQUAL fallback)
    target_compile_definitions(app PRIVATE CRUX_WOLF_ENTROPY_CONTROL=1)
endif()
# The same wolfCrypt settings as a management image: see tests/psa_ports/zephyr/CMakeLists.txt.
target_compile_definitions(wolfSSL INTERFACE
    WOLFSSL_WOLFSSH WOLFSSL_AES_COUNTER WOLFSSL_KEY_GEN WOLFSSL_ED25519_STREAMING_VERIFY
    ${CRUX_PSA_WOLFCRYPT_DEFINITIONS})
