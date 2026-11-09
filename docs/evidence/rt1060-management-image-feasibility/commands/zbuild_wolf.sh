# usage: zbuild_wolf.sh NAME SOURCE_DIR [cmake args...]: a patched Zephyr copy for the wolfSSL TLS sockets, then zbuild.sh, then size/reports
set -e
export PATH=/opt/crux-cache/venv/bin:$PATH
git config --global --add safe.directory '*'
cd /ws/crux-pilot
scripts/prepare-wolf-tls-zephyr.sh /build/zephyr-wolf
cd /build
export ZEPHYR_BASE_OVERRIDE=/build/zephyr-wolf
name=$1
bash /build/scripts/zbuild.sh "$@"
SDK=/opt/crux-cache/sdk/zephyr-sdk-1.0.1/gnu/arm-zephyr-eabi/bin
ls /opt/crux-cache/sdk/zephyr-sdk-1.0.1/ >/dev/null
$SDK/arm-zephyr-eabi-size /build/$name/zephyr/zephyr.elf | tee /build/$name.size.txt
ninja -C /build/$name ram_report rom_report > /build/$name.reports.log 2>&1 || echo "reports failed"
