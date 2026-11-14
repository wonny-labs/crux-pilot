# usage: target.sh NAME SOURCE_DIR   the bench image for mimxrt1060_evk@B//qspi on the patched Zephyr copy at /zw (read-only)
set -e
export PATH=/opt/crux-cache/venv/bin:$PATH
git config --global --add safe.directory '*'
export ZEPHYR_BASE_OVERRIDE=/zw
export PYTHONDONTWRITEBYTECODE=1
name=$1
bash /build/scripts/zbuild.sh "$@"
SDK=/opt/crux-cache/sdk/zephyr-sdk-1.0.1/gnu/arm-zephyr-eabi/bin
E=/build/$name/zephyr/zephyr.elf
$SDK/arm-zephyr-eabi-size $E | tee /build/$name.size.txt
$SDK/arm-zephyr-eabi-size -A $E > /build/$name.size-sections.txt
$SDK/arm-zephyr-eabi-nm -S --size-sort -r $E > /build/$name.nm-all.txt
ninja -C /build/$name ram_report rom_report > /build/$name.reports.log 2>&1 || echo "reports failed"
cp /build/$name/bench-manifest.json /build/$name.manifest.json 2>/dev/null || true
cp /build/$name/zephyr/.config /build/$name.config.txt
cp /build/$name/zephyr/zephyr.map /build/$name.map.txt 2>/dev/null || true
sha256sum /build/$name/zephyr/zephyr.bin /build/$name/zephyr/zephyr.elf > /build/$name.sha256.txt
ls -l /build/$name/zephyr/zephyr.bin | tee -a /build/$name.size.txt
