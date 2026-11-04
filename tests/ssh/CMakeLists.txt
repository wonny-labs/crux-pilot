# A test composition: it exists only when Crux builds its own tests, and it is the same fake that the
# CLI (#193) and SFTP (#194) Host tests link.
add_library(crux_ssh_fake STATIC fake_provider.c)
target_include_directories(crux_ssh_fake PUBLIC .)
target_link_libraries(crux_ssh_fake PUBLIC crux_ssh_provider)
target_compile_options(crux_ssh_fake PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_compile_features(crux_ssh_fake PRIVATE c_std_17)

add_library(crux_ssh_conformance STATIC conformance.c)
target_include_directories(crux_ssh_conformance PUBLIC .)
target_link_libraries(crux_ssh_conformance PUBLIC crux_ssh_provider)
target_compile_options(crux_ssh_conformance PRIVATE -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
target_compile_features(crux_ssh_conformance PRIVATE c_std_17)

foreach(suite IN ITEMS conformance fault)
    add_executable(ssh_${suite}_test ${suite}_test.c)
    target_link_libraries(ssh_${suite}_test PRIVATE crux_ssh_fake crux_ssh_conformance)
    target_compile_options(ssh_${suite}_test PRIVATE -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
    target_compile_features(ssh_${suite}_test PRIVATE c_std_17)
endforeach()
add_test(NAME Ssh.ProviderConformance COMMAND ssh_conformance_test)
add_test(NAME Ssh.ConformanceCatchesDefects COMMAND ssh_fault_test)
set_tests_properties(Ssh.ProviderConformance Ssh.ConformanceCatchesDefects PROPERTIES TIMEOUT 30)

add_library(ssh_cpp_headers OBJECT cpp_headers.cpp)
target_link_libraries(ssh_cpp_headers PRIVATE crux_ssh_provider)
target_compile_options(ssh_cpp_headers PRIVATE -Wall -Wextra -Wpedantic -Werror)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME Ssh.Boundaries
    COMMAND ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/scripts/check-ssh-boundaries.py
            --root ${PROJECT_SOURCE_DIR})
set_tests_properties(Ssh.Boundaries PROPERTIES TIMEOUT 30)
