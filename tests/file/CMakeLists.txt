add_executable(file_fault fault_test.c ../../components/file/src/file.c)
target_link_libraries(file_fault PRIVATE crux_file_api)
target_include_directories(file_fault PRIVATE ../../components/file/src)
add_executable(file_host host_test.c)
target_link_libraries(file_host PRIVATE crux::file)
target_include_directories(file_host PRIVATE ../../components/file/ports/linux)
target_compile_definitions(file_host PRIVATE _POSIX_C_SOURCE=200809L)
foreach(test IN ITEMS file_fault file_host)
    target_compile_options(${test} PRIVATE -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
    add_test(NAME File.${test} COMMAND ${test})
    set_tests_properties(File.${test} PROPERTIES TIMEOUT 10)
endforeach()

add_executable(file_lock lock_test.c ../../components/file/ports/linux/file_lock.c)
target_include_directories(file_lock PRIVATE ../../components/file/src)
target_link_libraries(file_lock PRIVATE crux_file_api Threads::Threads)
target_link_options(file_lock PRIVATE -Wl,--wrap=pthread_mutex_lock)
target_compile_options(file_lock PRIVATE -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
add_test(NAME File.SleepableLock COMMAND file_lock)
set_tests_properties(File.SleepableLock PROPERTIES TIMEOUT 20)

# The File additions, with the same cases on each provider. The fake is shared with the consumers of
# File in this repository, which need its faults and its power cut.
add_executable(file_conformance_linux conformance_linux.c conformance.c)
target_link_libraries(file_conformance_linux PRIVATE crux::file)
target_include_directories(file_conformance_linux PRIVATE ../../components/file/src
    ../../components/file/ports/linux)
target_compile_definitions(file_conformance_linux PRIVATE _POSIX_C_SOURCE=200809L)

add_library(crux_file_fake STATIC fake_file.c ../../components/file/src/file.c)
target_link_libraries(crux_file_fake PUBLIC crux_file_api)
target_include_directories(crux_file_fake PUBLIC . PRIVATE ../../components/file/src)
target_compile_definitions(crux_file_fake PRIVATE _DEFAULT_SOURCE)
target_compile_options(crux_file_fake PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_executable(file_conformance_fake conformance_fake.c conformance.c)
target_link_libraries(file_conformance_fake PRIVATE crux_file_fake)
target_include_directories(file_conformance_fake PRIVATE ../../components/file/src)

add_executable(file_fake_faults fake_faults_test.c)
target_link_libraries(file_fake_faults PRIVATE crux_file_fake)
target_compile_definitions(file_fake_faults PRIVATE _DEFAULT_SOURCE)

foreach(test IN ITEMS file_conformance_linux file_conformance_fake file_fake_faults)
    target_compile_options(${test} PRIVATE -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
    add_test(NAME File.${test} COMMAND ${test})
    set_tests_properties(File.${test} PROPERTIES TIMEOUT 20)
endforeach()
