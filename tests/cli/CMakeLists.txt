# Test compositions. They exist only when Crux builds its own tests, and no public target or provider
# setting can select them. Each instrumented core adds the test reset hook, and the provider of each
# fake is chosen at link: a scripted access control, or the real core on its own fake ports.
set(CRUX_CLI_TEST_INCLUDES
    . ../../components/cli/src ../../components/cli/console)
set(CRUX_CLI_TEST_DEFINITIONS
    CRUX_CLI_TESTING=1 CRUX_CLI_TEST_COMPOSITION=1)
set(CRUX_CLI_TEST_WARNINGS -Wall -Wextra -Wpedantic -Werror)

# The registry, the sessions and the portable login loop, with a recording event port and a scripted
# transport. Both compositions below use this.
add_library(cli_test_core STATIC
    ${CRUX_CLI_SOURCES} ../../components/cli/console/cli_console.c
    cli_test.c fake_events.c fake_transport.c)
target_include_directories(cli_test_core PUBLIC ${CRUX_CLI_TEST_INCLUDES})
target_link_libraries(cli_test_core PUBLIC crux_cli_adapter crux_serial_api)
target_compile_definitions(cli_test_core PUBLIC ${CRUX_CLI_TEST_DEFINITIONS})
target_compile_options(cli_test_core PRIVATE ${CRUX_CLI_TEST_WARNINGS})

# With the scripted access control, so that each row of a result table can be chosen directly.
add_library(cli_test_scripted STATIC fake_access.c cli_session_test.c)
target_link_libraries(cli_test_scripted PUBLIC cli_test_core)
target_compile_options(cli_test_scripted PRIVATE ${CRUX_CLI_TEST_WARNINGS})

foreach(suite IN ITEMS parser registry rejection permissions login ssh_shape fuzz)
    add_executable(cli_${suite} ${suite}.c)
    target_link_libraries(cli_${suite} PRIVATE cli_test_scripted)
    target_compile_options(cli_${suite} PRIVATE ${CRUX_CLI_TEST_WARNINGS} -UNDEBUG)
    add_test(NAME Cli.${suite} COMMAND cli_${suite})
    set_tests_properties(Cli.${suite} PROPERTIES TIMEOUT 120)
endforeach()

# The serial cycle over a scripted crux_serial. The pause is wrapped, so that a test can read it.
add_executable(cli_console console.c fake_serial.c ../../components/cli/console/cli_console_serial.c)
target_link_libraries(cli_console PRIVATE cli_test_scripted crux::thread)
target_link_options(cli_console PRIVATE -Wl,--wrap=crux_thread_sleep_for)
target_compile_options(cli_console PRIVATE ${CRUX_CLI_TEST_WARNINGS} -UNDEBUG)
add_test(NAME Cli.console COMMAND cli_console)
set_tests_properties(Cli.console PROPERTIES TIMEOUT 60)

# The same cycle on the real Host Serial provider, over a pseudo-terminal.
add_executable(cli_console_pty console_pty.c ../../components/cli/console/cli_console_serial.c)
target_include_directories(cli_console_pty PRIVATE ../../components/serial/ports/linux)
target_link_libraries(cli_console_pty PRIVATE cli_test_scripted crux::serial crux::thread
                                              Threads::Threads)
target_compile_options(cli_console_pty PRIVATE ${CRUX_CLI_TEST_WARNINGS} -UNDEBUG)
add_test(NAME Cli.console_pty COMMAND cli_console_pty)
set_tests_properties(Cli.console_pty PROPERTIES TIMEOUT 60)

# On the real core, with its fake ports and a fake store.
add_executable(cli_login_real login_real.c)
target_link_libraries(cli_login_real PRIVATE cli_test_core crux_access_test_composition)
target_compile_options(cli_login_real PRIVATE ${CRUX_CLI_TEST_WARNINGS} -UNDEBUG)
target_compile_definitions(cli_login_real PRIVATE _POSIX_C_SOURCE=200809L)
target_link_options(cli_login_real PRIVATE -Wl,--wrap=crux_mutex_lock)
add_test(NAME Cli.login_real COMMAND cli_login_real)
set_tests_properties(Cli.login_real PROPERTIES TIMEOUT 120)

# Every public, adapter and console declaration must also compile for a C++ consumer.
add_library(cli_cpp_headers OBJECT cpp_headers.cpp)
target_include_directories(cli_cpp_headers PRIVATE ../../components/cli/console)
target_link_libraries(cli_cpp_headers PRIVATE crux_cli_adapter crux_serial_api)
target_compile_options(cli_cpp_headers PRIVATE ${CRUX_CLI_TEST_WARNINGS})
