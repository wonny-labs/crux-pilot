# The reboot service on Zephyr (ADR-0004 phase 0, Experimental): the portable core, the sleep of its worker
# on Crux Sync, the reset over crux_system_reset(), and the providers of its event port and of the question
# whether events are still pending. The module's CMake adds this directory only under CONFIG_CRUX_REBOOT,
# which needs CONFIG_CRUX_ACCESS and refuses an image without Crux System, whose reset it calls.
#
# The policy is the application's: crux_reboot_start() refuses a configuration that states none. The
# worker's priority is the Kconfig symbol of ADR-0002 P3, CONFIG_CRUX_REBOOT_WORKER_PRIORITY, through
# crux_service_priority_placement.
#
# No crux:: target and no public header: see components/access/ports/zephyr/CMakeLists.txt. The sources are
# the ones the Host build lists in components/reboot/CMakeLists.txt, and
# tests/build/test_management_module.py holds the two lists together.
get_filename_component(CRUX_REBOOT_COMPONENT_DIR "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
get_filename_component(CRUX_COMPONENTS_DIR "${CRUX_REBOOT_COMPONENT_DIR}/.." ABSOLUTE)

if(NOT CONFIG_CRUX_REBOOT)
  message(FATAL_ERROR "This directory is the reboot selection of the module: set CONFIG_CRUX_REBOOT")
endif()

add_library(crux_reboot_zephyr STATIC
  ../../src/reboot.c
  ../../src/reboot_sleep.c
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
