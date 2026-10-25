#!/usr/bin/env bash
set -euo pipefail
repo="$(git rev-parse --show-toplevel)"
mode="${1:-all}"
if [[ "$mode" != all && "$mode" != --host-only ]]; then
    printf 'Usage: %s [--host-only]\n' "$0" >&2
    exit 2
fi
cmake -S "$repo/tests/system_consumer" -B "$repo/build/system-consumer" -G Ninja
cmake --build "$repo/build/system-consumer" --target system_consumer system_consumer_policy --parallel 2
ctest --test-dir "$repo/build/system-consumer" --output-on-failure \
    --output-junit "$repo/build/system-consumer/ctest-junit.xml"
if [[ "$mode" == --host-only ]]; then exit 0; fi
workspace="${CRUX_WORKSPACE:-$(dirname "$repo")}"
export ZEPHYR_BASE="$workspace/zephyr"
CRUX_WORKSPACE="$workspace" CRUX_SYSTEM_COMPOSITION_TESTS=1 python3 "$repo/tests/build/test_system_composition.py"
(cd "$workspace" && west twister -j 2 -p native_sim/native/64 \
    -T "$repo/tests/system/zephyr" -T "$repo/tests/system_consumer/zephyr" \
    -O "$repo/build/zephyr/twister-system" --inline-logs --extra-args="ZEPHYR_MODULES=$repo")
mapfile -t modules < <(cd "$workspace" && west list cmsis cmsis_6 hal_nxp -f '{abspath}')
modules+=("$repo")
modules_text="$(IFS=';'; printf '%s' "${modules[*]}")"
(cd "$workspace" && west build -b 'mimxrt1060_evk@B//qspi' \
    -d "$repo/build/zephyr/system-evkb" "$repo/tests/system_consumer/zephyr" -- \
    -DZEPHYR_MODULES="$modules_text" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON)
# Build only: no programming, reset, feed, clear or hardware access.

# Whole-image accounting; keep all six operations linked without invoking them.
evidence="$repo/build/system-resource-evidence"
build="$repo/build/zephyr/system-evkb"
mkdir -p "$evidence"
sdk="${ZEPHYR_SDK_INSTALL_DIR:-$(sed -n 's/^ZEPHYR_SDK_INSTALL_DIR:INTERNAL=//p' "$build/CMakeCache.txt")}"
cross="$sdk/gnu/arm-zephyr-eabi/bin/arm-zephyr-eabi"
"$cross-size" "$build/zephyr/zephyr.elf" > "$evidence/size.txt"
"$cross-nm" -S --size-sort --radix=d "$build/zephyr/zephyr.elf" > "$evidence/symbols.txt"
for operation in crux_system_status_get crux_system_reset_cause_get \
    crux_system_reset_cause_clear crux_system_reset crux_watchdog_start crux_watchdog_feed; do
    awk -v symbol="$operation" '$3 == "T" && $4 == symbol {found=1} END {exit !found}' "$evidence/symbols.txt"
done
cp "$build/zephyr/.config" "$evidence/selected.config"
(cd "$build" && sha256sum zephyr/.config zephyr/zephyr.dts zephyr/zephyr.elf) > "$evidence/sha256.txt"
(cd "$workspace" && west list zephyr cmsis cmsis_6 hal_nxp -f '{name} {revision} {path}') > "$evidence/modules.txt"
(cd "$repo" && git ls-files --cached --others --exclude-standard \
    components/system tests/system tests/system_consumer samples/controller/system \
    zephyr cmake/CruxConsumerBoundary.cmake scripts/test-system.sh | sort -u | \
    while IFS= read -r file; do sha256sum "$file"; done) > "$evidence/input-sha256.txt"
