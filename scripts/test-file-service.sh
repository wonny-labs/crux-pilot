#!/usr/bin/env bash
# The file service on Host: its suites, with sanitizers, and a consumer built from public headers
# alone. It uses fakes for File and for access, and it runs on no target.
set -euo pipefail

repo="$(git rev-parse --show-toplevel)"
export CRUX_CACHE_DIR="${CRUX_CACHE_DIR:-${repo}/../.crux-cache}"
for preset in host-debug host-sanitize; do
    cmake --preset "$preset" -S "$repo" -DFETCHCONTENT_FULLY_DISCONNECTED=ON
    cmake --build --preset "$preset" --target file_service_default file_service_small_table \
        file_service_with_access file_conformance_fake file_fake_faults
    ctest --test-dir "$repo/build/linux/${preset#host-}" --output-on-failure \
        -R '^(FileService\.|File\.file_conformance_fake|File\.file_fake_faults)'
done

consumer="$repo/build/file-service-consumer"
cmake -S "$repo/tests/file_service_consumer" -B "$consumer" -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build "$consumer" --target file_service_consumer_c file_service_consumer_cpp
