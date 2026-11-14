export PATH=/opt/crux-cache/venv/bin:$PATH
cd /ws/crux-pilot
clang-format --version
files="tests/zephyr/management_rt1060_compile/src/*.c tests/zephyr/management_rt1060_compile/src/*.h components/ssh/ports/zephyr/crux_ssh_wolf_provider.c components/ssh/src/crux_ssh_provider.h components/web/ports/zephyr/web_http_server.c"
clang-format -i $files
clang-format --dry-run -Werror $files && echo FORMAT-OK
