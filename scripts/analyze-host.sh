#!/usr/bin/env bash
set -euo pipefail

if [[ "$(uname -s)" != Linux ]]; then
    printf 'Host analysis is configured for Linux build artifacts; reopen this repository in the Dev Container.\n' >&2
    exit 2
fi

repo="$(git rev-parse --show-toplevel)"
host_preset="${CRUX_HOST_PRESET:-host-debug}"
export CRUX_CACHE_DIR="${CRUX_CACHE_DIR:-${repo}/../.crux-cache}"
case "$host_preset" in
    host-debug)
        build_dir="$repo/build/linux/debug"
        ;;
    host-sanitize)
        build_dir="$repo/build/linux/sanitize"
        ;;
    *)
        printf 'Unsupported CRUX_HOST_PRESET: %s\n' "$host_preset" >&2
        exit 2
        ;;
esac
if [[ ! -f "$build_dir/compile_commands.json" ]]; then
    printf 'Configure and build %s first (scripts/bootstrap-host.sh).\n' "$host_preset" >&2
    exit 2
fi
tidy="${CLANG_TIDY:-clang-tidy-18}"
"$tidy" -p "$build_dir" \
    "$repo/components/time/src/time.c" \
    "$repo/components/time/src/time_calendar.c" \
    "$repo/components/time/host/time_port_host.c" \
    "$repo/samples/time/src/time_command.c" \
    "$repo/samples/time/host/main.c" \
    "$repo/tests/time/src/time_test.cpp" \
    "$repo/tests/time/src/calendar_test.cpp" \
    "$repo/tests/time/src/command_test.cpp" \
    "$repo/tests/execution/src/posix.c" \
    "$repo/samples/controller/core/controller.c" \
    "$repo/samples/controller/core/health.c" \
    "$repo/samples/controller/host/main.c" \
    "$repo/tests/controller/controller_test.cpp" \
    "$repo/tests/controller/health_test.cpp"

"$tidy" -p "$build_dir" \
    "$repo/components/netif/src/netif.c" \
    "$repo/components/netif/host/netif_host.c" \
    "$repo/samples/networking/client.c" \
    "$repo/samples/networking/host/main.c" \
    "$repo/tests/networking/runtime_port.c"
