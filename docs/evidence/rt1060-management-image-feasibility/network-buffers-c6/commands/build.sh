# usage: build.sh NAME : compile and link the bench image of /ws/crux-pilot for mimxrt1060_evk@B//qspi (no board is
# touched), on C1's patched Zephyr copy at /ws/zephyr-wolf (read-only). Keeps the size report, the section sizes, the
# link map, the resolved configuration, the manifest and the sizes of the network symbols, and deletes the build tree.
set -e
export PATH=/opt/crux-cache/venv/bin:$PATH
git config --global --add safe.directory '*'
export ZEPHYR_BASE=/ws/zephyr-wolf
export PYTHONDONTWRITEBYTECODE=1
name=$1
M="/ws/crux-pilot;/ws/modules/crypto/wolfssl;/ws/modules/crypto/wolfssh;/ws/modules/crypto/wolfpsa;/ws/modules/crypto/mbedtls;/ws/modules/crypto/tf-psa-crypto;/ws/modules/fs/fatfs;/ws/modules/hal/cmsis;/ws/modules/hal/cmsis_6;/ws/modules/hal/nxp"
rm -rf /build/$name
cmake -S /ws/crux-pilot/tests/zephyr/management_rt1060_compile -B /build/$name -G Ninja -DBOARD='mimxrt1060_evk@B//qspi' "-DZEPHYR_MODULES=$M" -DPython3_EXECUTABLE=$(command -v python3) > /build/$name.configure.log 2>&1 || { tail -40 /build/$name.configure.log; echo CONFIGURE-FAILED; exit 1; }
echo configured
ninja -C /build/$name -j2 > /build/$name.build.log 2>&1 || { grep -E "error|FAILED|undefined" /build/$name.build.log | head -30; echo BUILD-FAILED; exit 1; }
tail -8 /build/$name.build.log
SDK=/opt/crux-cache/sdk/zephyr-sdk-1.0.1/gnu/arm-zephyr-eabi/bin
E=/build/$name/zephyr/zephyr.elf
$SDK/arm-zephyr-eabi-size $E | tee /build/$name.size.txt
$SDK/arm-zephyr-eabi-size -A $E > /build/$name.size-sections.txt
$SDK/arm-zephyr-eabi-nm -S --size-sort -r $E > /build/$name.nm-all.txt
grep -i -E "pkts|net_buf|nxp_enet_0|tcp_conns|malloc_arena|_net_buf|k_mem_slab_buf" /build/$name.nm-all.txt > /build/$name.net-symbols.txt || true
cp /build/$name/bench-manifest.json /build/$name.manifest.json
cp /build/$name/zephyr/.config /build/$name.config.txt
cp /build/$name/zephyr/zephyr.map /build/$name.map.txt 2>/dev/null || true
sha256sum /build/$name/zephyr/zephyr.bin /build/$name/zephyr/zephyr.elf > /build/$name.sha256.txt
ls -l /build/$name/zephyr/zephyr.bin > /build/$name.bin-size.txt
rm -rf /build/$name
echo BUILD-DONE
