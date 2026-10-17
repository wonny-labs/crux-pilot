add_executable(crux-time-host main.c)
target_link_libraries(crux-time-host PRIVATE crux_time_commands crux::time)
target_compile_options(crux-time-host PRIVATE -Wall -Wextra -Wpedantic -Werror)
