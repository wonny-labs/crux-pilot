set(CRUX_SECURITY_EVENT_SOURCES
    src/security_event.c
    src/security_event_drain.c
    src/security_event_fields.c
    src/security_event_line.c
)
list(TRANSFORM CRUX_SECURITY_EVENT_SOURCES PREPEND "${CMAKE_CURRENT_LIST_DIR}/")
set(CRUX_SECURITY_EVENT_SOURCES "${CRUX_SECURITY_EVENT_SOURCES}" CACHE INTERNAL
    "Portable Security event sources")

# Build settings that the composition sets. The core checks them, and it fails to compile on a reserve
# that leaves routine events no room. The burst and pause must suit the composition's log buffer.
set(CRUX_SECURITY_EVENT_RING 32 CACHE STRING "Records the event ring holds")
set(CRUX_SECURITY_EVENT_RESERVE 8 CACHE STRING "Ring records that only change-class events may use")
set(CRUX_SECURITY_EVENT_DRAIN_BURST 4 CACHE STRING "Log lines the drain writes before it pauses")
set(CRUX_SECURITY_EVENT_DRAIN_PAUSE_MS 100 CACHE STRING "Pause of the drain after a full burst")

add_library(crux_security_event_api INTERFACE)
target_include_directories(crux_security_event_api INTERFACE include)
# The public header names the session type only, so a test double can use it with no access core.
target_link_libraries(crux_security_event_api INTERFACE crux::base crux_access_api)

add_library(crux_security_event_core STATIC ${CRUX_SECURITY_EVENT_SOURCES})
target_include_directories(crux_security_event_core PRIVATE src ../interrupt/src)
target_link_libraries(crux_security_event_core
    PUBLIC crux_security_event_api crux::access
    PRIVATE crux::logging crux::sync crux::thread crux::time crux_interrupt)
target_compile_definitions(crux_security_event_core PRIVATE
    CRUX_SECURITY_EVENT_RING=${CRUX_SECURITY_EVENT_RING}
    CRUX_SECURITY_EVENT_RESERVE=${CRUX_SECURITY_EVENT_RESERVE}
    CRUX_SECURITY_EVENT_DRAIN_BURST=${CRUX_SECURITY_EVENT_DRAIN_BURST}
    CRUX_SECURITY_EVENT_DRAIN_PAUSE_MS=${CRUX_SECURITY_EVENT_DRAIN_PAUSE_MS})
target_compile_options(crux_security_event_core PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_library(crux_security_event INTERFACE)
add_library(crux::security_event ALIAS crux_security_event)
target_link_libraries(crux_security_event INTERFACE crux_security_event_api crux_security_event_core)

# The provider of crux_access_port_event. crux_access does not link this component, so the targets
# have no cycle, but a final link goes through the provider in both directions: Access calls the
# port, and the read path calls crux_access_check. Objects resolve that for a composition, and this
# target is what it adds. It is not a public target, and it reaches Access's private port header.
add_library(crux_security_event_access_port OBJECT src/security_event_access_port.c)
target_include_directories(crux_security_event_access_port PRIVATE ../access/src)
target_link_libraries(crux_security_event_access_port PRIVATE crux_access_adapter crux_security_event_api)
target_compile_options(crux_security_event_access_port PRIVATE -Wall -Wextra -Wpedantic -Werror)
