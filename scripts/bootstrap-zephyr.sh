#!/usr/bin/env bash
set -euo pipefail

repo="$(git rev-parse --show-toplevel)"
workspace="${CRUX_WORKSPACE:-$(dirname "$repo")}"
cache_dir="${CRUX_CACHE_DIR:-${repo}/../.crux-cache}"
python_bin="${CRUX_PYTHON:-python3}"
if [[ $# -gt 1 || ( $# -eq 1 && "${1:-}" != "--install-sdk" ) ]]; then
    printf 'Usage: %s [--install-sdk]\n' "$0" >&2
    exit 2
fi
if [[ "$repo" != "$workspace/crux-pilot" ]]; then
    printf 'Expected repo at %s/crux-pilot; got %s\n' "$workspace" "$repo" >&2
    exit 2
fi
if ! command -v west >/dev/null 2>&1; then
    printf 'west is required. Install the pinned version with: python3 -m pip install west==1.4.0\n' >&2
    exit 2
fi

if [[ ! -f "$workspace/.west/config" ]]; then
    (cd "$workspace" && west init -l "$repo")
fi
(cd "$workspace" && west update)
"$python_bin" -m pip install --disable-pip-version-check --no-cache-dir \
    -r "$workspace/zephyr/scripts/requirements-base.txt" \
    -r "$workspace/zephyr/scripts/requirements-build-test.txt" \
    -r "$workspace/zephyr/scripts/requirements-run-test.txt" \
    west==1.4.0
printf '\nWorkspace: %s\n' "$(cd "$workspace" && west topdir)"
printf 'Zephyr:    %s\n' "$workspace/zephyr"
if [[ "${1:-}" == "--install-sdk" ]]; then
    (cd "$workspace/zephyr" && west sdk install \
        --version 1.0.1 \
        --install-base "$cache_dir/sdk" \
        --gnu-toolchains arm-zephyr-eabi)
elif [[ $# -eq 0 ]]; then
    printf 'SDK prerequisite: Zephyr SDK v1.0.1; arm-zephyr-eabi toolchain.\n'
    printf 'After confirming storage headroom, run: scripts/bootstrap-zephyr.sh --install-sdk\n'
else
    exit 0
fi
