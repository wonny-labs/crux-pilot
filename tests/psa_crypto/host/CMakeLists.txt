# Standalone Linux Host composition for the provider-neutral PSA Crypto conformance cases
# (Issue #142). It is not part of the root Crux build and adds no dependency to it.
#
#   cmake -S tests/psa_crypto/host -B build/psa-host -G Ninja \
#       -DCRUX_PSA_PROVIDER=tf-psa-crypto -DCRUX_PSA_TF_PSA_CRYPTO_DIR=<source tree>
#
# The composition selects the provider; the cases never do.
cmake_minimum_required(VERSION 3.28)
project(crux_psa_conformance LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CRUX_PSA_PROVIDER "" CACHE STRING
    "PSA Crypto provider: tf-psa-crypto, stub-unsupported or stub-permissive")
set(CRUX_PSA_TF_PSA_CRYPTO_DIR "" CACHE PATH
    "TF-PSA-Crypto source tree. The stub providers use only its PSA API headers.")

if(CRUX_PSA_PROVIDER STREQUAL "")
    message(FATAL_ERROR "CRUX_PSA_PROVIDER is not set. Select tf-psa-crypto, stub-unsupported "
        "or stub-permissive; the conformance cases do not choose a provider.")
endif()
if(NOT CRUX_PSA_PROVIDER MATCHES "^(tf-psa-crypto|stub-unsupported|stub-permissive)$")
    message(FATAL_ERROR "Unknown CRUX_PSA_PROVIDER '${CRUX_PSA_PROVIDER}'")
endif()
if(NOT EXISTS "${CRUX_PSA_TF_PSA_CRYPTO_DIR}/include/psa/crypto.h")
    message(FATAL_ERROR "CRUX_PSA_TF_PSA_CRYPTO_DIR must name a TF-PSA-Crypto source tree "
        "containing include/psa/crypto.h (got '${CRUX_PSA_TF_PSA_CRYPTO_DIR}')")
endif()

set(_crux_psa_warnings -Wall -Wextra -Wpedantic -Werror)

if(CRUX_PSA_PROVIDER STREQUAL "tf-psa-crypto")
    # Provider sources are built as upstream configures them; Crux warning options do not
    # propagate to them. Generated files are already present in the pinned tree.
    set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
    set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(GEN_FILES OFF CACHE BOOL "" FORCE)
    set(TF_PSA_CRYPTO_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
    set(USE_STATIC_TF_PSA_CRYPTO_LIBRARY ON CACHE BOOL "" FORCE)
    set(USE_SHARED_TF_PSA_CRYPTO_LIBRARY OFF CACHE BOOL "" FORCE)
    add_subdirectory("${CRUX_PSA_TF_PSA_CRYPTO_DIR}" tf-psa-crypto EXCLUDE_FROM_ALL)
    add_library(crux_psa_provider INTERFACE)
    target_link_libraries(crux_psa_provider INTERFACE tfpsacrypto)
else()
    add_library(crux_psa_provider STATIC ../stubs/stub.c)
    target_include_directories(crux_psa_provider SYSTEM PUBLIC
        "${CRUX_PSA_TF_PSA_CRYPTO_DIR}/include"
        "${CRUX_PSA_TF_PSA_CRYPTO_DIR}/drivers/builtin/include")
    if(CRUX_PSA_PROVIDER STREQUAL "stub-permissive")
        target_compile_definitions(crux_psa_provider PRIVATE CRUX_PSA_STUB_PERMISSIVE=1)
    else()
        target_compile_definitions(crux_psa_provider PRIVATE CRUX_PSA_STUB_PERMISSIVE=0)
    endif()
    target_compile_options(crux_psa_provider PRIVATE ${_crux_psa_warnings})
endif()

add_library(crux_psa_cases STATIC ../cases/psa_cases.c)
target_link_libraries(crux_psa_cases PUBLIC crux_psa_provider)
target_compile_options(crux_psa_cases PRIVATE ${_crux_psa_warnings})

add_executable(psa_conformance main.c)
target_link_libraries(psa_conformance PRIVATE crux_psa_cases)
target_compile_definitions(psa_conformance PRIVATE CRUX_PSA_PROVIDER_NAME="${CRUX_PSA_PROVIDER}")
target_compile_options(psa_conformance PRIVATE ${_crux_psa_warnings})

enable_testing()
add_test(NAME PsaCrypto.Conformance COMMAND psa_conformance)
set_tests_properties(PsaCrypto.Conformance PROPERTIES TIMEOUT 120)
