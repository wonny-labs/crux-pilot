# Configure-only breaks of the bench image: each must stop at configuration, and nothing is compiled.
export PATH=/opt/crux-cache/venv/bin:$PATH
git config --global --add safe.directory '*'
src=/ws/crux-pilot/tests/zephyr/management_rt1060_compile
M="/ws/crux-pilot;/ws/modules/crypto/wolfssl;/ws/modules/crypto/wolfssh;/ws/modules/crypto/wolfpsa;/ws/modules/crypto/mbedtls;/ws/modules/crypto/tf-psa-crypto;/ws/modules/fs/fatfs;/ws/modules/hal/cmsis;/ws/modules/hal/cmsis_6;/ws/modules/hal/nxp"
run() { # name zephyr_base cmake-args...
  name=$1; zb=$2; shift 2
  rm -rf /build/$name
  ZEPHYR_BASE=$zb cmake -S $src -B /build/$name -G Ninja -DBOARD='mimxrt1060_evk@B//qspi' "-DZEPHYR_MODULES=$M" -DPython3_EXECUTABLE=$(command -v python3) "$@" > /build/$name.log 2>&1
  echo "== $name: cmake exit $?" | tee -a /build/breaks-summary.txt
  grep -E "warning:|error:|CMake Error|Aborting|The bench|bench image|reserved|ZEPHYR_BASE is not|must listen|Kconfig declaration" /build/$name.log | sed 's#/ws/crux-pilot/##; s#/build/#build/#' | head -8 | tee -a /build/breaks-summary.txt
  rm -rf /build/$name
}
: > /build/breaks-summary.txt
run brk_dhcp /build/zephyr-wolf -DCONFIG_NET_DHCPV4=y
run brk_rtc /build/zephyr-wolf -DCONFIG_RTC=y
run brk_bind /build/zephyr-wolf '-DCONFIG_CRUX_WEB_BIND_ADDRESS="0.0.0.0"'
run brk_sshbind /build/zephyr-wolf '-DCONFIG_CRUX_SSH_BIND_ADDRESS=""'
run brk_ramdisk /build/zephyr-wolf -DCONFIG_CRUX_BENCH_FILE_RAM_DISK=y
run brk_unpatched /ws/zephyr
