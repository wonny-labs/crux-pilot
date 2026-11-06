cmake_minimum_required(VERSION 3.20.0)
# The composition selects the provider, through tf-psa-crypto.conf or wolfpsa.conf. Both are projects
# of the crux-secure-management west group. The cases need no Crux component.
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(crux_psa_conformance C)
target_sources(app PRIVATE src/main.c ../cases/psa_cases.c)
target_include_directories(app PRIVATE ../cases)
target_compile_options(app PRIVATE -Wall -Wextra -Werror)

include("${CMAKE_CURRENT_LIST_DIR}/../provider_declarations.cmake")
if(CONFIG_WOLFPSA)
    set(_crux_psa_declared "${CRUX_PSA_UNSUPPORTED_WOLFPSA}")
    set(_crux_psa_deviations "${CRUX_PSA_DEVIATIONS_WOLFPSA}")
    # The same wolfCrypt settings as components/ssh/ports/zephyr/wolf_ssh_settings.cmake, because a
    # management image shares one wolfCrypt between wolfPSA, TLS and wolfSSH, and the TLS socket
    # backend rejects CONFIG_WOLFSSL_SETTINGS_FILE.
    target_compile_definitions(wolfSSL INTERFACE
        WOLFSSL_WOLFSSH WOLFSSL_AES_COUNTER WOLFSSL_KEY_GEN WOLFSSL_ED25519_STREAMING_VERIFY
        ${CRUX_PSA_WOLFCRYPT_DEFINITIONS})
elseif(CONFIG_PSA_CRYPTO_PROVIDER_MBEDTLS)
    set(_crux_psa_declared "${CRUX_PSA_UNSUPPORTED_TF_PSA_CRYPTO}")
    set(_crux_psa_deviations "${CRUX_PSA_DEVIATIONS_TF_PSA_CRYPTO}")
    # Where the PSA source is the legacy RNG over an entropy driver, as on RT1060-EVKB, a failed read
    # must fail PSA. native_sim uses the external RNG, which already refuses a non-secure source.
    if(CONFIG_MBEDTLS_PSA_DRIVER_GET_ENTROPY)
        include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/CruxMbedtlsEntropy.cmake")
        crux_mbedtls_fail_closed_entropy()
    endif()
else()
    message(FATAL_ERROR "Select a PSA Crypto provider: EXTRA_CONF_FILE=tf-psa-crypto.conf or wolfpsa.conf")
endif()
target_compile_definitions(app PRIVATE
    "PSA_CASES_DECLARED_UNSUPPORTED=\"${_crux_psa_declared}\""
    "PSA_CASES_DECLARED_DEVIATIONS=\"${_crux_psa_deviations}\"")
