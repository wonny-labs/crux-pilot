#!/usr/bin/env bash
set -euo pipefail
repo="$(git rev-parse --show-toplevel)"
workspace="${CRUX_WORKSPACE:-$(dirname "$repo")}"
export ZEPHYR_BASE="$workspace/zephyr"
CRUX_WORKSPACE="$workspace" CRUX_GPIO_COMPOSITION_TESTS=1 \
    python3 "$repo/tests/build/test_gpio_composition.py"
(cd "$workspace" && west twister -j 2 -p native_sim/native/64 \
    -T "$repo/tests/gpio/zephyr" -T "$repo/tests/gpio/zephyr_failure" \
    -O "$repo/build/zephyr/twister-gpio" --inline-logs \
    --extra-args="ZEPHYR_MODULES=$repo")
mapfile -t modules < <(cd "$workspace" && west list cmsis cmsis_6 hal_nxp -f '{abspath}')
modules+=("$repo")
modules_text="$(IFS=';'; printf '%s' "${modules[*]}")"
(cd "$workspace" && west build -b 'mimxrt1060_evk@B//qspi' \
    -d "$repo/build/zephyr/gpio-evkb" "$repo/tests/gpio/evkb" -- \
    -DZEPHYR_MODULES="$modules_text" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON)
# The same consumer source for a second RT1060-EVKB composition: the alias is replaced by the
# overlay in tests/gpio/evkb/compositions, which Zephyr applies after the board overlay.
(cd "$workspace" && west build -p always -b 'mimxrt1060_evk@B//qspi' \
    -d "$repo/build/zephyr/gpio-evkb-arduino-d2" "$repo/tests/gpio/evkb" -- \
    -DZEPHYR_MODULES="$modules_text" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DEXTRA_DTC_OVERLAY_FILE="$repo/tests/gpio/evkb/compositions/arduino_d2.overlay")
# The same consumer for RT1170-EVK board revision A (Cortex-M7). Zephyr's CMake honours these
# variables, so clear them: the build must use only the board's own configuration plus the
# alias overlay in tests/gpio/evkb/boards.
unset DTC_OVERLAY_FILE EXTRA_DTC_OVERLAY_FILE OVERLAY_CONFIG CONF_FILE BOARD_ROOT
(cd "$workspace" && west build -p always -b 'mimxrt1170_evk@A/mimxrt1176/cm7' \
    -d "$repo/build/zephyr/gpio-rt1170" "$repo/tests/gpio/evkb" -- \
    -DZEPHYR_MODULES="$modules_text" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON)
# The cross-builds are compile and link evidence only; this script never operates hardware.
