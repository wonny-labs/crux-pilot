#!/usr/bin/env bash
set -euo pipefail
repo="$(git rev-parse --show-toplevel)"
mode="${1:-all}"
if [[ "$mode" != all && "$mode" != --host-only ]]; then
    printf 'Usage: %s [--host-only]\n' "$0" >&2
    exit 2
fi
python3 "$repo/tests/build/test_syslog_server.py"
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
evidence_dir="$repo/build/evidence/logging-evkb"
inputs_before="$evidence_dir/inputs-before.json"
mkdir -p "$evidence_dir"
python3 "$repo/scripts/record-build-provenance.py" --workspace "$workspace" \
    --capture-inputs "$inputs_before"
build_evkb_profile() {
    local profile="$1"
    local config="$2"
    local build_dir="$repo/build/zephyr/logging-evkb-$profile"
    local extra=()
    if [[ -n "$config" ]]; then
        extra+=("-DEXTRA_CONF_FILE=$repo/tests/logging_consumer/zephyr/$config")
    fi
    printf 'Building EVKB profile %s with Zephyr %s and Zephyr SDK %s\n' \
        "$profile" "$(git -C "$workspace/zephyr" rev-parse --short HEAD)" \
        "${ZEPHYR_SDK_INSTALL_DIR:-unset}"
    (cd "$workspace" && west build -b 'mimxrt1060_evk@B//qspi' \
        -p always -d "$build_dir" "$repo/tests/logging_consumer/zephyr" -- \
        -DZEPHYR_MODULES="$modules_text" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON "${extra[@]}") \
        2>&1 | tee "$evidence_dir/$profile-build.log"
    cmake --build "$build_dir" --target rom_report \
        2>&1 | tee "$evidence_dir/$profile-rom-report.log"
    cmake --build "$build_dir" --target ram_report \
        2>&1 | tee "$evidence_dir/$profile-ram-report.log"
    python3 "$repo/scripts/record-build-provenance.py" --workspace "$workspace" \
        --build-dir "$build_dir" --target 'mimxrt1060_evk@B//qspi' --inputs "$inputs_before"
}
build_evkb_profile uart ""
build_evkb_profile udp-only prj.net-only.conf
build_evkb_profile uart-udp prj.net-dual.conf
python3 "$repo/scripts/record-build-provenance.py" --workspace "$workspace" \
    --capture-inputs "$evidence_dir/inputs-after.json"
cmp "$inputs_before" "$evidence_dir/inputs-after.json"
python3 "$repo/scripts/compare-logging-evkb-resources.py" --repo "$repo" \
    --output "$evidence_dir/resource-comparison.tsv"
# Cross-build only; never program, reset or access hardware.
