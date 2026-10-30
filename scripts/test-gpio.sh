#!/usr/bin/env bash
set -euo pipefail
repo="$(git rev-parse --show-toplevel)"
workspace="${CRUX_WORKSPACE:-$(dirname "$repo")}"
export ZEPHYR_BASE="$workspace/zephyr"
CRUX_WORKSPACE="$workspace" CRUX_GPIO_COMPOSITION_TESTS=1 \
    python3 "$repo/tests/build/test_gpio_composition.py"
(cd "$workspace" && west twister -j 2 -p native_sim/native/64 \
    -T "$repo/tests/gpio/zephyr" -O "$repo/build/zephyr/twister-gpio" --inline-logs \
    --extra-args="ZEPHYR_MODULES=$repo")
mapfile -t modules < <(cd "$workspace" && west list cmsis cmsis_6 hal_nxp -f '{abspath}')
modules+=("$repo")
modules_text="$(IFS=';'; printf '%s' "${modules[*]}")"
(cd "$workspace" && west build -b 'mimxrt1060_evk@B//qspi' \
    -d "$repo/build/zephyr/gpio-evkb" "$repo/tests/gpio/evkb" -- \
    -DZEPHYR_MODULES="$modules_text" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON)
# EVKB is cross-build evidence only; this script never operates hardware.
