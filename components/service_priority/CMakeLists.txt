# Private implementation only, like components/interrupt: no public alias and no include interface.
# Zephyr only. The Crux module adds it with Crux Thread, and gives the services' sources, wherever a
# composition compiles them, CRUX_SERVICE_PRIORITY_PLACEMENT and this src directory. The Linux Host
# builds nothing here, and the services' Host builds call nothing.
if(NOT CONFIG_CRUX_THREAD)
    message(FATAL_ERROR "The service priority placement is part of the Zephyr Crux Thread composition")
endif()
add_library(crux_service_priority STATIC ports/zephyr/service_priority_zephyr.c)
target_include_directories(crux_service_priority PRIVATE src)
target_link_libraries(crux_service_priority PRIVATE zephyr_interface)
zephyr_append_cmake_library(crux_service_priority)
target_compile_options(crux_service_priority PRIVATE -Wall -Wextra -Werror)
