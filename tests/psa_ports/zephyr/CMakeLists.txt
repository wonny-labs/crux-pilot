# SPDX-License-Identifier: Apache-2.0
# The PSA providers of the Access and credential ports on native_sim, with the provider from
# tests/psa_crypto/zephyr/tf-psa-crypto.conf or wolfpsa.conf. Compiled for a board, it is compile only.
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(crux_psa_ports_zephyr LANGUAGES C)
get_filename_component(CRUX_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
include("${CRUX_ROOT}/tests/psa_crypto/provider_declarations.cmake")
target_sources(app PRIVATE src/main.c ../psa_port_cases.c
    ${CRUX_ROOT}/components/access/ports/psa/access_psa.c
    ${CRUX_ROOT}/components/credential/ports/psa/credential_psa.c)
target_include_directories(app PRIVATE ..
    ${CRUX_ROOT}/components/base/include
    ${CRUX_ROOT}/components/access/include ${CRUX_ROOT}/components/access/adapter
    ${CRUX_ROOT}/components/access/src
    ${CRUX_ROOT}/components/credential/include ${CRUX_ROOT}/components/credential/adapter
    ${CRUX_ROOT}/components/credential/src ${CRUX_ROOT}/components/credential/ports/psa
    ${CRUX_ROOT}/components/file/include)
target_compile_options(app PRIVATE -Wall -Wextra -Werror)
# A test controls the device on native_sim. For a board, this is compile only, so it gets the real one.
if(CONFIG_BOARD_NATIVE_SIM)
    target_sources(app PRIVATE ../device_id_double.c)
else()
    target_sources(app PRIVATE ../device_id_board.c
        ${CRUX_ROOT}/components/credential/ports/zephyr/credential_device_id.c)
endif()
if(CONFIG_WOLFPSA)
    # The same wolfCrypt settings as a management image: see tests/psa_crypto/zephyr/CMakeLists.txt.
    target_compile_definitions(wolfSSL INTERFACE
        WOLFSSL_WOLFSSH WOLFSSL_AES_COUNTER WOLFSSL_KEY_GEN WOLFSSL_ED25519_STREAMING_VERIFY
        ${CRUX_PSA_WOLFCRYPT_DEFINITIONS})
    target_compile_definitions(app PRIVATE PSA_PORTS_HAS_ED25519=1)
elseif(CONFIG_PSA_CRYPTO_PROVIDER_MBEDTLS)
    target_compile_definitions(app PRIVATE PSA_PORTS_HAS_ED25519=0)
else()
    message(FATAL_ERROR "Select a PSA Crypto provider: tf-psa-crypto.conf or wolfpsa.conf")
endif()
