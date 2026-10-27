#!/usr/bin/env bash
set -euo pipefail

repo="$(git rev-parse --show-toplevel)"

if rg -n 'find_package\s*\(\s*Zephyr' "$repo/CMakeLists.txt"; then
    printf 'Host root CMake must not load Zephyr.\n' >&2
    exit 1
fi
rg -n '^\s*cmake:\s+zephyr\s*$' "$repo/zephyr/module.yml" >/dev/null

base_paths=("$repo/components/base/CMakeLists.txt" "$repo/components/base/include")
if rg -n 'components/time|crux_time|Zephyr|zephyr_interface|find_package' "${base_paths[@]}"; then
    printf 'The base status component must not depend on Time or Zephyr.\n' >&2
    exit 1
fi

# The shared policy covers headers, native tokens and environment selection in
# all designated portable roots; ISO C headers (including time/errno) are valid.
python3 "$repo/scripts/check-native-interfaces.py" --root "$repo"

if rg -n 'CRUX_TIME_TESTING|crux_time_(host_)?test_' \
    "$repo/components/base/include" "$repo/components/time/include"; then
    printf 'Test-only Time controls must not appear in exported headers.\n' >&2
    exit 1
fi

rg -n 'INTERFACE_CRUX_TIME_BACKEND memory' "$repo/components/time/CMakeLists.txt" >/dev/null
rg -n 'COMPATIBLE_INTERFACE_STRING CRUX_TIME_BACKEND' "$repo/components/time/CMakeLists.txt" >/dev/null
rg -n 'INTERFACE_CRUX_TIME_BACKEND zephyr_system' "$repo/components/time/ports/zephyr/CMakeLists.txt" >/dev/null
rg -n 'COMPATIBLE_INTERFACE_STRING CRUX_TIME_BACKEND' "$repo/components/time/ports/zephyr/CMakeLists.txt" >/dev/null
if rg -U -n 'target_link_libraries\(crux_base[^)]*zephyr_interface' "$repo/zephyr/CMakeLists.txt"; then
    printf 'The base usage target must not acquire Zephyr requirements.\n' >&2
    exit 1
fi

rg -n 'sources\.cmake' "$repo/components/time/CMakeLists.txt" >/dev/null
rg -n 'sources\.cmake' "$repo/tests/time/CMakeLists.txt" >/dev/null
rg -n 'sources\.cmake' "$repo/components/time/ports/zephyr/CMakeLists.txt" >/dev/null
if rg -n 'samples/time|time_command\.c|crux_time_commands' "$repo/zephyr/CMakeLists.txt" "$repo/components/time/ports/zephyr/CMakeLists.txt"; then
    printf 'The Zephyr module must not own sample command sources.\n' >&2
    exit 1
fi

printf 'Base is independent; designated portable sources pass the native interface policy; backend selection is checked by CMake; shared source ownership is explicit.\n'
python3 "$repo/scripts/check-execution-boundaries.py"

python3 "$repo/scripts/check-networking-dependencies.py"
