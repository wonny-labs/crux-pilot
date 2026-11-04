set(CRUX_CREDENTIAL_SOURCES
    src/credential_image.c
    src/credential_store.c
)
list(TRANSFORM CRUX_CREDENTIAL_SOURCES PREPEND "${CMAKE_CURRENT_LIST_DIR}/")
set(CRUX_CREDENTIAL_SOURCES "${CRUX_CREDENTIAL_SOURCES}" CACHE INTERNAL
    "Portable credential store sources")

add_library(crux_credential_api INTERFACE)
target_include_directories(crux_credential_api INTERFACE include)
target_link_libraries(crux_credential_api INTERFACE crux::base)

# Internal to the Access store port, TLS and SSH providers and a composition. Link it PRIVATE, so that
# its include directory never reaches a consumer. cmake/CruxCredentialBoundary.cmake enforces this.
add_library(crux_credential_adapter INTERFACE)
target_include_directories(crux_credential_adapter INTERFACE adapter)
target_link_libraries(crux_credential_adapter INTERFACE crux_credential_api crux_file_api)

# The core calls the private ports in src/credential_port.h and the File contract. No provider of
# either is linked here, so a product image links only once a composition supplies them. It refuses
# a test declaration at start, and the fake ports in tests/credential exist only when Crux builds
# its own tests.
add_library(crux_credential_core STATIC ${CRUX_CREDENTIAL_SOURCES})
target_include_directories(crux_credential_core PRIVATE src)
target_link_libraries(crux_credential_core
    PUBLIC crux_credential_api PRIVATE crux_credential_adapter crux::sync)
target_compile_options(crux_credential_core PRIVATE -Wall -Wextra -Wpedantic -Werror)
set(CRUX_CREDENTIAL_SLOT_BYTES 16384 CACHE STRING
    "Size of each credential store slot, a whole number of 512-byte sectors")
target_compile_definitions(crux_credential_core PRIVATE
    CRUX_CREDENTIAL_SLOT_BYTES=${CRUX_CREDENTIAL_SLOT_BYTES})

add_library(crux_credential_store INTERFACE)
add_library(crux::credential_store ALIAS crux_credential_store)
target_link_libraries(crux_credential_store INTERFACE crux_credential_api crux_credential_core)

# What a composition links to give crux_access its account store. It implements a private port of
# Access, so it needs Access's source directory and exposes nothing.
if(CRUX_BUILD_ACCESS)
    add_library(crux_credential_access_store STATIC access_store/credential_access_store.c)
    target_include_directories(crux_credential_access_store PRIVATE
        ../access/src
        src)
    target_link_libraries(crux_credential_access_store
        PRIVATE crux_credential_adapter crux_access_adapter)
    target_compile_options(crux_credential_access_store PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
