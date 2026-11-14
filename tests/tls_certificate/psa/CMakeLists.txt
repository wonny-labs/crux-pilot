# Standalone Linux Host composition of the module's PSA ports on TF-PSA-Crypto, with the real DER
# builders: a self-signed certificate, a request and the PKCS #8 key for a real P-256 key, checked
# with PSA and Mbed TLS's key parser here, and with OpenSSL by scripts/test-tls-certificate.sh. It is
# not part of the root Crux build and adds no dependency to it.
#
#   cmake -S tests/tls_certificate/psa -B build/tls-certificate-psa -G Ninja
cmake_minimum_required(VERSION 3.28)
project(crux_tls_certificate_psa LANGUAGES C)
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

get_filename_component(CRUX_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
include("${CRUX_ROOT}/cmake/CruxPsaCrypto.cmake")
crux_psa_tf_psa_crypto_add()

add_executable(tls_certificate_psa_cases psa_cases.c
    ${CRUX_ROOT}/components/tls_certificate/src/tls_x509.c
    ${CRUX_ROOT}/components/tls_certificate/ports/psa/tls_certificate_psa.c)
target_include_directories(tls_certificate_psa_cases PRIVATE
    ${CRUX_ROOT}/components/base/include
    ${CRUX_ROOT}/components/access/include
    ${CRUX_ROOT}/components/tls_certificate/include
    ${CRUX_ROOT}/components/tls_certificate/src)
target_link_libraries(tls_certificate_psa_cases PRIVATE tfpsacrypto)
target_compile_options(tls_certificate_psa_cases PRIVATE -Wall -Wextra -Wpedantic -Werror)
