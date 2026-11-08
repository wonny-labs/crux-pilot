#!/usr/bin/env bash
set -euo pipefail

repo="$(git rev-parse --show-toplevel)"
mkdir -p "$repo/build/i2c"

cc -std=c17 -Wall -Wextra -Wpedantic -Werror \
    -I"$repo/components/i2c/include" -I"$repo/components/base/include" \
    -I"$repo/components/i2c/src" \
    "$repo/components/i2c/src/i2c.c" \
    "$repo/components/i2c/host/i2c_port_host.c" \
    "$repo/tests/i2c/src/i2c_test.c" \
    -o "$repo/build/i2c/host-test"

cc -std=c17 -Wall -Wextra -Wpedantic -Werror \
    -I"$repo/components/i2c/include" -I"$repo/components/base/include" \
    -I"$repo/components/i2c/src" \
    "$repo/components/i2c/src/i2c.c" \
    "$repo/tests/i2c/src/i2c_fault_test.c" \
    -o "$repo/build/i2c/fault-test"
"$repo/build/i2c/fault-test"

cc -std=c17 -Wall -Wextra -Wpedantic -Werror \
    -I"$repo/components/i2c/include" -I"$repo/components/base/include" \
    -I"$repo/components/i2c/src" -I"$repo/tests/i2c/consumer" \
    "$repo/components/i2c/src/i2c.c" \
    "$repo/tests/i2c/consumer/tmp117.c" \
    "$repo/tests/i2c/consumer/tmp117_test.c" \
    -o "$repo/build/i2c/tmp117-test"
"$repo/build/i2c/tmp117-test"

cc -std=c17 -Wall -Wextra -Wpedantic -Werror \
    -I"$repo/components/base/include" -I"$repo/components/i2c/ports/zephyr" \
    "$repo/components/i2c/ports/zephyr/i2c_status.c" \
    "$repo/tests/i2c/src/i2c_status_test.c" \
    -o "$repo/build/i2c/status-test"
"$repo/build/i2c/status-test"
"$repo/build/i2c/host-test"

printf '#include <crux/i2c.h>\nint main(void) { return 0; }\n' |
    cc -std=c17 -Wall -Wextra -Wpedantic -Werror -fsyntax-only -x c \
        -I"$repo/components/i2c/include" -I"$repo/components/base/include" -
printf '#include <crux/i2c.h>\nint main() { return 0; }\n' |
    c++ -std=c++17 -Wall -Wextra -Wpedantic -Werror -fsyntax-only -x c++ \
        -I"$repo/components/i2c/include" -I"$repo/components/base/include" -

workspace="${CRUX_WORKSPACE:-$(dirname "$repo")}"
export ZEPHYR_BASE="$workspace/zephyr"
CRUX_WORKSPACE="$workspace" CRUX_I2C_COMPOSITION_TESTS=1 \
    python3 "$repo/tests/build/test_i2c_composition.py"
(cd "$workspace" && west twister -j 2 -p native_sim \
    -T "$repo/tests/i2c_zephyr" -T "$repo/tests/i2c_consumer_zephyr" \
    -O "$repo/build/zephyr/twister-i2c" --inline-logs \
    --extra-args="ZEPHYR_MODULES=$repo")
mapfile -t modules < <(cd "$workspace" && west list cmsis cmsis_6 hal_nxp -f '{abspath}')
modules+=("$repo")
modules_text="$(IFS=';'; printf '%s' "${modules[*]}")"
# The same portable consumer for both boards. Zephyr's CMake honours these variables, so clear
# them: each build must use only the board's own configuration plus the overlay in
# tests/i2c/evkb/boards.
unset DTC_OVERLAY_FILE EXTRA_DTC_OVERLAY_FILE OVERLAY_CONFIG CONF_FILE BOARD_ROOT
(cd "$workspace" && west build -p always -b 'mimxrt1060_evk@B//qspi' \
    -d "$repo/build/zephyr/i2c-evkb" "$repo/tests/i2c/evkb" -- \
    -DZEPHYR_MODULES="$modules_text" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON)
(cd "$workspace" && west build -p always -b 'mimxrt1170_evk@A/mimxrt1176/cm7' \
    -d "$repo/build/zephyr/i2c-rt1170" "$repo/tests/i2c/evkb" -- \
    -DZEPHYR_MODULES="$modules_text" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON)
# The cross-builds are compile and link evidence only; this script never operates hardware.
