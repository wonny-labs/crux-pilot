add_library(crux_reboot_api INTERFACE)
target_include_directories(crux_reboot_api INTERFACE include)
target_link_libraries(crux_reboot_api INTERFACE crux_access_api)

# The service is Zephyr-direct (pilot of ADR-0002 P9): its core and its sleep use k_mutex, k_sem,
# k_thread and k_msleep, so both sources live under ports/zephyr and no Host build compiles them. A
# Zephyr composition names these two files, the core and the sleep port, with CONFIG_CRUX_REBOOT_WORKER_PRIORITY and
# CONFIG_CRUX_REBOOT_WORKER_STACK_SIZE from zephyr/Kconfig. The Host builds the public interface
# and the two providers below, which call nothing of the kernel. Crux's own tests build the core on
# the Host over a test-only kernel stub (tests/reboot/host_kernel) and on native_sim over Zephyr.
set(CRUX_REBOOT_SOURCES "${CMAKE_CURRENT_LIST_DIR}/ports/zephyr/reboot_zephyr.c"
    CACHE INTERNAL "Zephyr-direct reboot service core")
set(CRUX_REBOOT_SLEEP_SOURCE "${CMAKE_CURRENT_LIST_DIR}/ports/zephyr/reboot_sleep_zephyr.c"
    CACHE INTERNAL "Zephyr-direct sleep port of the reboot service")

add_library(crux_reboot INTERFACE)
add_library(crux::reboot ALIAS crux_reboot)
target_link_libraries(crux_reboot INTERFACE crux_reboot_api)

# Thin providers, which a composition links by name. They are objects, so that a final link always
# takes them, and a second provider of a port is a duplicate symbol.
function(crux_reboot_provider target)
    add_library(${target} OBJECT ${ARGN})
    target_include_directories(${target} PRIVATE src)
    target_link_libraries(${target} PUBLIC crux_reboot_api)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
endfunction()

# The reset over crux_system_reset(), which does not return on a target. On the Host it returns
# CRUX_ERR_NOT_SUPPORTED and the request fails, so no Host image resets by this provider.
if(TARGET crux_system_api)
    crux_reboot_provider(crux_reboot_system_reset src/reboot_system_reset.c)
    target_link_libraries(crux_reboot_system_reset PUBLIC crux_system_api)
endif()

# The event port and the pending-events question, over the security events.
if(TARGET crux_security_event_api)
    crux_reboot_provider(crux_reboot_security_events src/reboot_events.c)
    target_link_libraries(crux_reboot_security_events PUBLIC crux_security_event_api)
endif()
