# One cross-build of the bench image of /ws/crux-pilot (a clean checkout of origin/main) for mimxrt1060_evk@B//qspi, on C1's
# patched Zephyr copy at /ws/zephyr-wolf (read-only), with CRUX_BENCH_PASSWORD_FILE=/secret/c8.pw (a throwaway file). Keeps the size
# report, the manifest, the configuration, the map, the scan of the password and the hashes, and deletes the build tree.
set -e
export PATH=/opt/crux-cache/venv/bin:$PATH
git config --global --add safe.directory '*'
export ZEPHYR_BASE=/ws/zephyr-wolf
export PYTHONDONTWRITEBYTECODE=1
export CRUX_BENCH_PASSWORD_FILE=/secret/c8.pw
name=c8
K=/build/keep
B=/build/out/$name
M="/ws/crux-pilot;/ws/modules/crypto/wolfssl;/ws/modules/crypto/wolfssh;/ws/modules/crypto/wolfpsa;/ws/modules/crypto/mbedtls;/ws/modules/crypto/tf-psa-crypto;/ws/modules/fs/fatfs;/ws/modules/hal/cmsis;/ws/modules/hal/cmsis_6;/ws/modules/hal/nxp"
rm -rf $B
cd /ws/crux-pilot
echo "source: $(git rev-parse HEAD)"; git status --porcelain --untracked-files=no | wc -l
python3 - <<'PY' > $K/source-snapshot.json
import importlib.util, json
from pathlib import Path
spec = importlib.util.spec_from_file_location('p', '/ws/crux-pilot/scripts/record-build-provenance.py')
m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
print(json.dumps(m.source_snapshot(Path('/ws/crux-pilot')), indent=2, sort_keys=True))
PY
cat $K/source-snapshot.json
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
wc -c < $B/zephyr/zephyr.bin > $K/bin-bytes.txt
# the firmware identity as scripts/record-build-provenance.py computes it
python3 - <<'PY' > $K/firmware-identity.json
import importlib.util, json
from pathlib import Path
spec = importlib.util.spec_from_file_location('p', '/ws/crux-pilot/scripts/record-build-provenance.py')
m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
identity = m.firmware_identity(Path('/build/out/c8/zephyr'), 'mimxrt1060_evk@B//qspi')
print(json.dumps(dict(identity=identity, firmware_identity_digest=m.firmware_digest(identity)), indent=2, sort_keys=True))
PY
cat $K/firmware-identity.json
# strings: the public test password, and the 12-character commit tag
{
echo "public test password in zephyr.elf: $(strings -a $E | grep -c 'composition test password' || true)"
echo "public test password in zephyr.bin: $(strings -a $B/zephyr/zephyr.bin | grep -c 'composition test password' || true)"
C=$(git -C /ws/crux-pilot rev-parse HEAD | cut -c1-12)
echo "commit prefix $C in zephyr.bin: $(grep -a -c "$C" $B/zephyr/zephyr.bin || true)"
echo "commit prefix $C in zephyr.elf: $(grep -a -c "$C" $E || true)"
echo "BENCH image banner string in zephyr.bin: $(strings -a $B/zephyr/zephyr.bin | grep -c 'declaration=CRUX_MANAGEMENT_BENCH_IMAGE' || true)"
echo "image id in zephyr.bin: $(strings -a $B/zephyr/zephyr.bin | grep -c 'crux-mgmt-bench-rt1060-evkb-b' || true)"
} > $K/strings-checks.txt
cat $K/strings-checks.txt
set +e
# C4's scan: no plaintext of the password, or an encoding of it, outside the image files
python3 /ws/crux-pilot/tests/zephyr/management_rt1060_compile/bench_first_account_file.py scan --password-file /secret/c8.pw \
  --build $B --repo /ws/crux-pilot --also /build/logs --allow-in-image --expect-in-image $E --manifest $B/bench-manifest.json > $K/scan-bench.txt 2>&1; echo "exit $?" >> $K/scan-bench.txt
cat $K/scan-bench.txt
python3 /ws/crux-pilot/tests/zephyr/management_rt1060_compile/bench_first_account_file.py scan --password-file /secret/c8.pw \
  --build $B --repo /ws/crux-pilot --also /build/logs > $K/scan-bench-no-allowance.txt 2>&1; echo "exit $?" >> $K/scan-bench-no-allowance.txt
cat $K/scan-bench-no-allowance.txt

# for the host-side checks of the image profile candidate (outside the repository, deleted afterwards)
mkdir -p /build/keep-bin
cp $B/zephyr/zephyr.bin $B/zephyr/zephyr.elf $B/zephyr/.config /build/keep-bin/
cp $B/zephyr/zephyr.dts /build/keep-bin/ 2>/dev/null || true
$SDK/arm-zephyr-eabi-nm -S $E | grep -i -E "stack|stacks" > $K/stack-symbols.txt || true
grep -n -A12 "chosen {" $B/zephyr/zephyr.dts > $K/dts-chosen.txt || true
grep -n -B1 -A6 "random@400cc000" $B/zephyr/zephyr.dts > $K/dts-trng.txt || true
strings -a $B/zephyr/zephyr.bin | grep -E "random@400cc000|nxp,kinetis-trng|CRUX-ENTROPY|BENCH-CERT|BENCH-REPORT|SSH-HOST-KEY|BENCH ready|BENCH file-medium|BENCH first account|BENCH refusing" > $K/uart-strings-in-bin.txt || true
# kept for the host-side reading of the image as a freeze would see it
cp $B/compile_commands.json /build/keep/compile_commands.sha-only.json 2>/dev/null && sha256sum /build/keep/compile_commands.sha-only.json > $K/compile-commands-sha256.txt; rm -f /build/keep/compile_commands.sha-only.json
rm -rf $B
echo BUILD-DONE
