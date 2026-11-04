#!/usr/bin/env bash
# Runs the credential store's Host suites under the debug and sanitizer presets, its clang-tidy
# selection and its boundary check. Host only: PSA is not qualified (Issue #142), so every suite here
# uses test-only protection, and nothing here operates hardware.
set -euo pipefail

if [[ "$(uname -s)" != Linux ]]; then
    printf 'Credential Host checks are configured for Linux only; reopen this repository in the Dev Container.\n' >&2
    exit 2
fi

repo="$(git rev-parse --show-toplevel)"
export CRUX_CACHE_DIR="${CRUX_CACHE_DIR:-${repo}/../.crux-cache}"
tidy="${CLANG_TIDY:-clang-tidy-18}"
targets=(crux_credential_core crux_credential_access_store credential_conformance_fake
    credential_conformance_linux credential_concurrency credential_faults
    credential_faults_default_slot credential_security credential_production_refuses
    credential_access_store credential_file_service_roots credential_cpp_headers file_fake_faults)

for preset in host-debug host-sanitize; do
    cmake --preset "$preset" -S "$repo" -DFETCHCONTENT_FULLY_DISCONNECTED=ON
    cmake --build --preset "$preset" --target "${targets[@]}"
    ctest --preset "$preset" -R '^(Credential\.|File\.file_fake_faults)'
done

"$tidy" -p "$repo/build/linux/debug" \
    "$repo"/components/credential/src/*.c \
    "$repo"/components/credential/access_store/*.c \
    "$repo"/tests/credential/*.c

python3 "$repo/scripts/check-credential-boundary.py"
