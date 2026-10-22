#!/usr/bin/env bash
set -euo pipefail
if [[ "$(uname -s)" != Linux ]]; then
    printf 'Networking software qualification requires the Linux development environment.\n' >&2
    exit 2
fi
repo="$(git rev-parse --show-toplevel)"
workspace="${CRUX_WORKSPACE:-$(dirname "$repo")}"
export ZEPHYR_BASE="$workspace/zephyr"
: "${CRUX_CACHE_DIR:?Set CRUX_CACHE_DIR}"
pico="${CRUX_PICOLIBC_MODULE:-$CRUX_CACHE_DIR/networking/picolibc}"
if [[ ! -f "$pico/zephyr/module.yml" ]]; then
    printf 'Run scripts/bootstrap-networking-tests.sh first.\n' >&2; exit 1
fi
[[ "$(git -C "$pico" rev-parse HEAD)" == 01254932e8e81085817ed61fd858648584ffe37c ]]
[[ -z "$(git -C "$pico" status --porcelain)" ]]
python3 "$repo/scripts/check-networking-dependencies.py"
cmake -S "$repo/tests/networking_consumer" -B "$repo/build/networking-consumer" \
    -G Ninja -DCRUX_SOURCE="$repo"
cmake --build "$repo/build/networking-consumer" -j 2
"$repo/build/networking-consumer/consumer_c"
"$repo/build/networking-consumer/consumer_cpp"
# Explicit modules avoid duplicate manifest-self discovery for linked worktrees.
modules="$repo;$pico"
(cd "$workspace" && west build -b native_sim/native/64 \
    -d "$repo/build/zephyr/networking-native-pico" "$repo/tests/networking_zephyr" -- \
    -DZEPHYR_MODULES="$modules" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON)
python3 "$repo/scripts/compile-networking-cpp-probe.py" \
    "$repo/build/zephyr/networking-native-pico/compile_commands.json" \
    "$repo/tests/networking_consumer/cpp_probe.cpp" \
    "$repo/build/zephyr/networking-native-pico/cpp-probe.o"
# Refuse a false qualification that silently resolves standard calls to Host libc.
grep -q '^CONFIG_PICOLIBC=y$' "$repo/build/zephyr/networking-native-pico/zephyr/.config"
grep -q '^# CONFIG_NATIVE_LIBC is not set$' "$repo/build/zephyr/networking-native-pico/zephyr/.config" ||
    ! grep -q '^CONFIG_NATIVE_LIBC=y$' "$repo/build/zephyr/networking-native-pico/zephyr/.config"
timeout 60 "$repo/build/zephyr/networking-native-pico/zephyr/zephyr.exe" \
    > "$repo/build/zephyr/networking-native-pico/scenario.log" 2>&1
cat "$repo/build/zephyr/networking-native-pico/scenario.log"
grep -q 'PROJECT EXECUTION SUCCESSFUL' "$repo/build/zephyr/networking-native-pico/scenario.log"
# Build-only target qualification; never invokes flash/debug/serial commands.
modules="$repo;$workspace/modules/hal/cmsis;$workspace/modules/hal/cmsis_6;$workspace/modules/hal/nxp"
(cd "$workspace" && west build -b 'mimxrt1060_evk@B//qspi' \
    -d "$repo/build/zephyr/networking-evkb" "$repo/samples/networking/zephyr" -- \
    -DZEPHYR_MODULES="$modules" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DDTC_OVERLAY_FILE="$repo/samples/controller/zephyr/m3-network.overlay")
