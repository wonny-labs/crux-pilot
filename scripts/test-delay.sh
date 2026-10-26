#!/usr/bin/env bash
set -euo pipefail
if [[ "$(uname -s)" != Linux ]]; then
    printf 'Delay software checks require the Linux development environment.\n' >&2
    exit 2
fi
repo="$(git rev-parse --show-toplevel)"
workspace="${CRUX_WORKSPACE:-$(dirname "$repo")}"
export ZEPHYR_BASE="$workspace/zephyr"
(cd "$workspace" && west twister -j 2 -p native_sim/native/64 \
    -T "$repo/tests/delay/zephyr" -O "$repo/build/zephyr/twister-delay" --inline-logs)
(cd "$workspace" && west build -b 'mimxrt1060_evk@B//qspi' \
    -d "$repo/build/zephyr/delay-evkb" "$repo/tests/delay/zephyr" -- \
    -DEXTRA_ZEPHYR_MODULES="$repo" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON)
# Compilation only: no programming, reset, debugger, serial or target operation.
