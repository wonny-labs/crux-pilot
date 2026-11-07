# Standalone Linux Host composition for the provider-neutral PSA Crypto conformance cases
# (Issue #142). It is not part of the root Crux build and adds no dependency to it.
#
#   cmake -S tests/psa_crypto/host -B build/psa-host -G Ninja -DCRUX_PSA_PROVIDER=tf-psa-crypto
#
# The composition selects the provider; the cases never do. TF-PSA-Crypto comes through
# cmake/CruxPsaCrypto.cmake. wolfPSA and the wolfSSL it builds on come from a west workspace with
# the crux-secure-management group, because a Host build of them is only ever a test: they are named
# with CRUX_PSA_WOLFPSA_DIR and CRUX_PSA_WOLFSSL_DIR.
cmake_minimum_required(VERSION 3.28)
project(crux_psa_conformance LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CRUX_PSA_PROVIDER "" CACHE STRING
    "PSA Crypto provider: tf-psa-crypto, wolfpsa, stub-unsupported or stub-permissive")
set(CRUX_PSA_WOLFPSA_DIR "" CACHE PATH "wolfPSA source tree, for the wolfpsa provider")
set(CRUX_PSA_WOLFSSL_DIR "" CACHE PATH "wolfSSL source tree, for the wolfpsa provider")

if(CRUX_PSA_PROVIDER STREQUAL "")
    message(FATAL_ERROR "CRUX_PSA_PROVIDER is not set. Select tf-psa-crypto, wolfpsa, "
        "stub-unsupported or stub-permissive; the conformance cases do not choose a provider.")
endif()
if(NOT CRUX_PSA_PROVIDER MATCHES "^(tf-psa-crypto|wolfpsa|stub-unsupported|stub-permissive)$")
    message(FATAL_ERROR "Unknown CRUX_PSA_PROVIDER '${CRUX_PSA_PROVIDER}'")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/CruxPsaCrypto.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/../provider_declarations.cmake")
set(_crux_psa_declared "")
set(_crux_psa_deviations "")

set(_crux_psa_warnings -Wall -Wextra -Wpedantic -Werror)
set(CRUX_PSA_WOLFPSA_JOBS 4 CACHE STRING "Parallel jobs for the wolfPSA Makefile")

if(CRUX_PSA_PROVIDER STREQUAL "tf-psa-crypto")
    crux_psa_tf_psa_crypto_add()
    add_library(crux_psa_provider INTERFACE)
    target_link_libraries(crux_psa_provider INTERFACE tfpsacrypto)
    set(_crux_psa_declared "${CRUX_PSA_UNSUPPORTED_TF_PSA_CRYPTO}")
    set(_crux_psa_deviations "${CRUX_PSA_DEVIATIONS_TF_PSA_CRYPTO}")
elseif(CRUX_PSA_PROVIDER STREQUAL "wolfpsa")
    if(NOT EXISTS "${CRUX_PSA_WOLFPSA_DIR}/wolfpsa/psa/crypto.h" OR
       NOT EXISTS "${CRUX_PSA_WOLFSSL_DIR}/wolfssl/wolfcrypt/settings.h")
        message(FATAL_ERROR "The wolfpsa provider needs CRUX_PSA_WOLFPSA_DIR and "
            "CRUX_PSA_WOLFSSL_DIR, from a west workspace with the crux-secure-management group")
    endif()
    # wolfPSA's own Makefile and settings, unedited. Everything it writes goes in this build tree.
    set(_crux_wolfpsa_lib "${CMAKE_CURRENT_BINARY_DIR}/wolfpsa/libwolfpsa.a")
    add_custom_command(OUTPUT "${_crux_wolfpsa_lib}"
        COMMAND make -C "${CRUX_PSA_WOLFPSA_DIR}" -j "${CRUX_PSA_WOLFPSA_JOBS}" "${_crux_wolfpsa_lib}"
            "LIBNAME=${_crux_wolfpsa_lib}" "BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}/wolfpsa"
            "WOLFSSL_PATH=${CRUX_PSA_WOLFSSL_DIR}" "CC=${CMAKE_C_COMPILER}" "WARNFLAGS="
            "CFLAGS=-O2 ${CMAKE_C_FLAGS}"
        VERBATIM)
    add_custom_target(crux_wolfpsa_build DEPENDS "${_crux_wolfpsa_lib}")
    add_library(crux_psa_provider INTERFACE)
    add_dependencies(crux_psa_provider crux_wolfpsa_build)
    target_include_directories(crux_psa_provider SYSTEM INTERFACE
        "${CRUX_PSA_WOLFPSA_DIR}/wolfpsa" "${CRUX_PSA_WOLFPSA_DIR}" "${CRUX_PSA_WOLFSSL_DIR}")
    target_compile_definitions(crux_psa_provider INTERFACE WOLFSSL_USER_SETTINGS)
    target_link_libraries(crux_psa_provider INTERFACE "${_crux_wolfpsa_lib}" m)
    set(_crux_psa_declared "${CRUX_PSA_UNSUPPORTED_WOLFPSA}")
    set(_crux_psa_deviations "${CRUX_PSA_DEVIATIONS_WOLFPSA}")
