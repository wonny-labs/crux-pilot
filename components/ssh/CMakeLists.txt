# Private to Crux: no public header and no crux:: target. A channel adapter links this target PRIVATE,
# so that its include directory never reaches a consumer. scripts/check-ssh-boundaries.py checks that.
add_library(crux_ssh_provider INTERFACE)
target_include_directories(crux_ssh_provider INTERFACE src)
target_link_libraries(crux_ssh_provider INTERFACE crux::base)
