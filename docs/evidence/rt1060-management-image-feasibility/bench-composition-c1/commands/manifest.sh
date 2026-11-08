# Configure only, to write bench-manifest.json for the committed tree. Nothing is compiled.
export PATH=/opt/crux-cache/venv/bin:$PATH
git config --global --add safe.directory '*'
src=/ws/crux-pilot/tests/zephyr/management_rt1060_compile
M="/ws/crux-pilot;/ws/modules/crypto/wolfssl;/ws/modules/crypto/wolfssh;/ws/modules/crypto/wolfpsa;/ws/modules/crypto/mbedtls;/ws/modules/crypto/tf-psa-crypto;/ws/modules/fs/fatfs;/ws/modules/hal/cmsis;/ws/modules/hal/cmsis_6;/ws/modules/hal/nxp"
rm -rf /build/manifest_c1
ZEPHYR_BASE=/build/zephyr-wolf cmake -S $src -B /build/manifest_c1 -G Ninja -DBOARD='mimxrt1060_evk@B//qspi' "-DZEPHYR_MODULES=$M" -DPython3_EXECUTABLE=$(command -v python3) > /build/manifest_c1.configure.log 2>&1
echo "configure exit $?"
cp /build/manifest_c1/bench-manifest.json /build/manifest_c1.json
rm -rf /build/manifest_c1
