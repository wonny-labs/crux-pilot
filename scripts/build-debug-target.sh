#!/usr/bin/env bash
set -euo pipefail

if [[ "$(uname -s)" != Linux ]]; then
    printf 'Zephyr firmware builds are configured for Linux only; reopen this repository in the Dev Container.\n' >&2
    exit 2
fi

if [[ $# -ne 1 || -z "${1:-}" ]]; then
    printf 'Usage: %s <validated-board-target>\n' "$0" >&2
    exit 2
fi

repo="$(git rev-parse --show-toplevel)"
workspace="${CRUX_WORKSPACE:-$(dirname "$repo")}"
board="$1"
zephyr="$workspace/zephyr"
build_key="$(printf '%s' "$board" | sed -E 's/[^A-Za-z0-9_.-]+/_/g')"
build_dir="$repo/build/zephyr/$build_key"

if [[ ! -f "$workspace/.west/config" || ! -f "$zephyr/zephyr-env.sh" ]]; then
    printf 'Run scripts/bootstrap-zephyr.sh before building firmware.\n' >&2
    exit 2
fi

printf 'Incremental board build: %s\n' "$board"
printf 'Application:          %s/samples/time\n' "$repo"
printf 'Build directory:      %s\n' "$build_dir"

# A development build invalidates the previous verification bundle, even on failure.
rm -f "$build_dir/provenance.json"

# Extra CMake arguments force west to reconfigure. Supply them only when creating
# the build system; Ninja tracks source/configuration changes on later launches.
cmake_args=()
if [[ ! -f "$build_dir/build.ninja" ]]; then
    cmake_args=(-- -DZEPHYR_EXTRA_MODULES="$repo" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON)
fi

# Disable pristine builds so repeated F5 launches reuse CMake/Ninja outputs.
(cd "$workspace" && west build --pristine=never -b "$board" -d "$build_dir" \
    "$repo/samples/time" "${cmake_args[@]}")

"$repo/scripts/select-clangd-database.sh" "$build_dir"

elf="$build_dir/zephyr/zephyr.elf"
if [[ ! -f "$elf" ]]; then
    printf 'Expected firmware ELF was not produced: %s\n' "$elf" >&2
    exit 1
fi

printf '\nFirmware ELF ready for the VS Code debug profile:\n%s\n' "$elf"
sha256sum "$elf"
