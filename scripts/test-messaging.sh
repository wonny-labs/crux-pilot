#!/usr/bin/env bash
set -euo pipefail
repo="$(git rev-parse --show-toplevel)"
mode="${1:-all}"
if [[ "$mode" != all && "$mode" != --host-only ]]; then
    printf 'Usage: %s [--host-only]\n' "$0" >&2
    exit 2
fi
cmake -S "$repo/tests/messaging_consumer" -B "$repo/build/messaging-consumer" -G Ninja
cmake --build "$repo/build/messaging-consumer" --parallel 2
ctest --test-dir "$repo/build/messaging-consumer" --output-on-failure \
    --output-junit "$repo/build/messaging-consumer/ctest-junit.xml"
if [[ "$mode" == --host-only ]]; then exit 0; fi
workspace="${CRUX_WORKSPACE:-$(dirname "$repo")}"
export ZEPHYR_BASE="$workspace/zephyr"
(cd "$workspace" && west twister -j 2 -p native_sim/native/64 \
    -T "$repo/tests/queue/zephyr" -T "$repo/tests/thread/zephyr_fractional" \
    -T "$repo/tests/messaging_consumer/zephyr" \
    -O "$repo/build/zephyr/twister-messaging" --inline-logs \
    --extra-args="ZEPHYR_MODULES=$repo")
mapfile -t modules < <(cd "$workspace" && west list cmsis cmsis_6 hal_nxp -f '{abspath}')
modules+=("$repo")
modules_text="$(IFS=';'; printf '%s' "${modules[*]}")"
(cd "$workspace" && west build -b 'mimxrt1060_evk@B//qspi' \
    -d "$repo/build/zephyr/messaging-evkb" "$repo/tests/messaging_consumer/zephyr" -- \
    -DZEPHYR_MODULES="$modules_text" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON)
# Cross-build only. No hardware operation is performed.
