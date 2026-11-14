# Access on Zephyr (ADR-0004 phase 0, Experimental): the portable core with its clock port over the
# Time contract and its derive, random and keyed hash ports over PSA Crypto. The module's CMake adds
# this directory only under CONFIG_CRUX_ACCESS, which also selects the credential store and the
# security events that provide the store and event ports of this core.
#
# This builds the library and registers it with the Zephyr link. It adds no crux:: target and no
# public header: an image that enables the symbol links the core, and an application reaches the
# public <crux/access.h> through its own include path until the composition interfaces of ADR-0004
# phase 1a exist. The sources are the ones the Host build lists in components/access/CMakeLists.txt,
# and tests/build/test_management_module.py holds the two lists together.
get_filename_component(CRUX_ACCESS_COMPONENT_DIR "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

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
