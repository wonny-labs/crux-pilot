# usage: bench.sh : compile and link the C5 tree of the bench image for mimxrt1060_evk@B//qspi (no board is touched),
# then read the resolved random configuration and the call sites in the linked ELF
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
E=/build/$name/zephyr/zephyr.elf
$SDK/arm-zephyr-eabi-size $E | tee /build/$name.size.txt
cp /build/$name/bench-manifest.json /build/$name.bench-manifest.json
cp /build/$name.build.log /build/$name.build.keep.log
# The resolved random configuration: every symbol that selects a generator, an entropy driver or a CSPRNG.
grep -E "^(# )?CONFIG_(ENTROPY|XOSHIRO|TIMER_RANDOM|TEST_RANDOM|TEST_CSPRNG|CSPRNG|CTR_DRBG|PSA_CSPRNG|HARDWARE_DEVICE_CS|WOLFSSL_CSPRNG|NET_TCP_ISN|NET_SOCKETS_ENABLE_DTLS|WOLFSSL_DTLS|WOLFPSA|WOLFSSL|WOLFSSH|MBEDTLS|FAKE_ENTROPY|PSA_CRYPTO|PSA_WANT_ALG_SHA_256)[A-Z_0-9]*[= ]" /build/$name/zephyr/.config > /build/$name.entropy-config.txt
# The call sites in the linked image: every instruction that branches to a random function, whatever the branch.
OD=/build/$name.disasm.txt
$SDK/arm-zephyr-eabi-objdump -d --no-show-raw-insn $E > $OD
{
  echo "== symbols"
  $SDK/arm-zephyr-eabi-nm $E | grep -E " (z_impl_)?sys_(cs)?rand(32)?_get$|crux_entropy_boot|wc_GenerateSeed|entropy_mcux|z_impl_sys_csrand_get|random_psa" || true
  echo "== functions that branch to a random function (any branch instruction, tail calls included)"
  awk '/^[0-9a-f]+ <.*>:$/ {fn=$2} /<(z_impl_sys_rand_get|z_impl_sys_csrand_get|sys_rand_get|sys_csrand_get|sys_rand32_get)(\+0x[0-9a-f]+)?>/ && !/^[0-9a-f]+ <.*>:$/ {t=$NF; print fn, "->", t}' $OD | sort | uniq -c
  echo "== functions that branch to psa_generate_random"
  awk '/^[0-9a-f]+ <.*>:$/ {fn=$2} /<psa_generate_random(\+0x[0-9a-f]+)?>/ && !/^[0-9a-f]+ <.*>:$/ {print fn}' $OD | sort | uniq -c
  echo "== wc_GenerateSeed: every call or branch out of it (blx is an indirect call through the driver's function table)"
  awk '$2=="<wc_GenerateSeed>:" {on=1; next} on && /^$/ {on=0} on' $OD | grep -E "\sbl\s|\sblx\s|\sbx\s|\sb\.w\s|\sb\s|\sbl\.w\s" | sed 's/^[ ]*[0-9a-f]*:\s*//' | sort | uniq -c
} > /build/$name.call-sites.txt
awk '$2=="<wc_GenerateSeed>:" {on=1; print; next} on && /^$/ {on=0} on' $OD > /build/$name.wc_GenerateSeed.disasm.txt
cat /build/$name.call-sites.txt
echo BENCH-DONE
