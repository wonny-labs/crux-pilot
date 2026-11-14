# Security events on Zephyr (ADR-0004 phase 0, Experimental): the portable core, the drain thread over
# Crux Logging, and the provider of the event port of Access. The module's CMake adds this directory
# only under CONFIG_CRUX_SECURITY_EVENT, which CONFIG_CRUX_ACCESS selects. The log provider is
# Crux Logging, so CONFIG_CRUX_LOGGING must be set: the module refuses a configuration without it.
#
# The ring and the drain rate are the Host build's defaults (components/security_event/CMakeLists.txt),
# fixed here until a product needs to tune them. The drain thread's priority is the Kconfig symbol of
# ADR-0002 P3, CONFIG_CRUX_SECURITY_EVENT_DRAIN_PRIORITY, through crux_service_priority_placement.
#
# This builds the library, registers it with the Zephyr link and publishes the portable target
# crux::security_event (ADR-0004 phase 1a, Experimental): see components/access/ports/zephyr/CMakeLists.txt.
# The sources are the ones the Host build lists in components/security_event/CMakeLists.txt, and
# tests/build/test_management_module.py holds the two lists together.
get_filename_component(CRUX_SECURITY_EVENT_COMPONENT_DIR "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
get_filename_component(CRUX_COMPONENTS_DIR "${CRUX_SECURITY_EVENT_COMPONENT_DIR}/.." ABSOLUTE)
include("${CRUX_COMPONENTS_DIR}/../cmake/CruxAccessBoundary.cmake")

# A portable usage target, like the Host build's. The public header includes <crux/access.h> (for the
# session type) and <crux/status.h>, so the target carries crux::access and crux::base.
function(crux_security_event_public_target)
  add_library(crux_security_event INTERFACE)
  add_library(crux::security_event ALIAS crux_security_event)
  target_include_directories(crux_security_event INTERFACE
    "${CRUX_SECURITY_EVENT_COMPONENT_DIR}/include")
  target_link_libraries(crux_security_event INTERFACE crux::base crux::access)
endfunction()

if(NOT CONFIG_CRUX_SECURITY_EVENT)
  message(FATAL_ERROR "This directory is the security events selection of the module: set CONFIG_CRUX_SECURITY_EVENT")
endif()

add_library(crux_security_event_zephyr STATIC
  ../../src/security_event.c
  ../../src/security_event_drain.c
  ../../src/security_event_fields.c
  ../../src/security_event_line.c
  ../../src/security_event_access_port.c
)
target_include_directories(crux_security_event_zephyr PRIVATE
  "${CRUX_SECURITY_EVENT_COMPONENT_DIR}/include"
  "${CRUX_SECURITY_EVENT_COMPONENT_DIR}/src"
  "${CRUX_COMPONENTS_DIR}/access/include"
  "${CRUX_COMPONENTS_DIR}/access/adapter"
  "${CRUX_COMPONENTS_DIR}/access/src"
  "${CRUX_COMPONENTS_DIR}/interrupt/src")
target_compile_definitions(crux_security_event_zephyr PRIVATE
  CRUX_SECURITY_EVENT_RING=32
  CRUX_SECURITY_EVENT_RESERVE=8
  CRUX_SECURITY_EVENT_DRAIN_BURST=4
  CRUX_SECURITY_EVENT_DRAIN_PAUSE_MS=100)
zephyr_append_cmake_library(crux_security_event_zephyr)
target_link_libraries(crux_security_event_zephyr PRIVATE zephyr_interface crux::base crux::logging
  crux::sync crux::thread crux::time crux_interrupt crux_service_priority_placement)
add_dependencies(crux_security_event_zephyr zephyr_generated_headers)
target_compile_options(crux_security_event_zephyr PRIVATE -Wall -Wextra -Werror)

crux_security_event_public_target()
cmake_language(DEFER CALL crux_assert_portable_usage crux::security_event)
cmake_language(DEFER CALL crux_assert_access_adapter_unreachable crux::security_event)
