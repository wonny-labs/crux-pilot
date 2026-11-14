# usage: c9_build.sh NAME [cmake args...]
# Compile and link the bench image for mimxrt1060_evk@B//qspi (no board is touched) on the read-only patched Zephyr
# copy of the C1 build, keep only logs, the map, sizes, the resolved configuration, the manifest, the symbol table and
# the static stack chains in /build/keep/NAME, and remove the build directory.
set -u
export PATH=/opt/crux-cache/venv/bin:$PATH
git config --global --add safe.directory '*'
export ZEPHYR_BASE=/ws/zephyr-wolf
export CRUX_BENCH_PASSWORD_FILE=/secret/first-account.password
export PYTHONDONTWRITEBYTECODE=1
name=$1; shift
K=/build/keep/$name
SDK=/opt/crux-cache/sdk/zephyr-sdk-1.0.1/gnu/arm-zephyr-eabi/bin
B=/build/$name
M="/ws/crux-pilot;/ws/modules/crypto/wolfssl;/ws/modules/crypto/wolfssh;/ws/modules/crypto/wolfpsa;/ws/modules/crypto/mbedtls;/ws/modules/crypto/tf-psa-crypto;/ws/modules/fs/fatfs;/ws/modules/hal/cmsis;/ws/modules/hal/cmsis_6;/ws/modules/hal/nxp"
mkdir -p $K; rm -rf $B
cmake -S /ws/crux-pilot/tests/zephyr/management_rt1060_compile -B $B -G Ninja -DBOARD='mimxrt1060_evk@B//qspi' "-DZEPHYR_MODULES=$M" -DPython3_EXECUTABLE=$(command -v python3) "$@" > $K/configure.log 2>&1
rc=$?; echo "configure exit $rc" > $K/status.txt
if [ $rc -ne 0 ]; then tail -40 $K/configure.log; rm -rf $B; exit 1; fi
ninja -C $B -j2 > $K/build.log 2>&1
rc=$?; echo "ninja exit $rc" >> $K/status.txt
if [ $rc -ne 0 ]; then grep -E "error|FAILED|undefined" $K/build.log | head -40; fi
E=$B/zephyr/zephyr.elf
if [ -f $E ]; then
  $SDK/arm-zephyr-eabi-size $E | tee $K/size.txt
  $SDK/arm-zephyr-eabi-size -A $E > $K/size-sections.txt
  $SDK/arm-zephyr-eabi-nm -S --size-sort -r $E > $K/nm-all.txt
  cp $B/zephyr/.config $K/config.txt
  cp $B/bench-manifest.json $K/ 2>/dev/null
  cp $B/zephyr/zephyr.map $K/zephyr.map
  cp $E $K/zephyr.elf
  sha256sum $B/zephyr/zephyr.bin $E > $K/sha256.txt
  ls -l $B/zephyr/zephyr.bin > $K/bin-size.txt
  cat /ws/crux-pilot/docs/evidence/rt1060-management-image-feasibility/commands/roots.txt /ws/crux-pilot/docs/evidence/rt1060-management-image-feasibility/commands/roots2.txt | sort -u > $K/roots.txt
  python3 /ws/crux-pilot/docs/evidence/rt1060-management-image-feasibility/commands/callgraph.py $E $K/roots.txt $K/static-stack-chains.json > $K/static-stack-chains.txt
  ninja -C $B ram_report rom_report > $K/reports.log 2>&1 || echo "reports failed" >> $K/status.txt
fi
rm -rf $B
du -sh /build 2>/dev/null | tail -1
