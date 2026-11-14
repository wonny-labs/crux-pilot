# The credential store on Zephyr (ADR-0004 phase 0, Experimental): the portable core, the store port
# of Access, the AEAD and key ports over PSA Crypto, the device ID from hwinfo and the provider of the
# core's event port. The module's CMake adds this directory only under CONFIG_CRUX_CREDENTIAL, which
# CONFIG_CRUX_ACCESS selects. The store reads and writes two File roots, so a File provider must be
# enabled too: the module refuses a configuration that has none.
#
# This builds the library, registers it with the Zephyr link and publishes the portable target
# crux::credential_store (ADR-0004 phase 1a, Experimental): the store's state and its start, through
# <crux/credential_store.h>. It publishes none of the private adapter header that it reads (raw record
# read, write, key replace, erase and key parse stay private): see
# components/access/ports/zephyr/CMakeLists.txt. The sources are the ones the Host build lists in
# components/credential/CMakeLists.txt, and tests/build/test_management_module.py holds the two lists
# together.
get_filename_component(CRUX_CREDENTIAL_COMPONENT_DIR "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
get_filename_component(CRUX_COMPONENTS_DIR "${CRUX_CREDENTIAL_COMPONENT_DIR}/.." ABSOLUTE)
include("${CRUX_COMPONENTS_DIR}/../cmake/CruxCredentialBoundary.cmake")

# A portable usage target, like the Host build's. The public header includes <crux/file.h> for the two
# roots of the start call, so the target carries the File contract through crux::file, which the module
# requires for this symbol. The adapter, src and ports directories stay private to the library.
function(crux_credential_public_target)
  add_library(crux_credential_store INTERFACE)
  add_library(crux::credential_store ALIAS crux_credential_store)
  target_include_directories(crux_credential_store INTERFACE "${CRUX_CREDENTIAL_COMPONENT_DIR}/include")
  target_link_libraries(crux_credential_store INTERFACE crux::base crux::file)
endfunction()

if(NOT CONFIG_CRUX_CREDENTIAL)
  message(FATAL_ERROR "This directory is the credential store selection of the module: set CONFIG_CRUX_CREDENTIAL")
endif()

add_library(crux_credential_zephyr STATIC
  ../../src/credential_image.c
  ../../src/credential_store.c
  ../../access_store/credential_access_store.c
  ../psa/credential_psa.c
  credential_device_id.c
  ../../src/credential_security_events.c
)
target_include_directories(crux_credential_zephyr PRIVATE
  "${CRUX_CREDENTIAL_COMPONENT_DIR}/include"
  "${CRUX_CREDENTIAL_COMPONENT_DIR}/adapter"
  "${CRUX_CREDENTIAL_COMPONENT_DIR}/src"
  "${CRUX_CREDENTIAL_COMPONENT_DIR}/ports/psa"
  "${CRUX_COMPONENTS_DIR}/access/include"
  "${CRUX_COMPONENTS_DIR}/access/adapter"
  "${CRUX_COMPONENTS_DIR}/access/src"
  "${CRUX_COMPONENTS_DIR}/security_event/include")
zephyr_append_cmake_library(crux_credential_zephyr)
target_link_libraries(crux_credential_zephyr PRIVATE zephyr_interface crux::base crux::file crux::sync)
add_dependencies(crux_credential_zephyr zephyr_generated_headers)
target_compile_options(crux_credential_zephyr PRIVATE -Wall -Wextra -Werror)

crux_credential_public_target()
cmake_language(DEFER CALL crux_assert_portable_usage crux::credential_store)
cmake_language(DEFER CALL crux_assert_credential_adapter_unreachable crux::credential_store)
