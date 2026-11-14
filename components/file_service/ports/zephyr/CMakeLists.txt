# The file service on Zephyr (ADR-0004 phase 0, Experimental): the portable core, which reaches the files
# only through the Crux File contract, and the provider of its event port over the security events. The
# module's CMake adds this directory only under CONFIG_CRUX_FILE_SERVICE, which needs CONFIG_CRUX_ACCESS (and
# so a Crux File provider, which that symbol's configuration check already requires).
#
# No crux:: target and no public header: see components/access/ports/zephyr/CMakeLists.txt. The sources are
# the ones the Host build lists in components/file_service/CMakeLists.txt, and
# tests/build/test_management_module.py holds the two lists together.
get_filename_component(CRUX_FILE_SERVICE_COMPONENT_DIR "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
get_filename_component(CRUX_COMPONENTS_DIR "${CRUX_FILE_SERVICE_COMPONENT_DIR}/.." ABSOLUTE)

if(NOT CONFIG_CRUX_FILE_SERVICE)
  message(FATAL_ERROR "This directory is the file service selection of the module: set CONFIG_CRUX_FILE_SERVICE")
endif()

add_library(crux_file_service_zephyr STATIC
  ../../src/file_service.c
  ../../src/file_service_security_events.c
)
target_include_directories(crux_file_service_zephyr PRIVATE
  "${CRUX_FILE_SERVICE_COMPONENT_DIR}/include"
  "${CRUX_FILE_SERVICE_COMPONENT_DIR}/src"
  "${CRUX_COMPONENTS_DIR}/access/include"
  "${CRUX_COMPONENTS_DIR}/security_event/include")
zephyr_append_cmake_library(crux_file_service_zephyr)
target_link_libraries(crux_file_service_zephyr PRIVATE zephyr_interface crux::base crux::file crux::sync)
add_dependencies(crux_file_service_zephyr zephyr_generated_headers)
target_compile_options(crux_file_service_zephyr PRIVATE -Wall -Wextra -Werror)
