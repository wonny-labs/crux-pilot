# A test composition: it exists only when Crux builds its own tests, and it is the same fake that the
# CLI (#193) and SFTP (#194) Host tests link.
add_library(crux_ssh_fake STATIC fake_provider.c)
target_include_directories(crux_ssh_fake PUBLIC .)
target_link_libraries(crux_ssh_fake PUBLIC crux_ssh_provider PRIVATE crux_ssh_wire)
target_compile_options(crux_ssh_fake PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_compile_features(crux_ssh_fake PRIVATE c_std_17)

add_library(crux_ssh_fake_attachment STATIC fake_attachment.c)
target_include_directories(crux_ssh_fake_attachment PUBLIC .)
target_link_libraries(crux_ssh_fake_attachment PUBLIC crux_ssh_adapter)
target_compile_options(crux_ssh_fake_attachment PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_compile_features(crux_ssh_fake_attachment PRIVATE c_std_17)

add_library(crux_ssh_conformance STATIC conformance.c)
target_include_directories(crux_ssh_conformance PUBLIC .)
target_link_libraries(crux_ssh_conformance PUBLIC crux_ssh_provider)
target_compile_options(crux_ssh_conformance PRIVATE -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
target_compile_features(crux_ssh_conformance PRIVATE c_std_17)

foreach(suite IN ITEMS conformance fault fake attachment callback_table)
    add_executable(ssh_${suite}_test ${suite}_test.c)
    target_compile_options(ssh_${suite}_test PRIVATE -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
    target_compile_features(ssh_${suite}_test PRIVATE c_std_17)
endforeach()
target_link_libraries(ssh_conformance_test PRIVATE crux_ssh_fake crux_ssh_conformance)
target_link_libraries(ssh_fault_test PRIVATE crux_ssh_fake crux_ssh_conformance)
target_link_libraries(ssh_fake_test PRIVATE crux_ssh_fake)
target_link_libraries(ssh_attachment_test PRIVATE crux_ssh_fake_attachment)
target_link_libraries(ssh_callback_table_test PRIVATE crux_ssh_fake crux_ssh_callbacks)
add_test(NAME Ssh.ProviderConformance COMMAND ssh_conformance_test)
add_test(NAME Ssh.ConformanceCatchesDefects COMMAND ssh_fault_test)
add_test(NAME Ssh.FakeProvider COMMAND ssh_fake_test)
add_test(NAME Ssh.AttachmentDouble COMMAND ssh_attachment_test)
add_test(NAME Ssh.CallbackTable COMMAND ssh_callback_table_test)
set_tests_properties(Ssh.ProviderConformance Ssh.ConformanceCatchesDefects Ssh.FakeProvider
    Ssh.AttachmentDouble Ssh.CallbackTable PROPERTIES TIMEOUT 30)

add_library(ssh_cpp_headers OBJECT cpp_headers.cpp)
target_link_libraries(ssh_cpp_headers PRIVATE crux_ssh_adapter)
target_compile_options(ssh_cpp_headers PRIVATE -Wall -Wextra -Wpedantic -Werror)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME Ssh.Boundaries
    COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/scripts/check-ssh-boundaries.py
            --root ${PROJECT_SOURCE_DIR})
set_tests_properties(Ssh.Boundaries PROPERTIES TIMEOUT 30)

# Each session's own tests. It adds them in its directory, so it has no need to change this file.
add_subdirectory(access)
add_subdirectory(wire)
add_subdirectory(cli)
add_subdirectory(sftp)