else()
    # The stubs use only the provider's API headers, so no provider source is compiled.
    crux_psa_tf_psa_crypto_source(_crux_tf_psa_crypto_dir)
    add_library(crux_psa_provider STATIC ../stubs/stub.c)
    target_include_directories(crux_psa_provider SYSTEM PUBLIC
        "${_crux_tf_psa_crypto_dir}/include"
        "${_crux_tf_psa_crypto_dir}/drivers/builtin/include")
    if(CRUX_PSA_PROVIDER STREQUAL "stub-permissive")
        target_compile_definitions(crux_psa_provider PRIVATE CRUX_PSA_STUB_PERMISSIVE=1)
    else()
        target_compile_definitions(crux_psa_provider PRIVATE CRUX_PSA_STUB_PERMISSIVE=0)
    endif()
    target_compile_options(crux_psa_provider PRIVATE ${_crux_psa_warnings})
endif()

add_library(crux_psa_cases STATIC ../cases/psa_cases.c)
target_link_libraries(crux_psa_cases PUBLIC crux_psa_provider)
target_compile_definitions(crux_psa_cases PRIVATE
    "PSA_CASES_DECLARED_UNSUPPORTED=\"${_crux_psa_declared}\""
    "PSA_CASES_DECLARED_DEVIATIONS=\"${_crux_psa_deviations}\"")
target_compile_options(crux_psa_cases PRIVATE ${_crux_psa_warnings})

add_executable(psa_conformance main.c)
target_link_libraries(psa_conformance PRIVATE crux_psa_cases)
target_compile_definitions(psa_conformance PRIVATE CRUX_PSA_PROVIDER_NAME="${CRUX_PSA_PROVIDER}")
target_compile_options(psa_conformance PRIVATE ${_crux_psa_warnings})

# Controls for the declarations. scripts/test-psa-crypto.sh requires these to fail: one has no
# declarations, so each declared gap and deviation must show as a failure, and the other declares a
# passing case unsupported, so it must be reported as a stale declaration.
function(crux_psa_declaration_control name unsupported deviations)
    add_library(${name}_cases STATIC ../cases/psa_cases.c)
    target_link_libraries(${name}_cases PUBLIC crux_psa_provider)
    target_compile_definitions(${name}_cases PRIVATE
        "PSA_CASES_DECLARED_UNSUPPORTED=\"${unsupported}\""
        "PSA_CASES_DECLARED_DEVIATIONS=\"${deviations}\"")
    target_compile_options(${name}_cases PRIVATE ${_crux_psa_warnings})
    add_executable(${name} main.c)
    target_link_libraries(${name} PRIVATE ${name}_cases)
    target_compile_options(${name} PRIVATE ${_crux_psa_warnings})
endfunction()
crux_psa_declaration_control(psa_conformance_undeclared "" "")
crux_psa_declaration_control(psa_conformance_stale "init_is_idempotent" "")
# A declared deviation must cover only the status it names. The permissive stub accepts an all-zero
# signature, which no declaration may excuse, so this must fail.
crux_psa_declaration_control(psa_conformance_hidden "" "ecdsa_p256_zero_signature_invalid")

enable_testing()
add_test(NAME PsaCrypto.Conformance COMMAND psa_conformance)
set_tests_properties(PsaCrypto.Conformance PROPERTIES TIMEOUT 120)
