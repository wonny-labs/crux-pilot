#!/usr/bin/env bash
set -euo pipefail

repo="$(git rev-parse --show-toplevel)"
mkdir -p "$repo/build/spi"

cc -std=c17 -Wall -Wextra -Wpedantic -Werror \
    -I"$repo/components/spi/include" -I"$repo/components/base/include" \
    -I"$repo/components/spi/src" \
    "$repo/components/spi/src/spi.c" \
    "$repo/components/spi/host/spi_port_host.c" \
    "$repo/tests/spi/src/spi_test.c" \
    -o "$repo/build/spi/host-test"
"$repo/build/spi/host-test"

printf '#include <crux/spi.h>\nint main(void) { return 0; }\n' |
    cc -std=c17 -Wall -Wextra -Wpedantic -Werror -fsyntax-only -x c \
        -I"$repo/components/spi/include" -I"$repo/components/base/include" -
printf '#include <crux/spi.h>\nint main() { return 0; }\n' |
    c++ -std=c++17 -Wall -Wextra -Wpedantic -Werror -fsyntax-only -x c++ \
        -I"$repo/components/spi/include" -I"$repo/components/base/include" -

workspace="${CRUX_WORKSPACE:-$(dirname "$repo")}"
export ZEPHYR_BASE="$workspace/zephyr"
(cd "$workspace" && west twister -j 2 -p native_sim \
    -T "$repo/tests/spi_zephyr" \
    -O "$repo/build/zephyr/twister-spi" --inline-logs \
    --extra-args="ZEPHYR_MODULES=$repo")
