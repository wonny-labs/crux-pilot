# Access on Zephyr (ADR-0004 phase 0, Experimental): the portable core with its clock port over the
# Time contract and its derive, random and keyed hash ports over PSA Crypto. The module's CMake adds
# this directory only under CONFIG_CRUX_ACCESS, which also selects the credential store and the
# security events that provide the store and event ports of this core.
#
# This builds the library, registers it with the Zephyr link and publishes the portable target
# crux::access (ADR-0004 phase 1a, Experimental): an application links it for <crux/access.h> and
# names no private path. The sources are the ones the Host build lists in
# components/access/CMakeLists.txt, and tests/build/test_management_module.py holds the two lists
# together.
get_filename_component(CRUX_ACCESS_COMPONENT_DIR "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
include("${CRUX_ACCESS_COMPONENT_DIR}/../../cmake/CruxAccessBoundary.cmake")

# A portable usage target, like the Host build's: it carries the public include root and nothing else.
# The library below is registered with the Zephyr link by zephyr_append_cmake_library, so the target
# does not link it, and the adapter and src directories stay private to the library.
# tests/build/test_service_targets_boundary.py runs this definition against the real guards.
function(crux_access_public_target)
  add_library(crux_access INTERFACE)
  add_library(crux::access ALIAS crux_access)
  target_include_directories(crux_access INTERFACE "${CRUX_ACCESS_COMPONENT_DIR}/include")
endfunction()

if(NOT CONFIG_CRUX_ACCESS)
  message(FATAL_ERROR "This directory is the Access selection of the module: set CONFIG_CRUX_ACCESS")
endif()

add_library(crux_access_zephyr STATIC
  ../../src/access.c
  ../../src/access_accounts.c
  ../../src/access_delay.c
  ../../src/access_login.c
  ../../src/access_record.c
  ../../src/access_session.c
  ../../src/access_clock_time.c
  ../psa/access_psa.c
)
target_include_directories(crux_access_zephyr PRIVATE
  "${CRUX_ACCESS_COMPONENT_DIR}/include"
  "${CRUX_ACCESS_COMPONENT_DIR}/adapter"
  "${CRUX_ACCESS_COMPONENT_DIR}/src")
zephyr_append_cmake_library(crux_access_zephyr)
target_link_libraries(crux_access_zephyr PRIVATE zephyr_interface crux::base crux::sync crux::time)
add_dependencies(crux_access_zephyr zephyr_generated_headers)
target_compile_options(crux_access_zephyr PRIVATE -Wall -Wextra -Werror)

crux_access_public_target()
cmake_language(DEFER CALL crux_assert_portable_usage crux::access)
cmake_language(DEFER CALL crux_assert_access_adapter_unreachable crux::access)
