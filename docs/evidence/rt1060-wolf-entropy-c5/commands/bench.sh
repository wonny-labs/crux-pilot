# usage: bench.sh : compile and link the C5 tree of the bench image for mimxrt1060_evk@B//qspi (no board is touched)
set -e
export PATH=/opt/crux-cache/venv/bin:$PATH
git config --global --add safe.directory '*'
export ZEPHYR_BASE=/ws/zephyr-wolf
name=bench_c5
M="/ws/crux-pilot;/ws/modules/crypto/wolfssl;/ws/modules/crypto/wolfssh;/ws/modules/crypto/wolfpsa;/ws/modules/crypto/mbedtls;/ws/modules/crypto/tf-psa-crypto;/ws/modules/fs/fatfs;/ws/modules/hal/cmsis;/ws/modules/hal/cmsis_6;/ws/modules/hal/nxp"
rm -rf /build/$name
cmake -S /ws/crux-pilot/tests/zephyr/management_rt1060_compile -B /build/$name -G Ninja -DBOARD='mimxrt1060_evk@B//qspi' "-DZEPHYR_MODULES=$M" -DPython3_EXECUTABLE=$(command -v python3) > /build/$name.configure.log 2>&1 || { tail -40 /build/$name.configure.log; echo CONFIGURE-FAILED; exit 1; }
echo configured
ninja -C /build/$name -j2 > /build/$name.build.log 2>&1 || { grep -E "error|FAILED|undefined" /build/$name.build.log | head -30; echo BUILD-FAILED; exit 1; }
tail -8 /build/$name.build.log
SDK=/opt/crux-cache/sdk/zephyr-sdk-1.0.1/gnu/arm-zephyr-eabi/bin
$SDK/arm-zephyr-eabi-size /build/$name/zephyr/zephyr.elf | tee /build/$name.size.txt
cp /build/$name/zephyr/.config /build/$name.config.txt
cp /build/$name/bench-manifest.json /build/$name.bench-manifest.json
$SDK/arm-zephyr-eabi-nm /build/$name/zephyr/zephyr.elf | grep -E "crux_entropy_boot|sys_rand_get|sys_csrand_get|wc_GenerateSeed|entropy_mcux" > /build/$name.symbols.txt || true
grep -c "" /build/$name.symbols.txt
echo BENCH-DONE
