#!/usr/bin/env bash
set -euo pipefail

if [[ "$(uname -s)" != Linux ]]; then
    printf 'Zephyr tests are configured for Linux only; reopen this repository in the Dev Container.\n' >&2
    exit 2
fi

repo="$(git rev-parse --show-toplevel)"
workspace="${CRUX_WORKSPACE:-$(dirname "$repo")}"
zephyr="$workspace/zephyr"
board="${1:-}"
if [[ ! -f "$workspace/.west/config" || ! -d "$zephyr" ]]; then
    printf 'Run scripts/bootstrap-zephyr.sh before Zephyr tests.\n' >&2
    exit 2
fi
if [[ -z "$board" ]]; then
    printf 'Usage: %s <validated-board-target>\n' "$0" >&2
    exit 2
fi

test_output="$repo/build/zephyr/twister-time"
native_sample_build="$repo/build/zephyr/native_sim_time"
printf 'Running Crux Time Ztest scenarios on native_sim/native/64.\n'
(cd "$workspace" && west twister -j 2 -p native_sim/native/64 -T "$repo/tests/zephyr_time" \
    -O "$test_output" --inline-logs)

printf '\nBuilding the Zephyr Time Shell sample for native_sim/native/64.\n'
(cd "$workspace" && west build -p always -b native_sim/native/64 -d "$native_sample_build" \
    "$repo/samples/time" -- -DZEPHYR_EXTRA_MODULES="$repo" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON)

printf '\nCross-building the Zephyr Time Shell sample for %s.\n' "$board"
"$repo/scripts/build-target.sh" "$board"
