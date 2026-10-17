#!/usr/bin/env bash
set -euo pipefail

if [[ "$(uname -s)" != Darwin ]]; then
    printf 'Run physical UART verification on macOS with the board and J-Link attached.\n' >&2
    exit 2
fi
repo="$(git rev-parse --show-toplevel)"
python="${CRUX_TARGET_PYTHON:-$repo/build/tools/m4-venv/bin/python}"
if [[ ! -x "$python" ]]; then
    printf 'Install tests/target/requirements.txt in a host venv and set CRUX_TARGET_PYTHON.\n' >&2
    exit 2
fi
exec "$python" -m pytest --rootdir "$repo" "$repo/tests/target" "$@"
