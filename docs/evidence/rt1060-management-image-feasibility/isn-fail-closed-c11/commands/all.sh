# All of row C11's heavy steps, one after another, in one container: the native_sim red and green runs, the copies of the
# Zephyr tree, the negative control of the real configure, and two cross-builds (before and after).
# /ws/crux-pilot is the AFTER tree (the commit under test). /ws/before is the BEFORE tree (origin/main).
set -e
export PATH=/opt/crux-cache/venv/bin:$PATH
git config --global --add safe.directory '*'
export PYTHONDONTWRITEBYTECODE=1
K=/build/keep
mkdir -p $K /build/logs
echo "start $(date -u +%FT%TZ)"
# 1. native_sim: red on the pinned tree, green on the patched copy
CRUX_ISN_OUTPUT=/build/out/isn bash -c 'cd /ws/crux-pilot && scripts/test-isn-fail-closed.sh all' > $K/native-all.txt 2>&1 && echo "native ok" || { echo "native FAILED"; tail -20 $K/native-all.txt; exit 1; }
mkdir -p $K/native; cp /build/out/isn/*.txt /build/out/isn/*.log $K/native/ 2>/dev/null || true
rm -rf /build/out/isn
# 2. the Zephyr copies: TLS patch only (the "before" copy), then a second copy with the ISN patch too (the "after" copy)
cd /ws/crux-pilot
rm -rf /build/zephyr-wolf /build/zephyr-bench
scripts/prepare-wolf-tls-zephyr.sh /build/zephyr-wolf | tee $K/prepare-tls.txt
sha256sum /build/zephyr-wolf/subsys/net/ip/tcp.c /build/zephyr-wolf/subsys/net/lib/sockets/sockets_tls.c > $K/tls-copy-hashes.txt
cp -a /build/zephyr-wolf /build/zephyr-bench
scripts/prepare-isn-fail-closed.py /build/zephyr-bench | tee $K/prepare-isn.txt
sha256sum /build/zephyr-bench/subsys/net/ip/tcp.c /build/zephyr-bench/subsys/net/lib/sockets/sockets_tls.c > $K/bench-copy-hashes.txt
cat /build/zephyr-bench/.crux-wolf-tls /build/zephyr-bench/.crux-isn-fail-closed > $K/markers.txt
# a second run is a no-op, and the checkout of the workspace is refused
scripts/prepare-isn-fail-closed.py /build/zephyr-bench | tee $K/prepare-isn-again.txt
scripts/prepare-isn-fail-closed.py /ws/zephyr > $K/prepare-isn-refuses-checkout.txt 2>&1 && echo "CHECKOUT ACCEPTED" >> $K/prepare-isn-refuses-checkout.txt || true
cat $K/prepare-isn-refuses-checkout.txt
# 3. the negative control: the real configure of the AFTER tree on the copy with the TLS patch only must be refused
set +e
M="/ws/crux-pilot;/ws/modules/crypto/wolfssl;/ws/modules/crypto/wolfssh;/ws/modules/crypto/wolfpsa;/ws/modules/crypto/mbedtls;/ws/modules/crypto/tf-psa-crypto;/ws/modules/fs/fatfs;/ws/modules/hal/cmsis;/ws/modules/hal/cmsis_6;/ws/modules/hal/nxp"
rm -rf /build/out/control
ZEPHYR_BASE=/build/zephyr-wolf CRUX_BENCH_PASSWORD_FILE=/secret/c11.pw cmake -S /ws/crux-pilot/tests/zephyr/management_rt1060_compile -B /build/out/control -G Ninja -DBOARD='mimxrt1060_evk@B//qspi' "-DZEPHYR_MODULES=$M" -DPython3_EXECUTABLE=$(command -v python3) > $K/control-tls-only-configure.log 2>&1
echo "configure exit $?" >> $K/control-tls-only-configure.log
grep -n "fail-closed TCP initial sequence\|CMake Error\|configure exit" $K/control-tls-only-configure.log | head
rm -rf /build/out/control
set -e
# 4. the cross-builds. BEFORE: origin/main's tree on the TLS-only copy. AFTER: this commit on the copy with both patches.
bash /build/scripts/build.sh before zephyr-wolf /ws/before
bash /build/scripts/build.sh after zephyr-bench /ws/crux-pilot
echo "end $(date -u +%FT%TZ)"
rm -rf /build/zephyr-wolf /build/zephyr-bench
