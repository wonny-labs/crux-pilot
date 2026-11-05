# Standalone Linux Host composition of the PSA providers of the Access and credential ports (Issue
# #242), on TF-PSA-Crypto, so that they run under sanitizers. It is not part of the root Crux build
# and adds no dependency to it. wolfPSA runs these cases on native_sim.
#
#   cmake -S tests/psa_ports/host -B build/psa-ports -G Ninja
cmake_minimum_required(VERSION 3.28)
project(crux_psa_ports LANGUAGES C)
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

get_filename_component(CRUX_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
include("${CRUX_ROOT}/cmake/CruxPsaCrypto.cmake")
crux_psa_tf_psa_crypto_add()

add_executable(psa_port_cases main.c ../psa_port_cases.c ../device_id_double.c
    ${CRUX_ROOT}/components/access/ports/psa/access_psa.c
    ${CRUX_ROOT}/components/credential/ports/psa/credential_psa.c)
target_include_directories(psa_port_cases PRIVATE ..
    ${CRUX_ROOT}/components/base/include
    ${CRUX_ROOT}/components/access/include ${CRUX_ROOT}/components/access/adapter
    ${CRUX_ROOT}/components/access/src
    ${CRUX_ROOT}/components/credential/include ${CRUX_ROOT}/components/credential/adapter
    ${CRUX_ROOT}/components/credential/src ${CRUX_ROOT}/components/credential/ports/psa
    ${CRUX_ROOT}/components/file/include)
target_link_libraries(psa_port_cases PRIVATE tfpsacrypto)
target_compile_definitions(psa_port_cases PRIVATE PSA_PORTS_HAS_ED25519=0)
target_compile_options(psa_port_cases PRIVATE -Wall -Wextra -Wpedantic -Werror)

# Controls, which scripts/test-native-composition.sh requires to fail: a provider that supports
# nothing and one that accepts everything with zeroed output must not pass these cases.
crux_psa_tf_psa_crypto_source(_crux_tf_psa_crypto_dir)
foreach(control IN ITEMS 0 1)
    add_executable(psa_port_cases_stub_${control} main.c ../psa_port_cases.c ../device_id_double.c
        ../../psa_crypto/stubs/stub.c
        ${CRUX_ROOT}/components/access/ports/psa/access_psa.c
        ${CRUX_ROOT}/components/credential/ports/psa/credential_psa.c)
    target_include_directories(psa_port_cases_stub_${control} PRIVATE ..
        ${CRUX_ROOT}/components/base/include
        ${CRUX_ROOT}/components/access/include ${CRUX_ROOT}/components/access/adapter
        ${CRUX_ROOT}/components/access/src
        ${CRUX_ROOT}/components/credential/include ${CRUX_ROOT}/components/credential/adapter
        ${CRUX_ROOT}/components/credential/src ${CRUX_ROOT}/components/credential/ports/psa
        ${CRUX_ROOT}/components/file/include)
    target_include_directories(psa_port_cases_stub_${control} SYSTEM PRIVATE
        "${_crux_tf_psa_crypto_dir}/include" "${_crux_tf_psa_crypto_dir}/drivers/builtin/include")
    target_compile_definitions(psa_port_cases_stub_${control} PRIVATE PSA_PORTS_HAS_ED25519=0
        CRUX_PSA_STUB_PERMISSIVE=${control})
endforeach()
