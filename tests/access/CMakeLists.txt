# The instrumented core and the fake ports form a test composition. They exist only when Crux builds
# its own tests, and no public target or provider setting can select them.
add_library(crux_access_test_composition STATIC ${CRUX_ACCESS_SOURCES} fake_ports.c access_test.c)
target_include_directories(crux_access_test_composition
    PUBLIC . ../../components/access/src)
target_link_libraries(crux_access_test_composition
    PUBLIC crux_access_adapter crux::sync Threads::Threads)
target_compile_definitions(crux_access_test_composition
    PUBLIC CRUX_ACCESS_TESTING=1 CRUX_ACCESS_TEST_COMPOSITION=1)
target_compile_options(crux_access_test_composition PRIVATE -Wall -Wextra -Wpedantic -Werror)

foreach(suite IN ITEMS conformance accounts security keys faults secrets events clock_fault)
    add_executable(access_${suite} ${suite}.c)
    target_link_libraries(access_${suite} PRIVATE crux_access_test_composition)
    target_compile_options(access_${suite} PRIVATE -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
    target_compile_definitions(access_${suite} PRIVATE _POSIX_C_SOURCE=200809L)
    add_test(NAME Access.${suite} COMMAND access_${suite})
    set_tests_properties(Access.${suite} PROPERTIES TIMEOUT 60)
endforeach()

target_link_options(access_faults PRIVATE -Wl,--wrap=crux_mutex_lock)

# Every public and adapter declaration must also compile for a C++ consumer.
add_library(access_cpp_headers OBJECT cpp_headers.cpp)
target_link_libraries(access_cpp_headers PRIVATE crux_access_adapter)
target_compile_options(access_cpp_headers PRIVATE -Wall -Wextra -Wpedantic -Werror)
