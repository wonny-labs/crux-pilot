# usage: hostci.sh [format]: what CI's host and docs jobs run on this change, in the Dev Container (CMake 3.28.4, Python 3.12,
# clang-format 18, no Kconfiglib): the formatter over every owned C file, the three bench unit-test files, the guard script
# without its Kconfig level, the docs check. With "format" it also rewrites the changed C files first.
export PATH=/opt/crux-cache/venv/bin:$PATH
git config --global --add safe.directory '*'
cd /ws/crux-pilot
export PYTHONDONTWRITEBYTECODE=1
cmake --version | head -1; python3 --version; clang-format --version
if [ "$1" = "format" ]; then
  V=tests/zephyr/management_rt1060_compile
  clang-format -i $V/report/bench_report_core.c $V/report/bench_report_core.h $V/report/bench_report_format.c $V/report/bench_report_format.h $V/report/bench_report_host_test.c $V/src/bench_report.c $V/src/bench_report.h
fi
echo "== check-format"; CLANG_FORMAT=clang-format ./scripts/check-format.sh && echo FORMAT-OK
echo "== unittest"; python3 -m unittest tests/build/test_management_bench_declaration.py tests/build/test_management_target_lists.py tests/build/test_management_bench_report.py 2>&1 | tail -6
echo "== docs"; python3 scripts/check_docs.py 2>&1 | tail -15; echo "docs rc=${PIPESTATUS[0]}"
