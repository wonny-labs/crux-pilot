export PATH=/opt/crux-cache/venv/bin:$PATH
cd /ws/crux-pilot
cmake --version | head -1
python3 --version
python3 tests/zephyr/management_rt1060_compile/check_bench_guard.py --skip-kconfig 2>&1 | cut -c1-300 | tail -6
echo "rc=${PIPESTATUS[0]}"
python3 -m unittest tests/build/test_management_bench_declaration.py tests/build/test_management_target_lists.py 2>&1 | tail -4
