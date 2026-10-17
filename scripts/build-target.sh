#!/usr/bin/env bash
set -euo pipefail

if [[ "$(uname -s)" != Linux ]]; then
    printf 'Zephyr firmware builds are configured for Linux only; reopen this repository in the Dev Container.\n' >&2
    exit 2
fi

if [[ $# -lt 1 || $# -gt 2 || -z "${1:-}" ]]; then
    printf 'Usage: %s <validated-board-target> [test-inputs.json]\n' "$0" >&2
    exit 2
fi

repo="$(git rev-parse --show-toplevel)"
workspace="${CRUX_WORKSPACE:-$(dirname "$repo")}"
zephyr="$workspace/zephyr"
board="$1"
if [[ ! -f "$workspace/.west/config" || ! -f "$zephyr/zephyr-env.sh" ]]; then
    printf 'Run scripts/bootstrap-zephyr.sh before building firmware.\n' >&2
    exit 2
fi

build_key="$(printf '%s' "$board" | sed -E 's/[^A-Za-z0-9_.-]+/_/g')"
build_dir="$repo/build/zephyr/$build_key"
evidence_dir="$repo/build/evidence"
mkdir -p "$evidence_dir"
rm -f "$build_dir/provenance.json"
inputs="$evidence_dir/target-inputs.json"
python="${CRUX_PYTHON:-python3}"
"$python" "$repo/scripts/record-build-provenance.py" --workspace "$workspace" \
    --capture-inputs "$inputs"
test_args=()
if [[ $# -eq 2 ]]; then
    test_args=(--test-inputs "$2")
fi
printf 'Board target: %s\n' "$board"
printf 'Application:  %s\n' "$repo/samples/time"
printf 'Build dir:    %s\n' "$build_dir"

(cd "$workspace" && west build -p always -b "$board" -d "$build_dir" \
    "$repo/samples/time" -- -DZEPHYR_EXTRA_MODULES="$repo" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON) \
    2>&1 | tee "$evidence_dir/evkb-build.log"
"$repo/scripts/select-clangd-database.sh" "$build_dir"
(cd "$workspace" && west build -d "$build_dir" -t rom_report) \
    2>&1 | tee "$evidence_dir/zephyr-rom-report.log"
(cd "$workspace" && west build -d "$build_dir" -t ram_report) \
    2>&1 | tee "$evidence_dir/zephyr-ram-report.log"
"${CRUX_PYTHON:-python3}" "$repo/scripts/record-build-provenance.py" \
    --build-dir "$build_dir" --target "$board" --workspace "$workspace" \
    --inputs "$inputs" "${test_args[@]}"

printf '\nFirmware artifacts:\n'
find "$build_dir/zephyr" -maxdepth 1 -type f \( -name 'zephyr.elf' -o -name 'zephyr.bin' -o -name 'zephyr.hex' \) \
    -exec sha256sum {} \;
printf '\nGenerated evidence files:\n'
find "$build_dir/zephyr" -maxdepth 1 -type f \( -name '.config' -o -name 'zephyr.dts' -o -name 'zephyr.stat' -o -name 'zephyr.map' \) \
    -print
printf 'No flash, reset, or hardware command was run.\n'
