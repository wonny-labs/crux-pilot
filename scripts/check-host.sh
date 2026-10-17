#!/usr/bin/env bash
set -euo pipefail

if [[ "$(uname -s)" != Linux ]]; then
    printf 'Host CMake checks are configured for Linux only; reopen this repository in the Dev Container.\n' >&2
    exit 2
fi

repo="$(git rev-parse --show-toplevel)"
host_preset="${CRUX_HOST_PRESET:-host-debug}"
export CRUX_CACHE_DIR="${CRUX_CACHE_DIR:-${repo}/../.crux-cache}"
cmake --preset "$host_preset" -S "$repo" -DFETCHCONTENT_FULLY_DISCONNECTED=ON
cmake --build --preset "$host_preset"
ctest --preset "$host_preset" \
    --output-junit "$repo/build/linux/${host_preset#host-}/ctest-junit.xml"
"$repo/scripts/check-host-sample.sh" "$host_preset"
