# Security events on Zephyr (ADR-0004 phase 0, Experimental): the portable core, the drain thread over
# Crux Logging, and the provider of the event port of Access. The module's CMake adds this directory
# only under CONFIG_CRUX_SECURITY_EVENT, which CONFIG_CRUX_ACCESS selects. The log provider is
# Crux Logging, so CONFIG_CRUX_LOGGING must be set: the module refuses a configuration without it.
#
# The ring and the drain rate are the Host build's defaults (components/security_event/CMakeLists.txt),
# fixed here until a product needs to tune them. The drain thread's priority is the Kconfig symbol of
# ADR-0002 P3, CONFIG_CRUX_SECURITY_EVENT_DRAIN_PRIORITY, through crux_service_priority_placement.
#
# This builds the library and registers it with the Zephyr link. It adds no crux:: target and no
# public header: see components/access/ports/zephyr/CMakeLists.txt. The sources are the ones the Host
# build lists in components/security_event/CMakeLists.txt, and tests/build/test_management_module.py
# holds the two lists together.
get_filename_component(CRUX_SECURITY_EVENT_COMPONENT_DIR "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
get_filename_component(CRUX_COMPONENTS_DIR "${CRUX_SECURITY_EVENT_COMPONENT_DIR}/.." ABSOLUTE)

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
