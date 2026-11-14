# Row C4, one container run: the Access host suite, three configure-only refusals, and the bench image build.
# The password file is mounted read-only at /secret and named by CRUX_BENCH_PASSWORD_FILE. Nothing here prints it.
export PATH=/opt/crux-cache/venv/bin:$PATH
git config --global --add safe.directory '*'
cd /ws/crux-pilot
A=components/access/src
echo "== host: Access suite for the first account step"
mkdir -p /tmp/h
gcc -std=c17 -Wall -Wextra -Wpedantic -Werror -UNDEBUG -D_POSIX_C_SOURCE=200809L -DCRUX_ACCESS_TESTING=1 \
  -DCRUX_ACCESS_TEST_COMPOSITION=1 -Itests/access -I$A -Icomponents/access/include -Icomponents/access/adapter \
  -Icomponents/sync/include -Icomponents/sync/src -Icomponents/sync/ports/linux -Icomponents/base/include \
  -Icomponents/interrupt/src -Icomponents/interrupt/include -Itests/zephyr/management_rt1060_compile/first_account \
  $A/access.c $A/access_accounts.c $A/access_delay.c $A/access_login.c $A/access_record.c $A/access_session.c \
  tests/access/fake_ports.c tests/access/access_test.c components/sync/src/sync.c components/sync/ports/linux/sync_native.c \
  tests/zephyr/management_rt1060_compile/first_account/bench_first_account_provision.c \
  tests/bench_first_account/first_account.c -o /tmp/h/t -lpthread && /tmp/h/t
echo "host suite exit $?" 
echo "== host: python tests in the CI-like container"
python3 -m unittest tests/build/test_bench_first_account_file.py tests/build/test_bench_first_account_build.py \
  tests/build/test_management_bench_declaration.py tests/build/test_management_target_lists.py 2>&1 | tail -8
bash scripts/prepare-wolf-tls-zephyr.sh /build/zephyr-wolf
cd /build
export ZEPHYR_BASE=/build/zephyr-wolf
M="/ws/crux-pilot;/ws/modules/crypto/wolfssl;/ws/modules/crypto/wolfssh;/ws/modules/crypto/wolfpsa;/ws/modules/crypto/mbedtls;/ws/modules/crypto/tf-psa-crypto;/ws/modules/fs/fatfs;/ws/modules/hal/cmsis;/ws/modules/hal/cmsis_6;/ws/modules/hal/nxp"
SRC=/ws/crux-pilot/tests/zephyr/management_rt1060_compile
configure() { # name, then the environment of the run is the caller's
  rm -rf /build/$1
  cmake -S $SRC -B /build/$1 -G Ninja -DBOARD='mimxrt1060_evk@B//qspi' "-DZEPHYR_MODULES=$M" -DPython3_EXECUTABLE=$(command -v python3) > /build/$1.configure.log 2>&1
  echo "configure $1 exit $?"
}
echo "== configure-only refusals"
( unset CRUX_BENCH_PASSWORD_FILE; configure c4_nofile )
( CRUX_BENCH_PASSWORD_FILE=/secret/empty.password; export CRUX_BENCH_PASSWORD_FILE; configure c4_empty )
( CRUX_BENCH_PASSWORD_FILE=/secret/short.password; export CRUX_BENCH_PASSWORD_FILE; configure c4_short )
( CRUX_BENCH_PASSWORD_FILE=/secret/missing.password; export CRUX_BENCH_PASSWORD_FILE; configure c4_missing )
for n in nofile empty short missing; do echo "-- $n"; grep -A6 "CMake Error" /build/c4_$n.configure.log | head -12; done
echo "== bench image build"
export CRUX_BENCH_PASSWORD_FILE=/secret/first-account.password
configure c4_bench || { tail -30 /build/c4_bench.configure.log; echo CONFIGURE-FAILED; exit 1; }
ninja -C /build/c4_bench -j2 > /build/c4_bench.build.log 2>&1 || { grep -E "error|FAILED|undefined" /build/c4_bench.build.log | head -30; echo BUILD-FAILED; exit 1; }
tail -22 /build/c4_bench.build.log
SDK=/opt/crux-cache/sdk/zephyr-sdk-1.0.1/gnu/arm-zephyr-eabi/bin
$SDK/arm-zephyr-eabi-size /build/c4_bench/zephyr/zephyr.elf | tee /build/c4_bench.size.txt
E=/build/c4_bench/zephyr/zephyr.elf
$SDK/arm-zephyr-eabi-size -A $E > /build/c4_bench.size-sections.txt
$SDK/arm-zephyr-eabi-nm -S $E | grep -i "first_account" > /build/c4_bench.first-account-symbols.txt
$SDK/arm-zephyr-eabi-readelf -SW $E | grep -iE "rodata|Name" | head -5 > /build/c4_bench.sections.txt
cp /build/c4_bench/bench-manifest.json /build/c4_bench.manifest.json
sha256sum /build/c4_bench/zephyr/zephyr.elf /build/c4_bench/zephyr/zephyr.bin > /build/c4_bench.sha256.txt
echo DONE
