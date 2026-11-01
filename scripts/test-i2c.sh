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
    -T "$repo/tests/i2c_zephyr" \
    -O "$repo/build/zephyr/twister-i2c" --inline-logs \
    --extra-args="ZEPHYR_MODULES=$repo")
