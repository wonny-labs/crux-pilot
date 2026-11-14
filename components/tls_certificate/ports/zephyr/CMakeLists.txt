# The TLS certificate module on Zephyr (ADR-0004 phase 0, Experimental): the portable core, the X.509
# encoder, the hash, signature and random ports over PSA Crypto, and the provider of its event port. The
# module's CMake adds this directory only under CONFIG_CRUX_TLS_CERTIFICATE, which needs CONFIG_CRUX_ACCESS
# (and so the credential store that keeps the device key).
#
# It registers nothing with Zephyr's TLS credentials: the application takes the material and calls
# tls_credential_add itself (ADR-0004 C4, A1), so the key's bytes pass through application code and no
# Zephyr call is made here. The adapter header is private: it carries the device's private key as PKCS #8
# bytes, and cmake/CruxCredentialBoundary.cmake refuses any public target that reaches it, so this directory
# adds no target either.
#
# No crux:: target and no public header: see components/access/ports/zephyr/CMakeLists.txt. The sources are
# the ones the Host build lists in components/tls_certificate/CMakeLists.txt, plus the PSA port that the
# Host composition links, and tests/build/test_management_module.py holds the lists together.
get_filename_component(CRUX_TLS_CERTIFICATE_COMPONENT_DIR "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
get_filename_component(CRUX_COMPONENTS_DIR "${CRUX_TLS_CERTIFICATE_COMPONENT_DIR}/.." ABSOLUTE)

if(NOT CONFIG_CRUX_TLS_CERTIFICATE)
  message(FATAL_ERROR "This directory is the TLS certificate selection of the module: set CONFIG_CRUX_TLS_CERTIFICATE")
endif()

add_library(crux_tls_certificate_zephyr STATIC
  ../../src/tls_certificate.c
  ../../src/tls_x509.c
  ../../src/tls_certificate_events.c
  ../psa/tls_certificate_psa.c
)
target_include_directories(crux_tls_certificate_zephyr PRIVATE
  "${CRUX_TLS_CERTIFICATE_COMPONENT_DIR}/include"
  "${CRUX_TLS_CERTIFICATE_COMPONENT_DIR}/adapter"
  "${CRUX_TLS_CERTIFICATE_COMPONENT_DIR}/src"
  "${CRUX_COMPONENTS_DIR}/access/include"
  "${CRUX_COMPONENTS_DIR}/credential/include"
  "${CRUX_COMPONENTS_DIR}/credential/adapter"
  "${CRUX_COMPONENTS_DIR}/security_event/include")
zephyr_append_cmake_library(crux_tls_certificate_zephyr)
target_link_libraries(crux_tls_certificate_zephyr PRIVATE zephyr_interface crux::base crux::file crux::sync)
add_dependencies(crux_tls_certificate_zephyr zephyr_generated_headers)
target_compile_options(crux_tls_certificate_zephyr PRIVATE -Wall -Wextra -Werror)
