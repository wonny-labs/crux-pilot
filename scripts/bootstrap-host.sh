#!/usr/bin/env bash
set -euo pipefail

if [[ "$(uname -s)" != Linux ]]; then
    printf 'Host CMake builds are configured for Linux only; reopen this repository in the Dev Container.\n' >&2
    exit 2
fi

repo="$(git rev-parse --show-toplevel)"
host_preset="${CRUX_HOST_PRESET:-host-debug}"
cache_dir="${CRUX_CACHE_DIR:-${repo}/../.crux-cache}"
export CRUX_CACHE_DIR="$cache_dir"
mkdir -p "$cache_dir/cmake"

cmake --preset "$host_preset" -S "$repo"
cmake --build --preset "$host_preset"
ctest --preset "$host_preset" \
    --output-junit "$repo/build/linux/${host_preset#host-}/ctest-junit.xml"
