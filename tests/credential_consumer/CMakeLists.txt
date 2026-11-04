cmake_minimum_required(VERSION 3.28)
project(crux_credential_consumer LANGUAGES C CXX)
if(NOT CRUX_SOURCE_DIR)
    get_filename_component(CRUX_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
endif()
# Selects what scripts/check-credential-boundary.py asks this project to do. See that script for each
# probe's expected outcome.
set(CREDENTIAL_PROBE "consumer" CACHE STRING "Boundary probe")
set(CRUX_BUILD_SYNC ON CACHE BOOL "Required by the credential store")
set(CRUX_BUILD_FILE ON CACHE BOOL "Required by the credential store")
set(CRUX_BUILD_CREDENTIAL ON CACHE BOOL "Capability under test")
add_subdirectory("${CRUX_SOURCE_DIR}" crux)
include("${CRUX_SOURCE_DIR}/cmake/CruxCredentialBoundary.cmake")
crux_assert_portable_usage(crux::credential_store)
crux_assert_credential_adapter_unreachable(crux::credential_store)

# A fake protection must not exist in a build that a product could use.
if(TARGET crux_credential_test_composition OR TARGET crux_credential_test_composition_small)
    message(FATAL_ERROR "the credential test composition must not exist outside Crux's own tests")
endif()

set(adapter_dir "${CRUX_SOURCE_DIR}/components/credential/adapter")

if(CREDENTIAL_PROBE STREQUAL "consumer")
    # A project: includes the public header and compiles as C17 and C++17.
    add_library(credential_consumer_c OBJECT consumer.c)
    add_library(credential_consumer_cpp OBJECT consumer.cpp)
    foreach(target IN ITEMS credential_consumer_c credential_consumer_cpp)
        target_link_libraries(${target} PRIVATE crux::credential_store)
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
        crux_assert_portable_consumer(${target})
        crux_assert_credential_consumer(${target})
    endforeach()
    set_target_properties(credential_consumer_c PROPERTIES C_STANDARD 17 C_EXTENSIONS OFF)
    set_target_properties(credential_consumer_cpp PROPERTIES CXX_STANDARD 17 CXX_EXTENSIONS OFF)
elseif(CREDENTIAL_PROBE STREQUAL "private_service")
    # The arrangement a TLS or SSH provider will use: must be accepted, and must not expose the header.
    add_library(credential_service STATIC service.c)
    target_link_libraries(credential_service PUBLIC crux::credential_store
        PRIVATE crux_credential_adapter)
    add_library(credential_service_consumer OBJECT consumer.c)
    target_link_libraries(credential_service_consumer PRIVATE credential_service)
    crux_assert_credential_consumer(credential_service_consumer)
elseif(CREDENTIAL_PROBE STREQUAL "private_service_include")
    # The same arrangement, with a project that tries to use the header anyway: must not compile.
    add_library(credential_service STATIC service.c)
    target_link_libraries(credential_service PUBLIC crux::credential_store
        PRIVATE crux_credential_adapter)
    add_library(credential_service_consumer OBJECT include_adapter.c)
    target_link_libraries(credential_service_consumer PRIVATE credential_service)
    crux_assert_credential_consumer(credential_service_consumer)
elseif(CREDENTIAL_PROBE STREQUAL "public_service")
    # A service that exposes the adapter: must be rejected at configuration.
    add_library(credential_service STATIC service.c)
    target_link_libraries(credential_service PUBLIC crux_credential_adapter)
    add_library(credential_service_consumer OBJECT consumer.c)
    target_link_libraries(credential_service_consumer PRIVATE credential_service)
    crux_assert_credential_consumer(credential_service_consumer)
elseif(CREDENTIAL_PROBE STREQUAL "interface_directory")
    # A library that passes the directory on without naming the target: must be rejected.
    add_library(credential_copy INTERFACE)
    target_include_directories(credential_copy INTERFACE "${adapter_dir}")
    add_library(credential_copy_consumer OBJECT consumer.c)
    target_link_libraries(credential_copy_consumer PRIVATE credential_copy crux::credential_store)
    crux_assert_credential_consumer(credential_copy_consumer)
elseif(CREDENTIAL_PROBE STREQUAL "parent_directory")
    # A library that passes on a parent, from which <adapter/crux_credential_adapter.h> resolves: must
    # be rejected.
    add_library(credential_copy INTERFACE)
    target_include_directories(credential_copy INTERFACE "${CRUX_SOURCE_DIR}/components/credential")
    add_library(credential_copy_consumer OBJECT consumer.c)
    target_link_libraries(credential_copy_consumer PRIVATE credential_copy crux::credential_store)
    crux_assert_credential_consumer(credential_copy_consumer)
elseif(CREDENTIAL_PROBE STREQUAL "compile_option")
    # A library that passes the directory on as a compile option: must be rejected.
    add_library(credential_copy INTERFACE)
    target_compile_options(credential_copy INTERFACE "-I${adapter_dir}")
    add_library(credential_copy_consumer OBJECT consumer.c)
    target_link_libraries(credential_copy_consumer PRIVATE credential_copy crux::credential_store)
    crux_assert_credential_consumer(credential_copy_consumer)
elseif(CREDENTIAL_PROBE STREQUAL "direct_interface")
    # A library that links the internal target through the direct-link property: must be rejected.
    add_library(credential_copy INTERFACE)
    set_property(TARGET credential_copy PROPERTY INTERFACE_LINK_LIBRARIES_DIRECT
        crux_credential_adapter)
    add_library(credential_copy_consumer OBJECT consumer.c)
    target_link_libraries(credential_copy_consumer PRIVATE credential_copy crux::credential_store)
    crux_assert_credential_consumer(credential_copy_consumer)
elseif(CREDENTIAL_PROBE STREQUAL "direct_directory")
    # A project that adds the directory itself: must be rejected.
    add_library(credential_direct_consumer OBJECT consumer.c)
    target_include_directories(credential_direct_consumer PRIVATE "${adapter_dir}")
    target_link_libraries(credential_direct_consumer PRIVATE crux::credential_store)
    crux_assert_credential_consumer(credential_direct_consumer)
elseif(CREDENTIAL_PROBE STREQUAL "direct_link")
    # A project that links the internal target itself: must be rejected.
    add_library(credential_direct_consumer OBJECT consumer.c)
    target_link_libraries(credential_direct_consumer PRIVATE crux_credential_adapter)
    crux_assert_credential_consumer(credential_direct_consumer)
elseif(CREDENTIAL_PROBE STREQUAL "include_adapter")
    # Includes the adapter header through the public target alone: must not compile.
    add_library(credential_include_consumer OBJECT include_adapter.c)
    target_link_libraries(credential_include_consumer PRIVATE crux::credential_store)
elseif(CREDENTIAL_PROBE STREQUAL "include_port")
    # Includes the private port header through the public target alone: must not compile.
    add_library(credential_include_consumer OBJECT include_port.c)
    target_link_libraries(credential_include_consumer PRIVATE crux::credential_store)
elseif(CREDENTIAL_PROBE STREQUAL "no_provider")
    # A product image with no provider selected: must not link.
    add_executable(credential_no_provider no_provider.c)
    target_link_libraries(credential_no_provider PRIVATE crux::credential_store)
else()
    message(FATAL_ERROR "unknown CREDENTIAL_PROBE: ${CREDENTIAL_PROBE}")
endif()
