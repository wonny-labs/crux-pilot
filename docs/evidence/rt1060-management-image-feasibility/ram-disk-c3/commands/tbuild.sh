# Target build of the bench image for mimxrt1060_evk@B//qspi on the read-only patched Zephyr copy of the C1 build
# (the same pin and patch: the bench guard checks the marker, the pin and the hashes). Keeps only logs, the map, the
# manifest, the resolved configuration and sizes, and removes the build directory.
set -e
export PATH=/opt/crux-cache/venv/bin:$PATH
git config --global --add safe.directory '*'
export ZEPHYR_BASE_OVERRIDE=/ws/zephyr-wolf
name=bench_c3
SDK=/opt/crux-cache/sdk/zephyr-sdk-1.0.1/gnu/arm-zephyr-eabi/bin
mkdir -p /build/keep
set +e
bash /build/scripts/zbuild.sh $name /ws/crux-pilot/tests/zephyr/management_rt1060_compile > /build/keep/build-summary.log 2>&1
status=$?
set -e
cp /build/$name.configure.log /build/$name.build.log /build/keep/ 2>/dev/null || true
tail -25 /build/keep/build-summary.log
if [ $status -eq 0 ]; then
  $SDK/arm-zephyr-eabi-size /build/$name/zephyr/zephyr.elf | tee /build/keep/size.txt
  $SDK/arm-zephyr-eabi-size -A /build/$name/zephyr/zephyr.elf > /build/keep/size-sections.txt
  cp /build/$name/zephyr/.config /build/keep/config.txt
  cp /build/$name/bench-manifest.json /build/keep/
  cp /build/$name/zephyr/zephyr.map /build/keep/ 2>/dev/null || true
  ls -l /build/$name/zephyr/zephyr.bin | tee /build/keep/bin-size.txt
  sha256sum /build/$name/zephyr/zephyr.bin /build/$name/zephyr/zephyr.elf > /build/keep/sha256.txt
  $SDK/arm-zephyr-eabi-nm -S --size-sort /build/$name/zephyr/zephyr.elf | tail -400 > /build/keep/nm-largest-400.txt
  $SDK/arm-zephyr-eabi-nm -S /build/$name/zephyr/zephyr.elf | grep -iE "disk_buf|ramdisk|ram_disk|sdmmc|usdhc|fs_mkfs|f_mkfs|fatfs_mkfs|bench_medium|disk_access" > /build/keep/medium-symbols.txt || true
  python3 /ws/crux-pilot/tests/zephyr/management_rt1060_compile/check_mkfs_callers.py $SDK/arm-zephyr-eabi-nm /build/$name ram > /build/keep/mkfs-callers.txt 2>&1; echo "mkfs callers exit $?" >> /build/keep/mkfs-callers.txt
  strings -a /build/$name/zephyr/zephyr.elf | grep -E "BENCH file-medium|nothing persists|composition test password" > /build/keep/strings-check.txt || true
  grep -E "CONFIG_(DISK_DRIVER|IMX_USDHC|CRUX_FILE_FATFS|CRUX_BENCH_FILE|FS_FATFS_MOUNT_MKFS|FILE_SYSTEM_MKFS|SDHC)" /build/$name/zephyr/.config > /build/keep/config-file-backend.txt || true
fi
rm -rf /build/$name
du -sh /build 2>/dev/null | tail -1
exit $status
