#!/usr/bin/env bash
set -euo pipefail
if [[ "$(uname -s)" != Linux ]]; then
    printf 'Controller validation requires the Linux development environment.\n' >&2
    exit 2
fi
repo="$(git rev-parse --show-toplevel)"
workspace="${CRUX_WORKSPACE:-$(dirname "$repo")}"
export ZEPHYR_BASE="$workspace/zephyr"
for configuration in normal missing-rtc; do
    build="$repo/build/zephyr/controller-$configuration"
    extra=()
    if [[ "$configuration" == missing-rtc ]]; then
        extra+=("-DDTC_OVERLAY_FILE=$repo/tests/time/zephyr/boards/native_sim_no_rtc.overlay")
    fi
    (cd "$workspace" && west build -p always -b native_sim/native/64 -d "$build" \
        "$repo/samples/controller/zephyr" -- -DZEPHYR_EXTRA_MODULES="$repo" "${extra[@]}")
    # Missing RTC is now a positive system-realtime composition, not init failure.
    python3 "$repo/scripts/check-controller.py" "$build/zephyr/zephyr.exe" \
        --native-sim --log "$build/scenario.log"
done
# Build only: no debugger, serial transport, programming or reset.
(cd "$workspace" && west build -p always -b 'mimxrt1060_evk@B//qspi' \
    -d "$repo/build/zephyr/controller-evkb" "$repo/samples/controller/zephyr" -- \
    -DZEPHYR_EXTRA_MODULES="$repo")
