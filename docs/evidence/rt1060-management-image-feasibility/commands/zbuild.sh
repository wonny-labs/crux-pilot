# usage: zbuild.sh NAME SOURCE_DIR [cmake args...]   (compile and link only; RT1060-EVKB rev B, QSPI)
set -e
name=$1; src=$2; shift 2
export PATH=/opt/crux-cache/venv/bin:$PATH
git config --global --add safe.directory '*'
export ZEPHYR_BASE=${ZEPHYR_BASE_OVERRIDE:-/ws/zephyr}
M="/ws/crux-pilot;/ws/modules/crypto/wolfssl;/ws/modules/crypto/wolfssh;/ws/modules/crypto/wolfpsa;/ws/modules/crypto/mbedtls;/ws/modules/crypto/tf-psa-crypto;/ws/modules/fs/fatfs;/ws/modules/hal/cmsis;/ws/modules/hal/cmsis_6;/ws/modules/hal/nxp"
[ -n "$KEEP" ] || rm -rf /build/$name
cmake -S $src -B /build/$name -G Ninja -DBOARD='mimxrt1060_evk@B//qspi' "-DZEPHYR_MODULES=$M" -DPython3_EXECUTABLE=$(command -v python3) "$@" > /build/$name.configure.log 2>&1 || { tail -40 /build/$name.configure.log; echo CONFIGURE-FAILED; exit 1; }
echo configured
ninja -C /build/$name -j2 > /build/$name.build.log 2>&1 || { grep -E "error|FAILED|undefined" /build/$name.build.log | head -30; echo BUILD-FAILED; exit 1; }
tail -22 /build/$name.build.log
