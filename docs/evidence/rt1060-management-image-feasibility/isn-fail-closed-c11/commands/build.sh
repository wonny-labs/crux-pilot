# usage: build.sh NAME COPY SRC : one cross-build of the bench image of SRC (a clean detached checkout) for
# mimxrt1060_evk@B//qspi, on the Zephyr copy at /build/COPY (read-only use), with CRUX_BENCH_PASSWORD_FILE=<password file>
# (a throwaway file). Keeps the size report, the manifest, the configuration, the call-site scan of the ELF and the
# scan for the password, and deletes the build tree. No board is touched.
set -e
export PATH=/opt/crux-cache/venv/bin:$PATH
git config --global --add safe.directory '*'
export PYTHONDONTWRITEBYTECODE=1
export CRUX_BENCH_PASSWORD_FILE=<password file>
name=$1
export ZEPHYR_BASE=/build/$2
SRC=$3
K=/build/keep/$name
B=/build/out/$name
M="$SRC;/ws/modules/crypto/wolfssl;/ws/modules/crypto/wolfssh;/ws/modules/crypto/wolfpsa;/ws/modules/crypto/mbedtls;/ws/modules/crypto/tf-psa-crypto;/ws/modules/fs/fatfs;/ws/modules/hal/cmsis;/ws/modules/hal/cmsis_6;/ws/modules/hal/nxp"
mkdir -p $K /build/logs
rm -rf $B
cd $SRC
echo "source: $(git rev-parse HEAD) modified-files: $(git status --porcelain --untracked-files=no | wc -l)" | tee $K/source.txt
cmake -S $SRC/tests/zephyr/management_rt1060_compile -B $B -G Ninja -DBOARD='mimxrt1060_evk@B//qspi' "-DZEPHYR_MODULES=$M" -DPython3_EXECUTABLE=$(command -v python3) > $K/configure.log 2>&1 || { tail -40 $K/configure.log; echo CONFIGURE-FAILED; exit 1; }
echo configured
ninja -C $B -j2 > $K/build.log 2>&1 || { grep -E "error|FAILED|undefined" $K/build.log | head -30; echo BUILD-FAILED; exit 1; }
tail -6 $K/build.log
SDK=/opt/crux-cache/sdk/zephyr-sdk-1.0.1/gnu/arm-zephyr-eabi/bin
E=$B/zephyr/zephyr.elf
$SDK/arm-zephyr-eabi-size $E | tee $K/size.txt
$SDK/arm-zephyr-eabi-size -A $E > $K/size-sections.txt
cp $B/bench-manifest.json $K/bench-manifest.json
sha256sum $B/zephyr/zephyr.bin $B/zephyr/zephyr.elf > $K/sha256.txt
ls -l $B/zephyr/zephyr.bin $B/zephyr/zephyr.elf > $K/file-sizes.txt
# The resolved configuration that matters here.
grep -E "^(# )?CONFIG_(ENTROPY_MCUX_TRNG|ENTROPY_HAS_DRIVER|XOSHIRO|CSPRNG|PSA_CSPRNG|HARDWARE_DEVICE_CS|NET_TCP_ISN|NET_TEST|NET_IPV6|NET_IPV4|NET_TCP|WOLFPSA|PSA_WANT_ALG_SHA_256)[A-Z_0-9]*[= ]" $B/zephyr/.config > $K/isn-config.txt
# The call sites in the linked image.
OD=/build/out/$name.disasm.txt
$SDK/arm-zephyr-eabi-objdump -d --no-show-raw-insn $E > $OD
{
  echo "== symbols"
  $SDK/arm-zephyr-eabi-nm $E | grep -E " (z_impl_)?sys_(cs)?rand(32)?_get$|tcp_init_isn|tcp_isn_key_get|tcpv4_init_isn|tcpv6_init_isn|psa_hash_compute$|z_impl_sys_csrand_get" || true
  echo "== functions that branch to a random function (any branch instruction, tail calls included)"
  awk '/^[0-9a-f]+ <.*>:$/ {fn=$2} /<(z_impl_sys_rand_get|z_impl_sys_csrand_get|sys_rand_get|sys_csrand_get|sys_rand32_get)(\+0x[0-9a-f]+)?>/ && !/^[0-9a-f]+ <.*>:$/ {t=$NF; print fn, "->", t}' $OD | grep -v "^<z_impl_sys_" | sort | uniq -c
  echo "== functions that branch to psa_hash_compute"
  awk '/^[0-9a-f]+ <.*>:$/ {fn=$2} /<psa_hash_compute(\+0x[0-9a-f]+)?>/ && !/^[0-9a-f]+ <.*>:$/ {print fn}' $OD | sort | uniq -c
} > $K/call-sites.txt
# The disassembly of every function that calls sys_csrand_get (here the TCP initial sequence number), whole.
for fn in $(awk '/^[0-9a-f]+ <.*>:$/ {fn=$2} /<z_impl_sys_csrand_get(\+0x[0-9a-f]+)?>/ && !/^[0-9a-f]+ <.*>:$/ {print fn}' $OD | grep -v "^<z_impl_sys_" | sort -u); do
  safe=$(echo $fn | tr -d '<>:')
  awk -v f="$fn" '$2==f {on=1; print; next} on && /^$/ {on=0} on' $OD > $K/disasm-$safe.txt
done
cat $K/call-sites.txt
# C4's scan: no plaintext of the password, or an encoding of it, outside the image files
set +e
python3 /ws/crux-pilot/tests/zephyr/management_rt1060_compile/bench_first_account_file.py scan --password-file <password file> \
  --build $B --repo $SRC --also $K --allow-in-image --expect-in-image $E --manifest $B/bench-manifest.json > $K/scan-bench.txt 2>&1; echo "exit $?" >> $K/scan-bench.txt
cat $K/scan-bench.txt
rm -rf $B $OD
echo BUILD-DONE $name
