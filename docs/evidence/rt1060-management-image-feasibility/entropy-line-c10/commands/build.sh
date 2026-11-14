# One cross-build of the bench image of /ws/crux-pilot (a clean checkout of the commit under review) for mimxrt1060_evk@B//qspi, on C1's
# patched Zephyr copy at /ws/zephyr-wolf (read-only), with CRUX_BENCH_PASSWORD_FILE=/secret/c10.pw (a throwaway file made by
# bench_first_account_file.py generate, outside every repository, deleted afterwards). Keeps the size report, the manifest, the
# configuration, the scan of the password, the hashes and the UART strings, and deletes the build tree.
set -e
export PATH=/opt/crux-cache/venv/bin:$PATH
git config --global --add safe.directory '*'
export ZEPHYR_BASE=/ws/zephyr-wolf
export PYTHONDONTWRITEBYTECODE=1
export CRUX_BENCH_PASSWORD_FILE=/secret/c10.pw
K=/build/keep
B=/build/out/c10
M="/ws/crux-pilot;/ws/modules/crypto/wolfssl;/ws/modules/crypto/wolfssh;/ws/modules/crypto/wolfpsa;/ws/modules/crypto/mbedtls;/ws/modules/crypto/tf-psa-crypto;/ws/modules/fs/fatfs;/ws/modules/hal/cmsis;/ws/modules/hal/cmsis_6;/ws/modules/hal/nxp"
rm -rf $B
cd /ws/crux-pilot
echo "source: $(git rev-parse HEAD)"; git status --porcelain --untracked-files=no | wc -l
cmake -S /ws/crux-pilot/tests/zephyr/management_rt1060_compile -B $B -G Ninja -DBOARD='mimxrt1060_evk@B//qspi' "-DZEPHYR_MODULES=$M" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DPython3_EXECUTABLE=$(command -v python3) > /build/logs/configure.log 2>&1 || { tail -40 /build/logs/configure.log; echo CONFIGURE-FAILED; exit 1; }
echo configured
ninja -C $B -j2 > /build/logs/build.log 2>&1 || { grep -E "error|FAILED|undefined" /build/logs/build.log | head -30; echo BUILD-FAILED; exit 1; }
tail -12 /build/logs/build.log
SDK=/opt/crux-cache/sdk/zephyr-sdk-1.0.1/gnu/arm-zephyr-eabi/bin
E=$B/zephyr/zephyr.elf
$SDK/arm-zephyr-eabi-size $E | tee $K/size.txt
$SDK/arm-zephyr-eabi-size -A $E > $K/size-sections.txt
cp $B/bench-manifest.json $K/bench-manifest.json
cp $B/zephyr/.config $K/config.txt
gzip -9 -c $B/zephyr/zephyr.map > $K/zephyr.map.gz
sha256sum $B/zephyr/zephyr.bin $B/zephyr/zephyr.elf > $K/sha256.txt
ls -l $B/zephyr/zephyr.bin $B/zephyr/zephyr.elf > $K/file-sizes.txt
# the symbols of the call order: main() calls the check and the refusal, and the composition is still linked
$SDK/arm-zephyr-eabi-nm $E | grep -E " (T|t) (bench_main|main|crux_entropy_boot_check|crux_native_management_start|crux_native_management_services_start|crux_mgmt_rt1060_ssh_start|crux_bench_first_account|crux_bench_report_start)$" > $K/symbols-of-main.txt || true
cat $K/symbols-of-main.txt
# the disassembly of main (bench_main is inlined into it): the order of the calls
$SDK/arm-zephyr-eabi-objdump -d --no-show-raw-insn $E | awk '/<main>:/{f=1} f&&/^$/{exit} f' | grep -E "bl\s|<main>:" | sed 's/^ *[0-9a-f]*:\s*//' > $K/bench-main-calls.txt || true
cat $K/bench-main-calls.txt
{
echo "public test password in zephyr.elf: $(strings -a $E | grep -c 'composition test password' || true)"
echo "public test password in zephyr.bin: $(strings -a $B/zephyr/zephyr.bin | grep -c 'composition test password' || true)"
echo "CRUX-ENTROPY format string in zephyr.bin: $(strings -a $B/zephyr/zephyr.bin | grep -c 'CRUX-ENTROPY first-read=' || true)"
} > $K/strings-checks.txt
cat $K/strings-checks.txt
strings -a $B/zephyr/zephyr.bin | grep -E "CRUX-ENTROPY|BENCH refusing|BENCH image=|BENCH test forms" > $K/uart-strings-in-bin.txt || true
set +e
python3 /ws/crux-pilot/tests/zephyr/management_rt1060_compile/bench_first_account_file.py scan --password-file /secret/c10.pw \
  --build $B --repo /ws/crux-pilot --also /build/logs --allow-in-image --expect-in-image $E --manifest $B/bench-manifest.json > $K/scan-bench.txt 2>&1; echo "exit $?" >> $K/scan-bench.txt
cat $K/scan-bench.txt
rm -rf $B
echo BUILD-DONE
