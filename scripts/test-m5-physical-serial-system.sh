#!/usr/bin/env bash
# Software-only checks for the M5 physical Serial (#27) / watchdog-reset (#30) fixture.
# No device, probe, serial port or programming path: host oracle/phase tests and an EVKB cross-build.
set -euo pipefail

if [[ "$(uname -s)" != Linux ]]; then
    printf 'Use the pinned Linux development environment.\n' >&2
    exit 2
fi
repo="$(git rev-parse --show-toplevel)"
workspace="${CRUX_WORKSPACE:-$(dirname "$repo")}"
export ZEPHYR_BASE="${ZEPHYR_BASE:-$workspace/zephyr}"
board="${CRUX_BOARD_TARGET:-mimxrt1060_evk@B//qspi}"
app="$repo/tests/m5_physical_serial_system/zephyr"
build="$repo/build/zephyr/m5-physical-serial-system-evkb"

python3 -m unittest discover -s "$repo/tests/build" -p 'test_m5_watchdog_oracle.py'
python3 -m unittest discover -s "$repo/tests/build" -p 'test_m5_serial_loopback_oracle.py'
python3 -m unittest discover -s "$repo/tests/build" -p 'test_m5_phase_logic.py'

mapfile -t modules < <(cd "$workspace" && west list cmsis cmsis_6 hal_nxp -f '{abspath}')
modules+=("$repo")
modules_text="$(IFS=';'; printf '%s' "${modules[*]}")"
(cd "$workspace" && west build -p always -b "$board" -d "$build" "$app" -- \
    -DZEPHYR_MODULES="$modules_text" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON)

config="$build/zephyr/.config"
for option in CONFIG_CRUX_SERIAL CONFIG_CRUX_SYSTEM CONFIG_CRUX_LOGGING CONFIG_WATCHDOG \
    CONFIG_UART_INTERRUPT_DRIVEN CONFIG_CACHE_MANAGEMENT; do
    grep -Fx "$option=y" "$config" >/dev/null
done
grep -q 'nxp,loopback' "$build/zephyr/zephyr.dts"
for option in CONFIG_SHELL CONFIG_STDOUT_CONSOLE CONFIG_UART_CONSOLE; do
    ! grep -Fx "$option=y" "$config" >/dev/null
done
sha256sum "$build/zephyr/zephyr.bin" "$build/zephyr/zephyr.elf"
