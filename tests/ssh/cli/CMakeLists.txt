# Owned by #193. Its tests go here, so it has no need to change tests/ssh/CMakeLists.txt.

# The entry points of an image with no command line, which is what the callback table links here.
add_executable(ssh_cli_stub_test stub_test.c)
target_link_libraries(ssh_cli_stub_test PRIVATE crux_ssh_cli crux_ssh_adapter)
target_include_directories(ssh_cli_stub_test PRIVATE ..)
target_compile_options(ssh_cli_stub_test PRIVATE -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
target_compile_features(ssh_cli_stub_test PRIVATE c_std_17)
add_test(NAME Ssh.cli_stub COMMAND ssh_cli_stub_test)
set_tests_properties(Ssh.cli_stub PROPERTIES TIMEOUT 30)

# The real entry points, on the compositions of tests/cli and tests/access. Those exist in a build with
# the command line and Serial, and these tests need no other part of them.
if(TARGET crux_ssh_cli_sessions AND TARGET cli_test_scripted)
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
endif()
