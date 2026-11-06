add_library(crux_reboot_api INTERFACE)
target_include_directories(crux_reboot_api INTERFACE include)
target_link_libraries(crux_reboot_api INTERFACE crux_access_api)

set(CRUX_REBOOT_SOURCES src/reboot.c)
list(TRANSFORM CRUX_REBOOT_SOURCES PREPEND "${CMAKE_CURRENT_LIST_DIR}/")
set(CRUX_REBOOT_SOURCES "${CRUX_REBOOT_SOURCES}" CACHE INTERNAL "Portable reboot service sources")

# The core calls the private ports in src/reboot_port.h: the reset, the sleep, the events that are
# still pending, and the event port. No provider is linked here, so that an image links only once a
# composition chooses one, as for the network settings service. The fakes in tests/reboot are a test
# composition that exists only when Crux builds its own tests.
add_library(crux_reboot_core STATIC ${CRUX_REBOOT_SOURCES})
target_include_directories(crux_reboot_core PRIVATE src)
target_link_libraries(crux_reboot_core
    PUBLIC crux_reboot_api
    PRIVATE crux::access crux::sync crux::thread crux_configuration_text)
target_compile_options(crux_reboot_core PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_library(crux_reboot INTERFACE)
add_library(crux::reboot ALIAS crux_reboot)
target_link_libraries(crux_reboot INTERFACE crux_reboot_api crux_reboot_core)

# Thin providers, which a composition links by name. They are objects, so that a final link always
# takes them, and a second provider of a port is a duplicate symbol.
function(crux_reboot_provider target)
    add_library(${target} OBJECT ${ARGN})
    target_include_directories(${target} PRIVATE src)
    target_link_libraries(${target} PUBLIC crux_reboot_api)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
endfunction()

# The sleep of the worker, on Sync's semaphore: one that is never given, so that taking it with a
# time limit is a sleep.
crux_reboot_provider(crux_reboot_sleep src/reboot_sleep.c)
target_link_libraries(crux_reboot_sleep PUBLIC crux::sync)

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
