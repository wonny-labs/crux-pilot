#!/usr/bin/env bash
set -euo pipefail
repo="$(git rev-parse --show-toplevel)"
mode="${1:-all}"
if [[ "$mode" != all && "$mode" != --host-only ]]; then
    printf 'Usage: %s [--host-only]\n' "$0" >&2
    exit 2
fi
cmake -S "$repo/tests/logging_consumer" -B "$repo/build/logging-consumer" -G Ninja
cmake --build "$repo/build/logging-consumer" --target logging_consumer logging_consumer_policy --parallel 2
ctest --test-dir "$repo/build/logging-consumer" --output-on-failure \
    --output-junit "$repo/build/logging-consumer/ctest-junit.xml"
if [[ "$mode" == --host-only ]]; then exit 0; fi
workspace="${CRUX_WORKSPACE:-$(dirname "$repo")}"
export ZEPHYR_BASE="$workspace/zephyr"
CRUX_WORKSPACE="$workspace" CRUX_LOGGING_COMPOSITION_TESTS=1 python3 "$repo/tests/build/test_logging_composition.py"
(cd "$workspace" && west twister -j 2 -p native_sim/native/64 \
    -T "$repo/tests/logging/zephyr" -O "$repo/build/zephyr/twister-logging" \
    --inline-logs --extra-args="ZEPHYR_MODULES=$repo")
mapfile -t modules < <(cd "$workspace" && west list cmsis cmsis_6 hal_nxp -f '{abspath}')
modules+=("$repo")
modules_text="$(IFS=';'; printf '%s' "${modules[*]}")"
(cd "$workspace" && west build -b 'mimxrt1060_evk@B//qspi' \
    -d "$repo/build/zephyr/logging-evkb" "$repo/tests/logging_consumer/zephyr" -- \
    -DZEPHYR_MODULES="$modules_text" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON)
# Cross-build only; never program, reset or access hardware.
