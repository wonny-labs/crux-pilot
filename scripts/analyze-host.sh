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
    "$repo/components/execution/src/execution_threads.c" \
    "$repo/components/execution/src/execution_sync.c" \
    "$repo/components/execution/ports/linux/execution_native.c" \
    "$repo/components/execution/ports/linux/execution_sync_native.c" \
    "$repo/tests/portable_execution/conformance.c" \
    "$repo/tests/portable_execution/host_main.c" \
    "$repo/tests/portable_execution/host_failures.c" \
    "$repo/tests/portable_execution/host_poll.c" \
    "$repo/tests/portable_execution/host_admission.c" \
    "$repo/tests/portable_execution/host_poll_finalize.c" \
    "$repo/tests/portable_execution/admission.c" \
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

"$tidy" -p "$build_dir" \
    "$repo/components/configuration/src/configuration.c" \
    "$repo/tests/configuration/conformance.c" \
    "$repo/tests/configuration/fault_provider.c" \
    "$repo/samples/controller/configuration/scenario.c" \
    "$repo/components/file/src/file.c" \
    "$repo/components/file/host/file_host.c" \
    "$repo/tests/file/fault_test.c" \
    "$repo/tests/file/host_test.c" \
    "$repo/components/queue/src/queue.c" \
    "$repo/components/queue/host/queue_port.c" \
    "$repo/components/monotonic/src/monotonic.c" \
    "$repo/components/monotonic/host/monotonic_host.c"
