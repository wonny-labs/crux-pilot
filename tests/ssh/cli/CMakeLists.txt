# Owned by #193. Its tests go here, so it has no need to change tests/ssh/CMakeLists.txt.

# The entry points of an image with no command line, which is what the callback table links here.
add_executable(ssh_cli_stub_test stub_test.c)
target_link_libraries(ssh_cli_stub_test PRIVATE crux_ssh_cli crux_ssh_adapter)
target_include_directories(ssh_cli_stub_test PRIVATE ..)
target_compile_options(ssh_cli_stub_test PRIVATE -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
target_compile_features(ssh_cli_stub_test PRIVATE c_std_17)
add_test(NAME Ssh.cli_stub COMMAND ssh_cli_stub_test)
set_tests_properties(Ssh.cli_stub PROPERTIES TIMEOUT 30)

# The real entry points, on the compositions of tests/cli and tests/access. A build that leaves out the
# command line, Serial or Thread has none of them, and says so. One that has them must run these suites,
# so a missing target is an error and not a smaller test run.
if(NOT (CRUX_BUILD_CLI AND CRUX_BUILD_SERIAL AND CRUX_BUILD_THREAD))
    message(STATUS "The CLI over SSH suites need CRUX_BUILD_CLI, _SERIAL and _THREAD, and do not run here")
else()
    foreach(needed IN ITEMS crux_ssh_cli_sessions cli_test_scripted cli_test_core
                            crux_access_test_composition Threads::Threads)
        if(NOT TARGET ${needed})
            message(FATAL_ERROR "The CLI over SSH suites need ${needed}, which this build does not have")
        endif()
    endforeach()

    set(SSH_CLI_TEST_WARNINGS -Wall -Wextra -Wpedantic -Werror -UNDEBUG)

    add_library(ssh_cli_harness STATIC ssh_cli_harness.c)
    target_include_directories(ssh_cli_harness PUBLIC . ../../../components/ssh/cli)
    target_link_libraries(ssh_cli_harness
        PUBLIC crux_ssh_cli_sessions crux_ssh_fake crux_ssh_fake_attachment crux_ssh_adapter)
    target_compile_options(ssh_cli_harness PRIVATE -Wall -Wextra -Wpedantic -Werror)
    target_compile_features(ssh_cli_harness PRIVATE c_std_17)

    # On the scripted access control, with the real console on a scripted Serial to compare with.
    add_executable(ssh_cli_session_test session_test.c ../../cli/fake_serial.c
        ../../../components/cli/console/cli_console_serial.c)
    target_link_libraries(ssh_cli_session_test PRIVATE ssh_cli_harness cli_test_scripted crux::thread)
    target_link_options(ssh_cli_session_test PRIVATE -Wl,--wrap=crux_thread_sleep_for)
    target_compile_options(ssh_cli_session_test PRIVATE ${SSH_CLI_TEST_WARNINGS})
    target_compile_features(ssh_cli_session_test PRIVATE c_std_17)
    add_test(NAME Ssh.cli_session COMMAND ssh_cli_session_test)
    set_tests_properties(Ssh.cli_session PROPERTIES TIMEOUT 120)

    # On the real access core with its fake ports.
    add_executable(ssh_cli_real_access_test real_access_test.c)
    target_link_libraries(ssh_cli_real_access_test PRIVATE ssh_cli_harness cli_test_core
        crux_access_test_composition)
    target_compile_options(ssh_cli_real_access_test PRIVATE ${SSH_CLI_TEST_WARNINGS})
    target_compile_features(ssh_cli_real_access_test PRIVATE c_std_17)
    add_test(NAME Ssh.cli_real_access COMMAND ssh_cli_real_access_test)
    set_tests_properties(Ssh.cli_real_access PROPERTIES TIMEOUT 120)

    # On real threads, which the fake provider cannot show.
    add_executable(ssh_cli_concurrency_test concurrency_test.c)
    target_include_directories(ssh_cli_concurrency_test PRIVATE ../../../components/ssh/cli)
    target_link_libraries(ssh_cli_concurrency_test PRIVATE crux_ssh_cli_sessions cli_test_core
        crux_access_test_composition crux_ssh_fake_attachment crux_ssh_adapter Threads::Threads)
    target_compile_options(ssh_cli_concurrency_test PRIVATE ${SSH_CLI_TEST_WARNINGS})
    target_compile_definitions(ssh_cli_concurrency_test PRIVATE _POSIX_C_SOURCE=200809L)
    target_compile_features(ssh_cli_concurrency_test PRIVATE c_std_17)
    add_test(NAME Ssh.cli_concurrency COMMAND ssh_cli_concurrency_test)
    set_tests_properties(Ssh.cli_concurrency PROPERTIES TIMEOUT 300)

    # What the real entry points can reach at all.
    add_test(NAME Ssh.cli_entry_symbols
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/check_entry_symbols.py
                --nm ${CMAKE_NM} $<TARGET_OBJECTS:crux_ssh_cli_sessions>)
    set_tests_properties(Ssh.cli_entry_symbols PROPERTIES TIMEOUT 30)

    # A longer line than a worker's stack is sized for does not compile, and the bound itself does.
    set(components ${PROJECT_SOURCE_DIR}/components)
    set(ssh_cli_syntax_only ${CMAKE_C_COMPILER} -std=c17 -fsyntax-only -DCRUX_CLI_TOKENS_MAX=10
        -I${components}/ssh/src -I${components}/ssh/adapter -I${components}/base/include
        -I${components}/access/include -I${components}/access/adapter
        -I${components}/cli/include -I${components}/cli/adapter)
    add_test(NAME Ssh.cli_line_at_the_stack_bound
        COMMAND ${ssh_cli_syntax_only} -DCRUX_CLI_LINE_MAX=1024 ${components}/ssh/cli/crux_ssh_cli.c)
    add_test(NAME Ssh.cli_line_over_the_stack_bound
        COMMAND ${ssh_cli_syntax_only} -DCRUX_CLI_LINE_MAX=1025 ${components}/ssh/cli/crux_ssh_cli.c)
    set_tests_properties(Ssh.cli_line_over_the_stack_bound PROPERTIES
        PASS_REGULAR_EXPRESSION "A longer line needs a larger SSH worker stack")
    set_tests_properties(Ssh.cli_line_at_the_stack_bound Ssh.cli_line_over_the_stack_bound PROPERTIES
        TIMEOUT 30)
endif()
