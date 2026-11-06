#!/usr/bin/env bash
# Reboot service (Issue #214): the Host suites of components/reboot and of the GET and POST
# /api/v1/system/reboot routes, under the debug and sanitizer presets, clang-tidy on both, and the
# boundary checks that apply.
#
#   scripts/test-reboot.sh
#
# What it runs is Host only. It never resets anything and it never touches a board: the reset port is a
# fake here, and crux_system_reset() is not called. The specification lint runs with
# scripts/check-management-api.sh.
set -euo pipefail

if [[ "$(uname -s)" != Linux ]]; then
    printf 'Reboot checks are configured for Linux only; reopen this repository in the Dev Container.\n' >&2
    exit 2
fi

repo="$(git rev-parse --show-toplevel)"
tidy="${CLANG_TIDY:-clang-tidy-18}"
targets=(reboot_service reboot_steps reboot_concurrency reboot_reason reboot_lock_faults reboot_pending
    reboot_cpp_headers
    web_reboot_routes web_reboot_no_service web_reboot_openapi_fixture management_providers_events)

for preset in host-debug host-sanitize; do
    cmake --preset "$preset" -S "$repo" -DFETCHCONTENT_FULLY_DISCONNECTED=ON
    cmake --build --preset "$preset" --target "${targets[@]}"
    ctest --test-dir "$repo/build/linux/${preset#host-}" --output-on-failure --no-tests=error \
        -R '^(Reboot|Web\.reboot|ManagementProviders\.events)'
done
"$tidy" -p "$repo/build/linux/debug" "$repo"/components/reboot/src/*.c "$repo"/tests/reboot/*.c \
    "$repo"/components/web/management/web_management_reboot.c "$repo"/tests/web/reboot/*.c
python3 "$repo/scripts/check-native-interfaces.py" --root "$repo"
python3 "$repo/scripts/check-web-routes.py"
printf 'Reboot: Host suites, clang-tidy and the interface policy passed. Not a target or physical result.\n'
