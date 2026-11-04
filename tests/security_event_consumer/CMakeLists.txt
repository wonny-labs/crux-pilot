cmake_minimum_required(VERSION 3.28)
project(crux_security_event_consumer LANGUAGES C CXX)
if(NOT CRUX_SOURCE_DIR)
    get_filename_component(CRUX_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
endif()
# Selects what scripts/check-security-event-boundary.py asks this project to do. See that script for
# each probe's expected outcome.
set(SECURITY_EVENT_PROBE "consumer" CACHE STRING "Boundary probe")
foreach(required IN ITEMS SYNC THREAD TIME LOGGING ACCESS SECURITY_EVENT)
    set(CRUX_BUILD_${required} ON CACHE BOOL "Required by Security events")
endforeach()
set(CRUX_TIME_PROVIDER memory CACHE STRING "Controlled Time, enough for a consumer build")
add_subdirectory("${CRUX_SOURCE_DIR}" crux)
include("${CRUX_SOURCE_DIR}/cmake/CruxAccessBoundary.cmake")
crux_assert_portable_usage(crux::security_event)
crux_assert_access_adapter_unreachable(crux::security_event)

# Neither a fake nor the instrumented core may exist in a build that a product could use.
foreach(forbidden IN ITEMS crux_security_event_fake security_event_test_core
                           security_event_access_fakes)
    if(TARGET ${forbidden})
        message(FATAL_ERROR "${forbidden} must not exist outside Crux's own tests")
    endif()
endforeach()

if(SECURITY_EVENT_PROBE STREQUAL "consumer")
    # A project: includes the public header, and compiles as C17 and C++17.
    add_library(security_event_consumer_c OBJECT consumer.c)
    add_library(security_event_consumer_cpp OBJECT consumer.cpp)
    foreach(target IN ITEMS security_event_consumer_c security_event_consumer_cpp)
        target_link_libraries(${target} PRIVATE crux::security_event)
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
        crux_assert_portable_consumer(${target})
        crux_assert_access_consumer(${target})
    endforeach()
    set_target_properties(security_event_consumer_c PROPERTIES C_STANDARD 17 C_EXTENSIONS OFF)
    set_target_properties(security_event_consumer_cpp PROPERTIES CXX_STANDARD 17 CXX_EXTENSIONS OFF)
elseif(SECURITY_EVENT_PROBE STREQUAL "composition")
    # A final link: the core, the provider as objects, and a project's own Access ports. This is the
    # arrangement the contract asks a composition to use, in a project outside Crux.
    add_executable(security_event_composition composition.c)
    target_link_libraries(security_event_composition PRIVATE
        crux::security_event crux_security_event_access_port)
    target_include_directories(security_event_composition PRIVATE
        "${CRUX_SOURCE_DIR}/components/access/src")
    target_link_libraries(security_event_composition PRIVATE crux_access_adapter)
elseif(SECURITY_EVENT_PROBE STREQUAL "no_provider")
    # A product image with no provider of the Access port: must not link.
    add_executable(security_event_no_provider no_provider.c)
    target_link_libraries(security_event_no_provider PRIVATE crux::security_event)
elseif(SECURITY_EVENT_PROBE STREQUAL "include_internal")
    # Includes the private core header through crux::security_event alone: must not compile.
    add_library(security_event_include OBJECT include_internal.c)
    target_link_libraries(security_event_include PRIVATE crux::security_event)
elseif(SECURITY_EVENT_PROBE STREQUAL "include_test")
    # Includes the private test controls through crux::security_event alone: must not compile.
    add_library(security_event_include OBJECT include_test.c)
    target_link_libraries(security_event_include PRIVATE crux::security_event)
elseif(SECURITY_EVENT_PROBE STREQUAL "include_adapter")
    # The Access adapter header stays out of reach through this target too: must not compile.
    add_library(security_event_include OBJECT include_adapter.c)
    target_link_libraries(security_event_include PRIVATE crux::security_event)
else()
    message(FATAL_ERROR "unknown SECURITY_EVENT_PROBE: ${SECURITY_EVENT_PROBE}")
endif()
