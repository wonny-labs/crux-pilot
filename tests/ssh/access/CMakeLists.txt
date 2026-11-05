# Owned by #192. Its tests go here, so it has no need to change tests/ssh/CMakeLists.txt.

# The entry points call crux_access, which needs a provider. Every test that links them, such as the
# callback table's, gets the test composition. This runs only when Crux builds its own tests.
target_link_libraries(crux_ssh_access INTERFACE crux_access_test_composition)

add_executable(ssh_access_adapter_test adapter_test.c)
target_link_libraries(ssh_access_adapter_test PRIVATE crux_ssh_access crux_ssh_adapter)
target_include_directories(ssh_access_adapter_test PRIVATE .. ../../../components/ssh/access)
target_compile_options(ssh_access_adapter_test PRIVATE -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
target_compile_features(ssh_access_adapter_test PRIVATE c_std_17)
add_test(NAME Ssh.access_adapter COMMAND ssh_access_adapter_test)

add_executable(ssh_host_key_test host_key_test.c ../../credential/medium_fake.c)
target_link_libraries(ssh_host_key_test PRIVATE crux_ssh_host_key crux_ssh_provider
    crux_credential_test_composition crux_file_fake)
target_include_directories(ssh_host_key_test PRIVATE .. ../../../components/ssh/access)
target_compile_options(ssh_host_key_test PRIVATE -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
target_compile_features(ssh_host_key_test PRIVATE c_std_17)
add_test(NAME Ssh.host_key COMMAND ssh_host_key_test)
set_tests_properties(Ssh.access_adapter Ssh.host_key PROPERTIES TIMEOUT 30)


add_test(NAME Ssh.ProviderTypes
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/check_provider_types.py
            --root ${PROJECT_SOURCE_DIR})
set_tests_properties(Ssh.ProviderTypes PROPERTIES TIMEOUT 30)
