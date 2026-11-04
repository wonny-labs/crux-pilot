# How a Linux Host build obtains the PSA Crypto provider. Issue #142 decided this: see
# docs/psa-crypto-qualification.md#decisions.
#
# Host builds have no west workspace, so TF-PSA-Crypto comes from a pinned archive through
# FetchContent, as GoogleTest does. The revision is the one west.yml pins for Zephyr builds, and
# scripts/test-psa-crypto.sh fails if the two differ. Nothing here runs unless a build includes
# this file and calls the function, so the default Crux build stays free of cryptography.
include_guard(GLOBAL)
include(FetchContent)

set(CRUX_TF_PSA_CRYPTO_REVISION dc575a2ddcc8cb16275d24c42a52eaf79ebe2231)
set(CRUX_TF_PSA_CRYPTO_SHA256
    1a122bcb91fc7c32060e64feeea29364a7cd894413e0ba89ee200fc5e384f95b)

# Sets <out_dir> to a TF-PSA-Crypto source tree at the pin, fetching the archive on first use. An
# offline build finds it in the FetchContent cache, or sets
# FETCHCONTENT_SOURCE_DIR_TF_PSA_CRYPTO. It adds no target: crux_psa_tf_psa_crypto_add() does that.
function(crux_psa_tf_psa_crypto_source out_dir)
    FetchContent_Declare(
        tf_psa_crypto
        URL "https://github.com/zephyrproject-rtos/tf-psa-crypto/archive/${CRUX_TF_PSA_CRYPTO_REVISION}.tar.gz"
        URL_HASH "SHA256=${CRUX_TF_PSA_CRYPTO_SHA256}"
    )
    FetchContent_GetProperties(tf_psa_crypto)
    if(NOT tf_psa_crypto_POPULATED)
        FetchContent_Populate(tf_psa_crypto)
    endif()
    set(${out_dir} "${tf_psa_crypto_SOURCE_DIR}" PARENT_SCOPE)
endfunction()

# Adds the provider as the static library target tfpsacrypto, built as upstream configures it, with
# none of Crux's warning options. Generated files are already present at this pin.
function(crux_psa_tf_psa_crypto_add)
    crux_psa_tf_psa_crypto_source(source_dir)
    set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
    set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(GEN_FILES OFF CACHE BOOL "" FORCE)
    set(TF_PSA_CRYPTO_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
    set(USE_STATIC_TF_PSA_CRYPTO_LIBRARY ON CACHE BOOL "" FORCE)
    set(USE_SHARED_TF_PSA_CRYPTO_LIBRARY OFF CACHE BOOL "" FORCE)
    add_subdirectory("${source_dir}" tf-psa-crypto EXCLUDE_FROM_ALL)
endfunction()
