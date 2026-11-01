#!/usr/bin/env bash
set -euo pipefail

repo="$(git rev-parse --show-toplevel)"
workspace="${CRUX_WORKSPACE:-$(dirname "$repo")}"
if [[ ! -d "$workspace/zephyr" ]]; then
    printf 'Pinned Zephyr workspace not found at %s/zephyr; set CRUX_WORKSPACE.\n' \
        "$workspace" >&2
    exit 2
fi
if ! command -v west >/dev/null 2>&1; then
    printf 'west is required to cross-build the RT1170-EVK Rev A CM7 target.\n' >&2
    exit 127
fi

# Zephyr's CMake honours these. Clear them so the build uses only the upstream Rev A board
# configuration plus m3.conf, whatever the calling environment sets.
unset DTC_OVERLAY_FILE EXTRA_DTC_OVERLAY_FILE OVERLAY_CONFIG CONF_FILE BOARD_ROOT

export ZEPHYR_BASE="$workspace/zephyr"
configuration="$repo/samples/controller/zephyr/m3.conf"

# Build only. This does not program, reset or debug a physical board.
(cd "$workspace" && west build -p always \
    -b 'mimxrt1170_evk@A/mimxrt1176/cm7' \
    -d "$repo/build/zephyr/rt1170-evk-a1-m3" \
    "$repo/samples/controller/zephyr" -- \
    -DZEPHYR_EXTRA_MODULES="$repo" \
    -DEXTRA_CONF_FILE="$configuration" \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON)
