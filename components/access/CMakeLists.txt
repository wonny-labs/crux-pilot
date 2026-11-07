set(CRUX_ACCESS_SOURCES
    src/access.c
    src/access_accounts.c
    src/access_delay.c
    src/access_login.c
    src/access_record.c
    src/access_session.c
)
list(TRANSFORM CRUX_ACCESS_SOURCES PREPEND "${CMAKE_CURRENT_LIST_DIR}/")
set(CRUX_ACCESS_SOURCES "${CRUX_ACCESS_SOURCES}" CACHE INTERNAL "Portable Access sources")

add_library(crux_access_api INTERFACE)
target_include_directories(crux_access_api INTERFACE include)

# Internal to Crux channel adapters. Link it PRIVATE, so that its include directory never reaches a
# consumer. cmake/CruxAccessBoundary.cmake enforces this.
add_library(crux_access_adapter INTERFACE)
target_include_directories(crux_access_adapter INTERFACE adapter)
target_link_libraries(crux_access_adapter INTERFACE crux_access_api)

# The core calls the private ports in src/access_port.h. No provider is linked here, so a product
# image links only once a composition supplies one. The fake ports in tests/access are a test
# composition that exists only when Crux builds its own tests.
add_library(crux_access_core STATIC ${CRUX_ACCESS_SOURCES})
target_include_directories(crux_access_core PRIVATE src)
target_link_libraries(crux_access_core PUBLIC crux_access_api PRIVATE crux_access_adapter crux::sync)
target_compile_options(crux_access_core PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_library(crux_access INTERFACE)
add_library(crux::access ALIAS crux_access)
target_link_libraries(crux_access INTERFACE crux_access_api crux_access_core)

# The clock port over the Time contract, which a composition links by name. It is an object target,
# so that a final link always takes it, and a second provider of the port is a duplicate symbol.
if(TARGET crux::time)
    add_library(crux_access_time_clock OBJECT src/access_clock_time.c)
    target_include_directories(crux_access_time_clock PRIVATE src)
    target_link_libraries(crux_access_time_clock PRIVATE crux_access_adapter PUBLIC crux::time)
    target_compile_options(crux_access_time_clock PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
