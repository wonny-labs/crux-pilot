add_library(crux_execution_admission OBJECT admission.c)
target_link_libraries(crux_execution_admission PRIVATE crux::thread crux::sync)
target_include_directories(crux_execution_admission PRIVATE
    ../../components/thread/src ../../components/thread/ports/linux
    ../../components/sync/src ../../components/sync/ports/linux)
target_compile_options(crux_execution_admission PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_executable(crux_execution_conformance conformance.c host_main.c public_probe.c
    $<TARGET_OBJECTS:crux_execution_admission>)
target_link_options(crux_execution_conformance PRIVATE -Wl,--wrap=crux_thread_impl_join
    -Wl,--wrap=crux_mutex_impl_lock -Wl,--wrap=crux_semaphore_impl_take)
target_link_libraries(crux_execution_conformance PRIVATE crux::thread crux::sync)
target_compile_definitions(crux_execution_conformance PRIVATE _POSIX_C_SOURCE=200809L)
target_compile_options(crux_execution_conformance PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME Execution.Conformance COMMAND crux_execution_conformance)
set_tests_properties(Execution.Conformance PROPERTIES TIMEOUT 20)

find_package(Threads REQUIRED)
add_executable(crux_execution_host_failures host_failures.c)
target_link_libraries(crux_execution_host_failures PRIVATE crux::thread crux::sync Threads::Threads)
target_compile_definitions(crux_execution_host_failures PRIVATE _POSIX_C_SOURCE=200809L)
target_compile_options(crux_execution_host_failures PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_link_options(crux_execution_host_failures PRIVATE
    -Wl,--wrap=pthread_create -Wl,--wrap=pthread_mutex_init
    -Wl,--wrap=pthread_cond_init -Wl,--wrap=clock_gettime)
add_test(NAME Execution.HostFailures COMMAND crux_execution_host_failures)
set_tests_properties(Execution.HostFailures PROPERTIES TIMEOUT 20)

add_executable(crux_execution_host_poll host_poll.c)
target_link_libraries(crux_execution_host_poll PRIVATE crux::thread crux::sync Threads::Threads)
target_compile_definitions(crux_execution_host_poll PRIVATE _POSIX_C_SOURCE=200809L)
target_compile_options(crux_execution_host_poll PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_link_options(crux_execution_host_poll PRIVATE -Wl,--wrap=pthread_mutex_init
    -Wl,--wrap=pthread_mutex_lock -Wl,--wrap=pthread_mutex_trylock)
add_test(NAME Execution.HostPoll COMMAND crux_execution_host_poll)
set_tests_properties(Execution.HostPoll PROPERTIES TIMEOUT 20)

add_executable(crux_execution_host_admission host_admission.c)
target_include_directories(crux_execution_host_admission PRIVATE
    ../../components/thread/src ../../components/thread/ports/linux
    ../../components/sync/src ../../components/sync/ports/linux)
target_link_libraries(crux_execution_host_admission PRIVATE crux::thread crux::sync Threads::Threads)
target_compile_definitions(crux_execution_host_admission PRIVATE _POSIX_C_SOURCE=200809L)
target_compile_options(crux_execution_host_admission PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_link_options(crux_execution_host_admission PRIVATE -Wl,--wrap=thread_native_metadata_lock -Wl,--wrap=sync_native_metadata_lock)
add_test(NAME Execution.HostAdmission COMMAND crux_execution_host_admission)
set_tests_properties(Execution.HostAdmission PROPERTIES TIMEOUT 20)

add_executable(crux_execution_host_poll_finalize host_poll_finalize.c)
target_include_directories(crux_execution_host_poll_finalize PRIVATE
    ../../components/thread/src ../../components/thread/ports/linux
    ../../components/sync/src ../../components/sync/ports/linux)
target_link_libraries(crux_execution_host_poll_finalize PRIVATE crux::thread crux::sync Threads::Threads)
target_compile_definitions(crux_execution_host_poll_finalize PRIVATE _POSIX_C_SOURCE=200809L)
target_compile_options(crux_execution_host_poll_finalize PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_link_options(crux_execution_host_poll_finalize PRIVATE
    -Wl,--wrap=thread_native_metadata_lock -Wl,--wrap=sync_native_metadata_lock
    -Wl,--wrap=crux_mutex_impl_lock
    -Wl,--wrap=crux_semaphore_impl_take -Wl,--wrap=crux_thread_impl_join)
add_test(NAME Execution.HostPollFinalize COMMAND crux_execution_host_poll_finalize)
set_tests_properties(Execution.HostPollFinalize PROPERTIES TIMEOUT 20)
