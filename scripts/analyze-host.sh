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
    "$repo/components/time/src/time_port_memory.c" \
    "$repo/samples/time/src/time_command.c" \
    "$repo/samples/time/host/main.c" \
    "$repo/tests/time/time_test.cpp" \
    "$repo/tests/time/calendar_test.cpp" \
    "$repo/tests/time/command_test.cpp" \
    "$repo/tests/execution/src/posix.c" \
    "$repo/components/thread/src/thread.c" \
    "$repo/components/sync/src/sync.c" \
    "$repo/components/thread/ports/linux/thread_native.c" \
    "$repo/components/sync/ports/linux/sync_native.c" \
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
    "$repo/components/netif/ports/linux/netif_host.c" \
    "$repo/samples/networking/client.c" \
    "$repo/samples/networking/host/main.c" \
    "$repo/tests/networking/runtime_port.c"

"$tidy" -p "$build_dir" \
    "$repo/components/configuration/src/configuration.c" \
    "$repo/tests/configuration/conformance.c" \
    "$repo/tests/configuration/fault_provider.c" \
    "$repo/samples/controller/configuration/scenario.c" \
    "$repo/components/file/src/file.c" \
    "$repo/components/file/ports/linux/file_host.c" \
    "$repo/tests/file/fault_test.c" \
    "$repo/tests/file/host_test.c" \
    "$repo/components/queue/src/queue.c" \
    "$repo/components/queue/ports/linux/queue_port.c" \
    "$repo/components/monotonic/src/monotonic.c" \
    "$repo/components/monotonic/ports/linux/monotonic_host.c"

"$tidy" -p "$build_dir" \
    "$repo/components/serial/src/serial.c" \
    "$repo/components/serial/ports/linux/serial_host.c" \
    "$repo/tests/serial/common.c" \
    "$repo/tests/serial/host.c"

"$tidy" -p "$build_dir" \
    "$repo/components/logging/src/logging.c" \
    "$repo/components/logging/ports/linux/logging_host.c" \
    "$repo/tests/logging/common.c" \
    "$repo/tests/logging/host.c"

"$tidy" -p "$repo/build/logging-consumer" \
    "$repo/tests/logging_consumer/src/runtime.c" \
    "$repo/tests/logging_consumer/src/consumer.cpp" \
    "$repo/tests/logging_consumer/src/contract_test.c" \
    "$repo/tests/logging_consumer/host/main.c" \
    "$repo/samples/logging/controller.c"

"$tidy" -p "$build_dir" \
    "$repo/components/system/src/system.c" \
    "$repo/components/system/ports/linux/system_host.c" \
    "$repo/tests/system/common.c" \
    "$repo/tests/system/host.c"

"$tidy" -p "$repo/build/system-consumer" \
    "$repo/tests/system_consumer/src/runtime.c" \
    "$repo/tests/system_consumer/src/consumer.cpp" \
    "$repo/tests/system_consumer/src/contract_test.c" \
    "$repo/tests/system_consumer/host/main.c" \
    "$repo/samples/controller/system/controller_system.c"
