# Mutants of the first account step on the Host suite, then the incremental rebuild of the bench image from the committed tree.
export PATH=/opt/crux-cache/venv/bin:$PATH
git config --global --add safe.directory '*'
cd /ws/crux-pilot
A=components/access/src
mkdir -p /tmp/h /tmp/m
for f in components/sync/ports/linux/sync_native.c components/interrupt/ports/linux/interrupt_linux.c; do
  gcc -std=c17 -D_GNU_SOURCE -Icomponents/sync/include -Icomponents/sync/src -Icomponents/sync/ports/linux -Icomponents/base/include -Icomponents/interrupt/src -c $f -o /tmp/h/$(basename $f).o
done
CORE=tests/zephyr/management_rt1060_compile/first_account/bench_first_account_provision.c
runsuite() { # core source file
  gcc -std=c17 -Wall -Wextra -Wpedantic -UNDEBUG -D_POSIX_C_SOURCE=200809L -DCRUX_ACCESS_TESTING=1 -DCRUX_ACCESS_TEST_COMPOSITION=1 \
    -Itests/access -I$A -Icomponents/access/include -Icomponents/access/adapter -Icomponents/sync/include -Icomponents/sync/src \
    -Icomponents/sync/ports/linux -Icomponents/base/include -Icomponents/interrupt/src -Itests/zephyr/management_rt1060_compile/first_account \
    $A/access.c $A/access_accounts.c $A/access_delay.c $A/access_login.c $A/access_record.c $A/access_session.c \
    tests/access/fake_ports.c tests/access/access_test.c components/sync/src/sync.c /tmp/h/sync_native.c.o /tmp/h/interrupt_linux.c.o \
    $1 tests/bench_first_account/first_account.c -o /tmp/m/t -lpthread >/dev/null 2>&1 || { echo compile-failed; return 2; }
  /tmp/m/t > /tmp/m/out.txt 2>&1; return $?
}
echo "== mutants of the step, run on the Host suite"
runsuite $CORE && echo "control: the real step passes"
python3 - <<'PY'
src = open("tests/zephyr/management_rt1060_compile/first_account/bench_first_account_provision.c").read()
mutants = [
 ("the newline is not removed", "if (length > 0u && file[length - 1u] == (uint8_t)'\\n') {\n        length -= 1u;\n    }", ""),
 ("no lower length limit", "length < CRUX_BENCH_FIRST_ACCOUNT_LENGTH_MIN ||", ""),
 ("no upper length limit", "|| length > CRUX_BENCH_FIRST_ACCOUNT_LENGTH_MAX", ""),
 ("no character rule", "if (file[index] < 0x21u || file[index] > 0x7eu) {", "if (0) {"),
 ("a space is allowed", "file[index] < 0x21u", "file[index] < 0x20u"),
 ("the web origin", ".channel = CRUX_ACCESS_CHANNEL_SERIAL", ".channel = CRUX_ACCESS_CHANNEL_WEB"),
 ("one byte too many", "(const char *)file, length, role", "(const char *)file, length + 1u, role"),
 ("one byte too few", "(const char *)file, length, role", "(const char *)file, length - 1u, role"),
 ("the user is ignored", "crux_access_provision_first_account(user,", "crux_access_provision_first_account(\"root\","),
 ("the role is ignored", "length, role, &serial", "length, \"operator\", &serial"),
 ("a null user is accepted", "if (user == NULL || role == NULL || file == NULL) {", "if (role == NULL || file == NULL) {"),
]
for n, (name, old, new) in enumerate(mutants):
    assert old in src, name
    open(f"/tmp/m/mutant{n}.c", "w").write(src.replace(old, new, 1))
    open(f"/tmp/m/mutant{n}.name", "w").write(name)
print(len(mutants))
PY
for f in /tmp/m/mutant*.c; do
  n=$(basename $f .c); runsuite $f; rc=$?
  if [ $rc -eq 0 ]; then echo "SURVIVED $(cat /tmp/m/$n.name)"; else echo "killed   $(cat /tmp/m/$n.name)"; fi
done
echo "== incremental rebuild of the bench image from the committed tree"
cd /build
export ZEPHYR_BASE=/build/zephyr-wolf
export CRUX_BENCH_PASSWORD_FILE=/secret/first-account.password
M="/ws/crux-pilot;/ws/modules/crypto/wolfssl;/ws/modules/crypto/wolfssh;/ws/modules/crypto/wolfpsa;/ws/modules/crypto/mbedtls;/ws/modules/crypto/tf-psa-crypto;/ws/modules/fs/fatfs;/ws/modules/hal/cmsis;/ws/modules/hal/cmsis_6;/ws/modules/hal/nxp"
SRC=/ws/crux-pilot/tests/zephyr/management_rt1060_compile
cmake -S $SRC -B /build/c4_bench -G Ninja -DBOARD='mimxrt1060_evk@B//qspi' "-DZEPHYR_MODULES=$M" -DPython3_EXECUTABLE=$(command -v python3) > /build/c4_bench.reconfigure.log 2>&1; echo "reconfigure exit $?"
ninja -C /build/c4_bench -j2 > /build/c4_bench.rebuild.log 2>&1 || { grep -E "error|FAILED|undefined" /build/c4_bench.rebuild.log | head -30; echo BUILD-FAILED; exit 1; }
tail -14 /build/c4_bench.rebuild.log
SDK=/opt/crux-cache/sdk/zephyr-sdk-1.0.1/gnu/arm-zephyr-eabi/bin
E=/build/c4_bench/zephyr/zephyr.elf
$SDK/arm-zephyr-eabi-size $E | tee /build/c4_bench.size.txt
$SDK/arm-zephyr-eabi-size -A $E > /build/c4_bench.size-sections.txt
$SDK/arm-zephyr-eabi-nm -S $E | grep -i "first_account" > /build/c4_bench.first-account-symbols.txt
$SDK/arm-zephyr-eabi-readelf -SW $E | grep -E "Nr|rodata" > /build/c4_bench.sections.txt
$SDK/arm-zephyr-eabi-objdump -t $E | grep -i "crux_bench_first_account_password" > /build/c4_bench.symbol-section.txt
cp /build/c4_bench/bench-manifest.json /build/c4_bench.manifest.json
sha256sum $E /build/c4_bench/zephyr/zephyr.bin > /build/c4_bench.sha256.txt
echo DONE
