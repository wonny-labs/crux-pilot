#!/usr/bin/env bash
set -euo pipefail

if [[ "$(uname -s)" != Linux ]]; then
    printf 'Zephyr smoke builds are configured for Linux only; reopen this repository in the Dev Container.\n' >&2
    exit 2
fi

repo="$(git rev-parse --show-toplevel)"
workspace="${CRUX_WORKSPACE:-$(dirname "$repo")}"
zephyr="$workspace/zephyr"
if [[ ! -f "$workspace/.west/config" || ! -d "$zephyr" ]]; then
    printf 'Run scripts/bootstrap-zephyr.sh before the Zephyr smoke.\n' >&2
    exit 2
fi

(cd "$workspace" && west build -p always -b native_sim/native/64 \
    "$zephyr/samples/hello_world" \
    -- -DZEPHYR_EXTRA_MODULES="$repo")
(cd "$workspace" && west twister \
    -p native_sim/native/64 -T "$zephyr/tests/kernel/mem_slab/mslab_concept")
(cd "$workspace" && west build -p always -b native_sim/native/64 \
    "$zephyr/samples/cpp/hello_world" \
    -- -DZEPHYR_EXTRA_MODULES="$repo")

board="${CRUX_BOARD_TARGET:-}"
if [[ -n "$board" ]]; then
    (cd "$workspace" && west build -p always -b "$board" \
        "$zephyr/samples/hello_world" \
        -- -DZEPHYR_EXTRA_MODULES="$repo")
else
    printf 'SKIP: EVK Hello World compile pending CRUX_BOARD_TARGET (exact EVK/config not verified).\n'
fi
