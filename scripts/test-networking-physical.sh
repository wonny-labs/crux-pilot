#!/usr/bin/env bash
# Software-only build/test preparation. This script has no device/programming path.
set -euo pipefail
if [[ "$(uname -s)" != Linux ]]; then
    printf 'Use the pinned Linux development environment.\n' >&2
    exit 2
fi
freeze=false
if [[ "${1:-}" == --freeze ]]; then
    freeze=true
    shift
fi
if [[ $# != 0 ]]; then
    printf 'Usage: scripts/test-networking-physical.sh [--freeze]\n' >&2
    exit 2
fi
repo="$(git rev-parse --show-toplevel)"
workspace="${CRUX_WORKSPACE:-$(dirname "$repo")}"
export ZEPHYR_BASE="$workspace/zephyr"
: "${CRUX_CACHE_DIR:?Set CRUX_CACHE_DIR}"
pico="${CRUX_PICOLIBC_MODULE:-$CRUX_CACHE_DIR/networking/picolibc}"
[[ -f "$pico/zephyr/module.yml" ]]
[[ "$(git -C "$pico" rev-parse HEAD)" == 01254932e8e81085817ed61fd858648584ffe37c ]]
[[ -z "$(git -C "$pico" status --porcelain)" ]]
evidence="$repo/build/evidence/m5-preparation"
mkdir -p "$evidence"
target_build="$repo/build/zephyr/m5-physical-evkb"
if "$freeze"; then
    [[ -z "$(git -C "$repo" status --porcelain --untracked-files=all)" ]]
    # Separate fresh output prevents mixing any previously frozen image evidence.
    source_tag="$(git -C "$repo" rev-parse --short=12 HEAD)"
    frozen="$evidence/frozen-$source_tag"
    target_build="$repo/build/zephyr/m5-physical-evkb-$source_tag"
    mkdir "$frozen"
    python3 "$repo/scripts/record-build-provenance.py" --workspace "$workspace" \
        --capture-inputs "$frozen/inputs.json"
fi
python3 -m unittest discover -s "$repo/tests/build" -p 'test_m5_*.py'
modules="$repo;$pico"
(cd "$workspace" && west build -b native_sim/native/64 \
    -d "$repo/build/zephyr/m5-physical-native" "$repo/tests/networking_physical_native" -- \
    -DZEPHYR_MODULES="$modules" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON)
grep -q '^CONFIG_PICOLIBC=y$' "$repo/build/zephyr/m5-physical-native/zephyr/.config"
! grep -q '^CONFIG_NATIVE_LIBC=y$' "$repo/build/zephyr/m5-physical-native/zephyr/.config"
timeout 60 "$repo/build/zephyr/m5-physical-native/zephyr/zephyr.exe" \
    > "$evidence/native-scenario.log" 2>&1
cat "$evidence/native-scenario.log"
grep -q 'PROJECT EXECUTION SUCCESSFUL' "$evidence/native-scenario.log"
modules="$repo;$workspace/modules/hal/cmsis;$workspace/modules/hal/cmsis_6;$workspace/modules/hal/nxp"
# --cmake forces regeneration of the source identity banner even without source edits.
(cd "$workspace" && west build --cmake -b 'mimxrt1060_evk@B//qspi' \
    -d "$target_build" "$repo/tests/networking_physical" -- \
    -DZEPHYR_MODULES="$modules" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DDTC_OVERLAY_FILE="$repo/samples/controller/zephyr/m3-network.overlay")
if "$freeze"; then
    cmake --build "$target_build" --target rom_report ram_report
    python3 "$repo/scripts/record-build-provenance.py" --workspace "$workspace" \
        --build-dir "$target_build" --target 'mimxrt1060_evk@B//qspi' \
        --inputs "$frozen/inputs.json"
    python3 "$repo/tests/target/prepare_m5_network.py" prepare --repo "$repo" \
        --build "$target_build" --inputs "$frozen/inputs.json" \
        --output "$frozen/image-manifest.json"
fi
