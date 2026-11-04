#!/usr/bin/env bash
# The status and configuration services on Host: their suites and the Configuration ones, with and
# without sanitizers, the thread suites under ThreadSanitizer, their clang-tidy selection, consumers
# built from public headers, and the boundary check. They use fakes for access ports, status sources
# and the store, and none of this operates hardware.
set -euo pipefail

if [[ "$(uname -s)" != Linux ]]; then
    printf 'Status and configuration Host checks are configured for Linux only; reopen this repository in the Dev Container.\n' >&2
    exit 2
fi

repo="$(git rev-parse --show-toplevel)"
export CRUX_CACHE_DIR="${CRUX_CACHE_DIR:-${repo}/../.crux-cache}"
tidy="${CLANG_TIDY:-clang-tidy-18}"
targets=(crux_configuration_text crux_configuration_tests crux_configuration_no_provider_test
    crux_configuration_controller
    crux_config_service_core crux_mgmt_status_core crux_mgmt_status_sources crux_mgmt_status_no_security
    config_service_permissions config_service_values config_service_save config_service_concurrency
    config_service_lock_faults config_service_no_provider config_service_cpp_headers
    mgmt_status_items mgmt_status_snapshot mgmt_status_json mgmt_status_json_fixture
    mgmt_status_lock_faults mgmt_status_source_mapping mgmt_status_real_sources
    mgmt_status_cpp_headers)
tests='^(Configuration\.|ConfigService\.|MgmtStatus\.)'

for preset in host-debug host-sanitize; do
    cmake --preset "$preset" -S "$repo" -DFETCHCONTENT_FULLY_DISCONNECTED=ON
    cmake --build --preset "$preset" --target "${targets[@]}"
    ctest --test-dir "$repo/build/linux/${preset#host-}" --output-on-failure --no-tests=error -R "$tests"
done

# A link can take a whole archive, as Zephyr's does for each library registered with it, so the
# character rule's archive must not need a Configuration provider. Nothing in it may refer to the port.
if nm --undefined-only "$repo/build/linux/debug/components/configuration/libcrux_configuration_text.a" |
    grep crux_config_record; then
    printf 'The character rule must not need a Configuration provider.\n' >&2
    exit 1
fi

# A build that changes the longest string, which every user of the header must then agree on.
limit="$repo/build/mgmt-services-string-limit"
cmake -S "$repo" -B "$limit" -G Ninja -DCMAKE_BUILD_TYPE=Debug \
    -DFETCHCONTENT_FULLY_DISCONNECTED=ON -DFETCHCONTENT_BASE_DIR="$CRUX_CACHE_DIR/cmake" \
    -DCRUX_CONFIG_STRING_MAX=64
cmake --build "$limit" --target crux_configuration_tests config_service_values
ctest --test-dir "$limit" --output-on-failure --no-tests=error \
    -R '^(Configuration\.Conformance|ConfigService\.values)$'

# ASan and UBSan do not detect data races. The snapshot's word and the polled lock depend on them.
tsan="$repo/build/mgmt-services-tsan"
cmake -S "$repo" -B "$tsan" -G Ninja -DCMAKE_BUILD_TYPE=Debug \
    -DFETCHCONTENT_FULLY_DISCONNECTED=ON -DFETCHCONTENT_BASE_DIR="$CRUX_CACHE_DIR/cmake" \
    -DCMAKE_C_FLAGS=-fsanitize=thread -DCMAKE_CXX_FLAGS=-fsanitize=thread \
    -DCMAKE_EXE_LINKER_FLAGS=-fsanitize=thread
cmake --build "$tsan" --target config_service_concurrency mgmt_status_snapshot
# ThreadSanitizer cannot map its shadow memory at every address layout, so address randomisation is
# turned off for its run where the system allows that.
no_randomisation=()
if setarch "$(uname -m)" -R true 2>/dev/null; then
    no_randomisation=(setarch "$(uname -m)" -R)
fi
"${no_randomisation[@]}" ctest --test-dir "$tsan" --output-on-failure --no-tests=error \
    -R '^(ConfigService\.concurrency|MgmtStatus\.snapshot)$'

"$tidy" -p "$repo/build/linux/debug" \
    "$repo"/components/configuration/src/configuration*.c \
    "$repo"/components/config_service/src/*.c \
    "$repo"/components/mgmt_status/src/*.c \
    "$repo"/tests/configuration/string_conformance.c \
    "$repo"/tests/configuration/no_provider.c \
    "$repo"/tests/config_service/*.c \
    "$repo"/tests/mgmt_status/*.c

consumer="$repo/build/mgmt-services-consumer"
cmake -S "$repo/tests/mgmt_services_consumer" -B "$consumer" -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build "$consumer" --target mgmt_services_consumer_c mgmt_services_consumer_cpp

python3 "$repo/scripts/check-mgmt-services-boundary.py"
