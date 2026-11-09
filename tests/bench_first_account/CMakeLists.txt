# Row C4: the step that creates the RT1060-EVKB bench image's first account, on the real Access core with the fake ports of
# tests/access. The step's own source is in the bench image's directory, where the bench guard does not apply to it because
# it holds no test form and no secret; the bench-only caller (src/bench_first_account.c) is compiled for the board.
set(CRUX_BENCH_FIRST_ACCOUNT_DIR
    "${CMAKE_CURRENT_LIST_DIR}/../zephyr/management_rt1060_compile/first_account")
get_filename_component(CRUX_BENCH_FIRST_ACCOUNT_DIR "${CRUX_BENCH_FIRST_ACCOUNT_DIR}" ABSOLUTE)
add_executable(bench_first_account_test first_account.c
    "${CRUX_BENCH_FIRST_ACCOUNT_DIR}/bench_first_account_provision.c")
target_include_directories(bench_first_account_test PRIVATE "${CRUX_BENCH_FIRST_ACCOUNT_DIR}")
target_link_libraries(bench_first_account_test PRIVATE crux_access_test_composition)
target_compile_options(bench_first_account_test PRIVATE -Wall -Wextra -Wpedantic -Werror -UNDEBUG)
target_compile_definitions(bench_first_account_test PRIVATE _POSIX_C_SOURCE=200809L)
add_test(NAME BenchFirstAccount.provision COMMAND bench_first_account_test)
set_tests_properties(BenchFirstAccount.provision PROPERTIES TIMEOUT 60)
