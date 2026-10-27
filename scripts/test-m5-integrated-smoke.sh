#!/usr/bin/env bash
set -euo pipefail

repo="$(git rev-parse --show-toplevel)"
workspace="${CRUX_WORKSPACE:-$(dirname "$repo")}"
export ZEPHYR_BASE="${ZEPHYR_BASE:-$workspace/zephyr}"
board="${CRUX_BOARD_TARGET:-mimxrt1060_evk@B//qspi}"
app="$repo/tests/m5_integrated/zephyr"
twister_output="$repo/build/zephyr/twister-m5-integrated"
build="$repo/build/zephyr/m5-integrated-evkb"
evidence="$repo/build/evidence/m5-integrated-smoke"
inputs="$evidence/inputs-before.json"

mkdir -p "$evidence"
python3 "$repo/scripts/record-build-provenance.py" --capture-inputs "$inputs"

(cd "$workspace" && west twister -j 2 -p native_sim/native/64 -T "$app" \
    -O "$twister_output" --inline-logs --extra-args="ZEPHYR_MODULES=$repo")
native_config="$(find "$twister_output" -path '*/zephyr/.config' -print -quit)"
grep -Fx 'CONFIG_EARLY_CONSOLE=y' "$native_config" >/dev/null
grep -Fx '# CONFIG_LOG_PRINTK is not set' "$native_config" >/dev/null

mapfile -t modules < <(cd "$workspace" && west list cmsis cmsis_6 hal_nxp -f '{abspath}')
modules+=("$repo")
modules_text="$(IFS=';'; printf '%s' "${modules[*]}")"
(cd "$workspace" && west build -b "$board" -d "$build" "$app" -- \
    -DZEPHYR_MODULES="$modules_text" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON)
west build -d "$build" -t rom_report
west build -d "$build" -t ram_report

config="$build/zephyr/.config"
for option in CONFIG_CRUX_THREAD CONFIG_CRUX_SYNC CONFIG_CRUX_LOGGING CONFIG_CRUX_SYSTEM \
    CONFIG_LOG_BACKEND_UART CONFIG_UART_CONSOLE CONFIG_WATCHDOG CONFIG_HWINFO CONFIG_REBOOT \
    CONFIG_LOG_PRINTK; do
    grep -Fx "$option=y" "$config" >/dev/null
done
if grep -Fx 'CONFIG_CRUX_SERIAL=y' "$config" >/dev/null; then
    echo "Integrated smoke must not enable Crux Serial" >&2
    exit 1
fi
for option in CONFIG_STDOUT_CONSOLE CONFIG_CONSOLE_HANDLER \
    CONFIG_CONSOLE_SUBSYS CONFIG_EARLY_CONSOLE CONFIG_SHELL; do
    grep -Fx "# $option is not set" "$config" >/dev/null
done

python3 "$repo/scripts/record-build-provenance.py" --build-dir "$build" --target "$board" \
    --inputs "$inputs"
python3 "$repo/scripts/record-build-provenance.py" --capture-inputs "$evidence/inputs-after.json"
cmp "$inputs" "$evidence/inputs-after.json"
