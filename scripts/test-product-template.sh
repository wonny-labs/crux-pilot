#!/usr/bin/env bash
set -euo pipefail

if [[ "$(uname -s)" != Linux ]]; then
    printf 'Zephyr firmware builds are configured for Linux only; reopen this repository in the Dev Container.\n' >&2
    exit 2
fi
if [[ $# -ne 1 || -z "${1:-}" ]]; then
    printf 'Usage: %s <validated-board-target>\n' "$0" >&2
    exit 2
fi

repo="$(git rev-parse --show-toplevel)"
workspace="${CRUX_WORKSPACE:-$(dirname "$repo")}"
board="$1"
build="$repo/build/zephyr/product-template"
if [[ ! -f "$workspace/.west/config" || ! -d "$workspace/zephyr" ]]; then
    printf 'Run scripts/bootstrap-zephyr.sh before building the product template.\n' >&2
    exit 2
fi

# These use real west on local repositories. The host CI job has no west and skips them.
python="${CRUX_PYTHON:-python3}"
"$python" -m unittest discover -s "$repo/tests/build" -p test_crux_pins.py -v
"$python" -m unittest discover -s "$repo/tests/build" -p test_product_template.py -v

# This runs the shipped command on a real Zephyr manifest: Crux's own workspace.
(cd "$workspace" && west crux-pins)

# This builds the application inside Crux's own workspace, not through the template's
# manifest. Build only: no debugger, serial transport, programming or reset.
export ZEPHYR_BASE="$workspace/zephyr"
(cd "$workspace" && west build -p always -b "$board" -d "$build" \
    "$repo/templates/product/app" -- -DZEPHYR_EXTRA_MODULES="$repo")
for output in zephyr.bin zephyr.meta; do
    if [[ ! -f "$build/zephyr/$output" ]]; then
        printf 'The product template build did not produce %s.\n' "$output" >&2
        exit 1
    fi
done
printf 'Product template built for %s. No flash, reset, or hardware command was run.\n' "$board"
