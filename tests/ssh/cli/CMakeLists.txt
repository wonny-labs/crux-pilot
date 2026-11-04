# Owned by #193. Its tests go here, so it has no need to change tests/ssh/CMakeLists.txt.
add_executable(ssh_cli_stub_test stub_test.c)
target_link_libraries(ssh_cli_stub_test PRIVATE crux_ssh_cli crux_ssh_adapter)
target_include_directories(ssh_cli_stub_test PRIVATE ..)
target_compile_options(ssh_cli_stub_test PRIVATE -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
target_compile_features(ssh_cli_stub_test PRIVATE c_std_17)
add_test(NAME Ssh.cli_stub COMMAND ssh_cli_stub_test)
set_tests_properties(Ssh.cli_stub PROPERTIES TIMEOUT 30)
