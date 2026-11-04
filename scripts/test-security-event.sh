#!/usr/bin/env bash
# Runs the Security event Host suites under the debug and sanitizer presets, its clang-tidy selection
# and its boundary check. Host only: nothing here operates hardware.
set -euo pipefail

if [[ "$(uname -s)" != Linux ]]; then
    printf 'Security event Host checks are configured for Linux only; reopen this repository in the Dev Container.\n' >&2
    exit 2
fi

repo="$(git rev-parse --show-toplevel)"
export CRUX_CACHE_DIR="${CRUX_CACHE_DIR:-${repo}/../.crux-cache}"
tidy="${CLANG_TIDY:-clang-tidy-18}"
targets=(crux_security_event_core crux_security_event_access_port crux_security_event_fake
    security_event_cpp_headers security_event_conformance security_event_ring security_event_ring_small
    security_event_drain security_event_time_cases security_event_init security_event_concurrency
    security_event_rejections
    security_event_read security_event_waits security_event_secrets security_event_integration
    security_event_public_core security_event_public_fake security_event_fake_cases)

for preset in host-debug host-sanitize; do
    cmake --preset "$preset" -S "$repo" -DFETCHCONTENT_FULLY_DISCONNECTED=ON
    cmake --build --preset "$preset" --target "${targets[@]}"
    ctest --preset "$preset" -R '^SecurityEvent\.' --no-tests=error
done

"$tidy" -p "$repo/build/linux/debug" \
    "$repo"/components/security_event/src/*.c \
    "$repo"/tests/security_event/*.c \
    "$repo"/tests/security_event/fake/*.c

python3 "$repo/scripts/check-security-event-boundary.py"
