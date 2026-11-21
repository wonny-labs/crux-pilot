# The reboot service on Zephyr (ADR-0004 phase 0, Experimental): the core and the sleep of its worker, both
# Zephyr-direct over kernel objects and k_msleep() (pilot of ADR-0002 P9), the reset over crux_system_reset(), and the providers of its event port and of the question
# whether events are still pending. The module's CMake adds this directory only under CONFIG_CRUX_REBOOT,
# which needs CONFIG_CRUX_ACCESS and refuses an image without Crux System, whose reset it calls.
#
# The policy is the application's: crux_reboot_start() refuses a configuration that states none. The
# worker's priority is the Kconfig symbol of ADR-0002 P3, CONFIG_CRUX_REBOOT_WORKER_PRIORITY, through
# crux_service_priority_placement.
#
# Publishes the portable target crux::reboot. The core and the sleep are the two files that
# components/reboot/CMakeLists.txt names, CRUX_REBOOT_SOURCES and CRUX_REBOOT_SLEEP_SOURCE.
get_filename_component(CRUX_REBOOT_COMPONENT_DIR "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
get_filename_component(CRUX_COMPONENTS_DIR "${CRUX_REBOOT_COMPONENT_DIR}/.." ABSOLUTE)
include("${CRUX_COMPONENTS_DIR}/../cmake/CruxAccessBoundary.cmake")

# A portable usage target, like the Host build's (ADR-0004 phase 1a, Experimental; the owner approved the
# names on 2026-11-15): the public include root and the contracts that the header includes,
# and nothing private. The library below is registered with the Zephyr link by zephyr_append_cmake_library,
# so the target does not link it. tests/build/test_service_usage_targets.py runs this definition against
# the real guards.
function(crux_reboot_public_target)
  add_library(crux_reboot INTERFACE)
  add_library(crux::reboot ALIAS crux_reboot)
  target_include_directories(crux_reboot INTERFACE "${CRUX_REBOOT_COMPONENT_DIR}/include")
  target_link_libraries(crux_reboot INTERFACE crux::access)
endfunction()

if(NOT CONFIG_CRUX_REBOOT)
  message(FATAL_ERROR "This directory is the reboot selection of the module: set CONFIG_CRUX_REBOOT")
endif()

add_library(crux_reboot_zephyr STATIC
  reboot_zephyr.c
  reboot_sleep_zephyr.c
  ../../src/reboot_system_reset.c
  ../../src/reboot_events.c
)
target_include_directories(crux_reboot_zephyr PRIVATE
  "${CRUX_REBOOT_COMPONENT_DIR}/include"
  "${CRUX_REBOOT_COMPONENT_DIR}/src"
  "${CRUX_COMPONENTS_DIR}/access/include"
  "${CRUX_COMPONENTS_DIR}/configuration/include"
  "${CRUX_COMPONENTS_DIR}/security_event/include")
zephyr_append_cmake_library(crux_reboot_zephyr)
target_link_libraries(crux_reboot_zephyr PRIVATE zephyr_interface crux::base crux::sync crux::thread
  crux::system crux_service_priority_placement)
add_dependencies(crux_reboot_zephyr zephyr_generated_headers)
target_compile_options(crux_reboot_zephyr PRIVATE -Wall -Wextra -Werror)

crux_reboot_public_target()
cmake_language(DEFER CALL crux_assert_portable_usage crux::reboot)
cmake_language(DEFER CALL crux_assert_access_adapter_unreachable crux::reboot)
