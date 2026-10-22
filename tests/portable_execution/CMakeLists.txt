add_library(crux_execution_admission OBJECT admission.c)
target_link_libraries(crux_execution_admission PRIVATE crux::execution)
target_include_directories(crux_execution_admission PRIVATE
    ../../components/execution/src ../../components/execution/host)
target_compile_options(crux_execution_admission PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_executable(crux_execution_conformance conformance.c host_main.c public_probe.c
    $<TARGET_OBJECTS:crux_execution_admission>)
target_link_options(crux_execution_conformance PRIVATE -Wl,--wrap=exec_native_thread_join
    -Wl,--wrap=exec_native_mutex_lock -Wl,--wrap=exec_native_semaphore_take)
target_link_libraries(crux_execution_conformance PRIVATE crux::execution)
target_compile_definitions(crux_execution_conformance PRIVATE _POSIX_C_SOURCE=200809L)
target_compile_options(crux_execution_conformance PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME Execution.Conformance COMMAND crux_execution_conformance)
set_tests_properties(Execution.Conformance PROPERTIES TIMEOUT 20)

find_package(Threads REQUIRED)
add_executable(crux_execution_host_failures host_failures.c)
target_link_libraries(crux_execution_host_failures PRIVATE crux::execution Threads::Threads)
target_compile_definitions(crux_execution_host_failures PRIVATE _POSIX_C_SOURCE=200809L)
target_compile_options(crux_execution_host_failures PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_link_options(crux_execution_host_failures PRIVATE
    -Wl,--wrap=pthread_create -Wl,--wrap=pthread_mutex_init
    -Wl,--wrap=pthread_cond_init -Wl,--wrap=clock_gettime)
add_test(NAME Execution.HostFailures COMMAND crux_execution_host_failures)
set_tests_properties(Execution.HostFailures PROPERTIES TIMEOUT 20)

add_executable(crux_execution_host_poll host_poll.c)
target_link_libraries(crux_execution_host_poll PRIVATE crux::execution Threads::Threads)
target_compile_definitions(crux_execution_host_poll PRIVATE _POSIX_C_SOURCE=200809L)
target_compile_options(crux_execution_host_poll PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_link_options(crux_execution_host_poll PRIVATE -Wl,--wrap=pthread_mutex_init
    -Wl,--wrap=pthread_mutex_lock -Wl,--wrap=pthread_mutex_trylock)
add_test(NAME Execution.HostPoll COMMAND crux_execution_host_poll)
set_tests_properties(Execution.HostPoll PROPERTIES TIMEOUT 20)

add_executable(crux_execution_host_admission host_admission.c)
target_include_directories(crux_execution_host_admission PRIVATE
    ../../components/execution/src ../../components/execution/host)
target_link_libraries(crux_execution_host_admission PRIVATE crux::execution Threads::Threads)
target_compile_definitions(crux_execution_host_admission PRIVATE _POSIX_C_SOURCE=200809L)
target_compile_options(crux_execution_host_admission PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_link_options(crux_execution_host_admission PRIVATE -Wl,--wrap=exec_native_metadata_lock)
add_test(NAME Execution.HostAdmission COMMAND crux_execution_host_admission)
set_tests_properties(Execution.HostAdmission PROPERTIES TIMEOUT 20)

add_executable(crux_execution_host_poll_finalize host_poll_finalize.c)
target_include_directories(crux_execution_host_poll_finalize PRIVATE
    ../../components/execution/src ../../components/execution/host)
target_link_libraries(crux_execution_host_poll_finalize PRIVATE crux::execution Threads::Threads)
target_compile_definitions(crux_execution_host_poll_finalize PRIVATE _POSIX_C_SOURCE=200809L)
target_compile_options(crux_execution_host_poll_finalize PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_link_options(crux_execution_host_poll_finalize PRIVATE
    -Wl,--wrap=exec_native_metadata_lock -Wl,--wrap=exec_native_mutex_lock
    -Wl,--wrap=exec_native_semaphore_take -Wl,--wrap=exec_native_thread_join)
add_test(NAME Execution.HostPollFinalize COMMAND crux_execution_host_poll_finalize)
set_tests_properties(Execution.HostPollFinalize PROPERTIES TIMEOUT 20)
