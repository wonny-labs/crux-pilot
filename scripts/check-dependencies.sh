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

native_include_pattern='#\s*include\s*[<"](zephyr/|errno\.h|unistd\.h|time\.h|sys/|windows\.h)'
portable_paths=(
    "$repo/components/time/src"
    "$repo/components/time/include"
    "$repo/samples/time/src/time_command.c"
    "$repo/samples/controller/core"
    "$repo/samples/controller/include"
)
if rg -n "$native_include_pattern" "${portable_paths[@]}"; then
    printf 'Portable Time and command code must not include native operating-system headers.\n' >&2
    exit 1
fi

controller_native_pattern='#\s*include\s*[<"](zephyr/|board\.h|fsl_[^">]*|cmsis[^">]*|stm32[^">]*)'
if rg -n "$controller_native_pattern" "$repo/samples/controller/core" "$repo/samples/controller/include"; then
    printf 'Portable controller code must not include native board or vendor headers.\n' >&2
    exit 1
fi
if rg -n '^\s*#\s*(if|ifdef|ifndef).*(__ZEPHYR__|CONFIG_|__linux__|_WIN32|BOARD_)' \
    "$repo/samples/controller/core" "$repo/samples/controller/include"; then
    printf 'Environment selection belongs in controller composition, not portable code.\n' >&2
    exit 1
fi

if rg -n 'CRUX_TIME_TESTING|crux_time_(host_)?test_' \
    "$repo/components/base/include" "$repo/components/time/include"; then
    printf 'Test-only Time controls must not appear in exported headers.\n' >&2
    exit 1
fi

rg -n 'INTERFACE_CRUX_TIME_BACKEND host' "$repo/components/time/CMakeLists.txt" >/dev/null
rg -n 'COMPATIBLE_INTERFACE_STRING CRUX_TIME_BACKEND' "$repo/components/time/CMakeLists.txt" >/dev/null
rg -n 'INTERFACE_CRUX_TIME_BACKEND zephyr_rtc' "$repo/zephyr/CMakeLists.txt" >/dev/null
rg -n 'COMPATIBLE_INTERFACE_STRING CRUX_TIME_BACKEND' "$repo/zephyr/CMakeLists.txt" >/dev/null
if rg -U -n 'target_link_libraries\(crux_base[^)]*zephyr_interface' "$repo/zephyr/CMakeLists.txt"; then
    printf 'The base usage target must not acquire Zephyr requirements.\n' >&2
    exit 1
fi

rg -n 'sources\.cmake' "$repo/components/time/CMakeLists.txt" >/dev/null
rg -n 'sources\.cmake' "$repo/tests/time/CMakeLists.txt" >/dev/null
rg -n 'sources\.cmake' "$repo/zephyr/CMakeLists.txt" >/dev/null
if rg -n 'samples/time|time_command\.c|crux_time_commands' "$repo/zephyr/CMakeLists.txt"; then
    printf 'The Zephyr module must not own sample command sources.\n' >&2
    exit 1
fi

printf 'Base is independent; public/common Time and portable commands avoid native headers; backend selection is checked by CMake; shared source ownership is explicit.\n'
python3 "$repo/scripts/check-execution-boundaries.py"
