#!/usr/bin/env bash
# The file service on Host: its suites, with sanitizers, its clang-tidy selection, and a consumer
# built from public headers alone. It uses fakes for File and for access, and it runs on no target.
set -euo pipefail

if [[ "$(uname -s)" != Linux ]]; then
    printf 'File service Host checks are configured for Linux only; reopen this repository in the Dev Container.\n' >&2
    exit 2
fi

repo="$(git rev-parse --show-toplevel)"
export CRUX_CACHE_DIR="${CRUX_CACHE_DIR:-${repo}/../.crux-cache}"
tidy="${CLANG_TIDY:-clang-tidy-18}"
for preset in host-debug host-sanitize; do
    cmake --preset "$preset" -S "$repo" -DFETCHCONTENT_FULLY_DISCONNECTED=ON
    cmake --build --preset "$preset" --target file_service_default file_service_small_table \
        file_service_with_access file_conformance_fake file_fake_faults
    ctest --test-dir "$repo/build/linux/${preset#host-}" --output-on-failure --no-tests=error \
        -R '^(FileService\.|File\.file_conformance_fake|File\.file_fake_faults)'
done

"$tidy" -p "$repo/build/linux/debug" \
    "$repo"/components/file_service/src/*.c \
    "$repo"/tests/file_service/*.c

consumer="$repo/build/file-service-consumer"
cmake -S "$repo/tests/file_service_consumer" -B "$consumer" -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build "$consumer" --target file_service_consumer_c file_service_consumer_cpp
