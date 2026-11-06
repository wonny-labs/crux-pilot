# Private to Crux: no public header and no crux:: target. A channel adapter links these targets PRIVATE,
# so that their include directories never reach a consumer. scripts/check-ssh-boundaries.py checks that.
add_library(crux_ssh_provider INTERFACE)
target_include_directories(crux_ssh_provider INTERFACE src)
target_link_libraries(crux_ssh_provider INTERFACE crux::base)

# The checks a provider applies to what a client sends, with no SSH library type, so that the Host tests
# them (#192). A provider build compiles the same source: see ports/zephyr/provider_sources.cmake.
add_library(crux_ssh_wire STATIC src/crux_ssh_wire.c)
target_link_libraries(crux_ssh_wire PUBLIC crux_ssh_provider)
target_compile_options(crux_ssh_wire PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_compile_features(crux_ssh_wire PRIVATE c_std_17)

add_library(crux_ssh_adapter INTERFACE)
target_include_directories(crux_ssh_adapter INTERFACE adapter)
target_link_libraries(crux_ssh_adapter INTERFACE crux_ssh_provider crux_access_api)

# The entry points. Each directory belongs to one session, which adds its sources there and in its
# sources.cmake, and so has no need to change this file.
add_subdirectory(access)
add_subdirectory(cli)
add_subdirectory(sftp)

add_library(crux_ssh_callbacks STATIC src/crux_ssh_callbacks.c)
target_link_libraries(crux_ssh_callbacks
    PUBLIC crux_ssh_provider
    PRIVATE crux_ssh_access crux_ssh_cli crux_ssh_sftp)
target_compile_options(crux_ssh_callbacks PRIVATE -Wall -Wextra -Wpedantic -Werror)
