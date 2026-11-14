export PATH=/opt/crux-cache/venv/bin:$PATH
cd /ws/crux-pilot
clang-format --version
V=tests/zephyr/management_rt1060_compile
files="$V/report/*.c $V/report/*.h $V/src/bench_report.c $V/src/bench_report.h $V/src/main.c tests/native/bench_report/src/main.c components/ssh/ports/zephyr/crux_ssh_wolf_provider.c components/ssh/ports/zephyr/crux_ssh_wolf_provider.h"
clang-format -i $files; clang-format --dry-run -Werror $files && echo FORMAT-OK
