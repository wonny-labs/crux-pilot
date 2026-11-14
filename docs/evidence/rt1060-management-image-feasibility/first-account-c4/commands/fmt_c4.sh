export PATH=/opt/crux-cache/venv/bin:$PATH
cd /ws/crux-pilot
clang-format --version
files="tests/zephyr/management_rt1060_compile/first_account/bench_first_account_provision.c tests/zephyr/management_rt1060_compile/first_account/bench_first_account_provision.h tests/zephyr/management_rt1060_compile/src/bench_first_account.c tests/zephyr/management_rt1060_compile/src/bench_first_account.h tests/bench_first_account/first_account.c"
clang-format -i $files
clang-format --dry-run -Werror $files && echo FORMAT-OK
