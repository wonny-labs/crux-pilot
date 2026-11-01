#!/usr/bin/env bash
set -euo pipefail

if [[ "$(uname -s)" != Linux ]]; then
    printf 'Zephyr tests are configured for Linux only; reopen this repository in the Dev Container.\n' >&2
    exit 2
fi

repo="$(git rev-parse --show-toplevel)"
workspace="${CRUX_WORKSPACE:-$(dirname "$repo")}"
if [[ ! -f "$workspace/.west/config" || ! -d "$workspace/zephyr" ]]; then
    printf 'Run scripts/bootstrap-zephyr.sh before Zephyr tests.\n' >&2
    exit 2
fi
export ZEPHYR_BASE="$workspace/zephyr"
export ZEPHYR_TOOLCHAIN_VARIANT=host
build_dir="${CRUX_PERSISTENCE_BUILD_DIR:-$repo/build/zephyr/file-fatfs-persist}"

# FatFs on a flash disk backed by a host file; the processes below are separate runs of it.
(cd "$workspace" && west build -p always -b native_sim/native/64 -d "$build_dir" \
    "$repo/tests/native/file_fatfs_persist")
python3 "$repo/tests/native/file_fatfs_persist/run_persistence.py" \
    --exe "$build_dir/zephyr/zephyr.exe" "$@"
