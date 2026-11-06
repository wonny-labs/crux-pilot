#!/usr/bin/env bash
# Network settings service (Issue #213): the Host suites of components/network_settings and of the
# GET and PUT /api/v1/network routes, under the debug and sanitizer presets, clang-tidy on both, and
# the boundary checks that apply.
#
#   scripts/test-network-settings.sh
#
# What it runs is Host only. It never applies an address to an interface, and it never touches a board:
# the apply port is a fake here. The client's checks against the same vectors run in web with
# `npm run test:unit`, and the specification lint with scripts/check-management-api.sh.
set -euo pipefail

if [[ "$(uname -s)" != Linux ]]; then
    printf 'Network settings checks are configured for Linux only; reopen this repository in the Dev Container.\n' >&2
    exit 2
fi

repo="$(git rev-parse --show-toplevel)"
tidy="${CLANG_TIDY:-clang-tidy-18}"
targets=(network_settings_validation network_settings_boot network_settings_service
    network_settings_power_loss network_settings_foreign_save network_settings_busy
    network_settings_concurrency network_settings_lock_faults network_settings_no_provider
    network_settings_cli network_settings_cpp_headers
    web_network_routes web_network_fuzz web_network_short_reads web_network_no_provider web_network_openapi_fixture
    web_network_openapi_fixture_no_provider web_network_cpp_header)

for preset in host-debug host-sanitize; do
    cmake --preset "$preset" -S "$repo" -DFETCHCONTENT_FULLY_DISCONNECTED=ON
    cmake --build --preset "$preset" --target "${targets[@]}"
    ctest --test-dir "$repo/build/linux/${preset#host-}" --output-on-failure --no-tests=error \
        -R '^(NetworkSettings|Web\.network)'
done
"$tidy" -p "$repo/build/linux/debug" "$repo"/components/network_settings/src/*.c \
    "$repo"/tests/network_settings/*.c "$repo"/components/web/management/web_management_network*.c \
    "$repo"/tests/web/network/*.c
python3 "$repo/scripts/check-native-interfaces.py" --root "$repo"
python3 "$repo/scripts/check-web-routes.py"
printf 'Network settings: Host suites, clang-tidy and the interface policy passed. Not a target or physical result.\n'
