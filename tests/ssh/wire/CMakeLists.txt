# Owned by #192: the wire checks of components/ssh/src/crux_ssh_wire.c.
add_executable(ssh_wire_test wire_test.c)
target_link_libraries(ssh_wire_test PRIVATE crux_ssh_wire)
target_include_directories(ssh_wire_test PRIVATE ..)
target_compile_options(ssh_wire_test PRIVATE -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
target_compile_features(ssh_wire_test PRIVATE c_std_17)
add_test(NAME Ssh.wire COMMAND ssh_wire_test)
set_tests_properties(Ssh.wire PROPERTIES TIMEOUT 120)

# Each mutant of the checks, compiled apart with this suite, must fail it. The suite runs fewer random
# rounds there, and the known answers and exhaustive cases in full.
add_test(NAME Ssh.wire_mutations
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/wire_mutations.py
            --compiler ${CMAKE_C_COMPILER}
            --source ${PROJECT_SOURCE_DIR}/components/ssh/src/crux_ssh_wire.c
            --test ${CMAKE_CURRENT_SOURCE_DIR}/wire_test.c
            --include ${PROJECT_SOURCE_DIR}/components/ssh/src
            --include ${PROJECT_SOURCE_DIR}/components/base/include
            --include ${CMAKE_CURRENT_SOURCE_DIR}/..)
set_tests_properties(Ssh.wire_mutations PROPERTIES TIMEOUT 300)
