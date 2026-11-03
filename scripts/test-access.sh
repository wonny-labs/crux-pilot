#!/usr/bin/env bash
# Runs the Access Host suites under the debug and sanitizer presets, its clang-tidy selection and its
# boundary check. Host only: no real provider exists yet, and nothing
# here operates hardware.
set -euo pipefail

if [[ "$(uname -s)" != Linux ]]; then
    printf 'Access Host checks are configured for Linux only; reopen this repository in the Dev Container.\n' >&2
    exit 2
fi

repo="$(git rev-parse --show-toplevel)"
export CRUX_CACHE_DIR="${CRUX_CACHE_DIR:-${repo}/../.crux-cache}"
tidy="${CLANG_TIDY:-clang-tidy-18}"
targets=(crux_access_core access_conformance access_accounts access_security access_keys
    access_faults access_secrets access_events access_cpp_headers)

for preset in host-debug host-sanitize; do
    cmake --preset "$preset" -S "$repo" -DFETCHCONTENT_FULLY_DISCONNECTED=ON
    cmake --build --preset "$preset" --target "${targets[@]}"
    ctest --preset "$preset" -R '^Access\.'
done

"$tidy" -p "$repo/build/linux/debug" \
    "$repo"/components/access/src/*.c \
    "$repo"/tests/access/*.c

python3 "$repo/scripts/check-access-boundary.py"
