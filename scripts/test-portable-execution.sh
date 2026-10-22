#!/usr/bin/env bash
set -euo pipefail
if [[ "$(uname -s)" != Linux ]]; then
    printf 'Portable execution checks require the Linux development environment.\n' >&2
    exit 2
fi
repo="$(git rev-parse --show-toplevel)"
mode="${1:-all}"
if [[ "$mode" != all && "$mode" != --host-only ]]; then
    printf 'Usage: %s [--host-only]\n' "$0" >&2
    exit 2
fi
for consumer in firmware runtime; do
    out="$repo/build/execution-$consumer-host"
    cmake -S "$repo/tests/execution_$consumer" -B "$out" -G Ninja \
        -DCRUX_SOURCE_DIR="$repo" -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
    cmake --build "$out" --parallel 2
    ctest --test-dir "$out" --output-on-failure --output-junit "$out/ctest-junit.xml"
done
python3 "$repo/scripts/check-execution-boundaries.py" \
    --compile-commands "$repo/build/execution-firmware-host/compile_commands.json" \
    --compile-commands "$repo/build/execution-runtime-host/compile_commands.json" \
    --target-record "$repo/build/execution-firmware-host/consumer-boundary.json"
if [[ "$mode" == --host-only ]]; then
    exit 0
fi
workspace="${CRUX_WORKSPACE:-$(dirname "$repo")}"
export ZEPHYR_BASE="$workspace/zephyr"
mapfile -t dependencies < <(cd "$workspace" && west list cmsis cmsis_6 hal_nxp -f '{abspath}')
if [[ ${#dependencies[@]} != 3 ]]; then
    printf 'Expected three pinned west dependencies.\n' >&2
    exit 1
fi
dependencies+=("$repo")
export ZEPHYR_MODULES
ZEPHYR_MODULES="$(IFS=';'; printf '%s' "${dependencies[*]}")"
unset ZEPHYR_EXTRA_MODULES EXTRA_ZEPHYR_MODULES
west twister -j 2 -p native_sim/native/64 \
    -T "$repo/tests/portable_execution/zephyr" -T "$repo/tests/execution_runtime/zephyr" \
    -O "$repo/build/zephyr/twister-portable-execution" --inline-logs
for consumer in portable_execution execution_runtime; do
    out="$repo/build/zephyr/$consumer-evkb"
    west build -b 'mimxrt1060_evk@B//qspi' -d "$out" "$repo/tests/$consumer/zephyr"
    python3 "$repo/scripts/check-execution-boundaries.py" --compile-commands "$out/compile_commands.json"
done
# Cross-build only: no programming, reset, debugger, serial or hardware operations.
