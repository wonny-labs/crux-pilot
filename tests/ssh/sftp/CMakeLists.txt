# Owned by #194. Its tests go here, so it has no need to change tests/ssh/CMakeLists.txt.

# The entry points of an image with no file service, which is what the callback table links here.
add_executable(ssh_sftp_stub_test stub_test.c)
target_link_libraries(ssh_sftp_stub_test PRIVATE crux_ssh_sftp crux_ssh_adapter)
target_include_directories(ssh_sftp_stub_test PRIVATE .. ../../../components/ssh/sftp/port)
target_compile_options(ssh_sftp_stub_test PRIVATE -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
target_compile_features(ssh_sftp_stub_test PRIVATE c_std_17)
add_test(NAME Ssh.sftp_stub COMMAND ssh_sftp_stub_test)
set_tests_properties(Ssh.sftp_stub PROPERTIES TIMEOUT 30)

# The port's macros, as wolfSSH calls them. wolfSSH itself is not part of the Host build.
add_executable(ssh_sftp_port_shape_test port_shape_test.c)
target_link_libraries(ssh_sftp_port_shape_test PRIVATE crux_ssh_provider)
target_include_directories(ssh_sftp_port_shape_test PRIVATE .. ../../../components/ssh/sftp/port)
target_compile_options(ssh_sftp_port_shape_test PRIVATE -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
target_compile_features(ssh_sftp_port_shape_test PRIVATE c_std_17)
add_test(NAME Ssh.sftp_port_shape COMMAND ssh_sftp_port_shape_test)
set_tests_properties(Ssh.sftp_port_shape PROPERTIES TIMEOUT 30)

if(TARGET crux_ssh_sftp_service)
    # The real operations through the fake provider, on the real file service and Access core.
    add_executable(ssh_sftp_test sftp_test.c)
    target_link_libraries(ssh_sftp_test PRIVATE crux_ssh_sftp_service crux_file_service_core
        crux_access_test_composition crux_file_fake crux_ssh_fake crux_ssh_fake_attachment crux_ssh_adapter)
    target_include_directories(ssh_sftp_test PRIVATE ..
        ../../../components/file_service/src ../../../components/ssh/sftp/port)
    target_compile_options(ssh_sftp_test PRIVATE -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
    target_compile_features(ssh_sftp_test PRIVATE c_std_17)
    target_compile_definitions(ssh_sftp_test PRIVATE _DEFAULT_SOURCE)
    add_test(NAME Ssh.sftp COMMAND ssh_sftp_test)
    set_tests_properties(Ssh.sftp PROPERTIES TIMEOUT 60)

    # The real wolfSSH handlers on Linux, for scripts/test-ssh-sftp-openssh.sh only.
    if(CRUX_SSH_SFTP_WOLF_MODULES)
        add_subdirectory(openssh)
    endif()
endif()
