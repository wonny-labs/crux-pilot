#!/usr/bin/env bash
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
: "${ZEPHYR_BASE:?Set ZEPHYR_BASE to the pinned west workspace Zephyr directory}"
workspace="$(cd "$ZEPHYR_BASE/.." && pwd)"
output="${CRUX_FLASH_POC_BUILD:-$repo/build/flash-safety-poc/zephyr}"
python3 "$repo/tests/native/flash_safety/test_driver.py"
(cd "$workspace" && west build -b 'mimxrt1060_evk@B//qspi' -d "$output" \
    "$repo/samples/flash_safety_poc" -- \
    "-DZEPHYR_MODULES=$repo;$workspace/modules/hal/nxp;$workspace/modules/hal/cmsis;$workspace/modules/hal/cmsis_6")
python3 "$repo/scripts/verify-flash-poc.py" "$output/zephyr/zephyr.elf"
python3 "$repo/tests/native/flash_safety/test_elf.py" "$output/zephyr/zephyr.elf"
printf 'Compile-only PoC; this script never programs or operates hardware.\n'
