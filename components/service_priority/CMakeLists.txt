# Private implementation only, like components/interrupt: no crux:: alias and no public header. Zephyr
# only: the Crux module adds this directory, and the Linux Host builds nothing here.
#
# crux_service_priority_placement carries the private include directory and
# CRUX_SERVICE_PRIORITY_PLACEMENT. A composition or test that compiles one of the services' worker
# sources links it to the target that compiles them, so that the worker places itself at its
# CONFIG_CRUX_<SERVICE>_<ROLE>_PRIORITY (ADR-0002 P3). tests/service_priority/check_placement.py fails
# a Zephyr CMake file that lists a worker source without it. The services' Host builds call nothing.
add_library(crux_service_priority_placement INTERFACE)
target_include_directories(crux_service_priority_placement INTERFACE "${CMAKE_CURRENT_LIST_DIR}/src")
target_compile_definitions(crux_service_priority_placement INTERFACE CRUX_SERVICE_PRIORITY_PLACEMENT=1)

# The placement itself needs Crux Thread's Kconfig, as only Thread workers call it. A compile-only
# check of the sources without Thread links the interface above and no implementation.
if(CONFIG_CRUX_THREAD)
    add_library(crux_service_priority STATIC ports/zephyr/service_priority_zephyr.c)
    target_include_directories(crux_service_priority PRIVATE src)
    target_link_libraries(crux_service_priority PRIVATE zephyr_interface)
    zephyr_append_cmake_library(crux_service_priority)
    target_compile_options(crux_service_priority PRIVATE -Wall -Wextra -Werror)
endif()
