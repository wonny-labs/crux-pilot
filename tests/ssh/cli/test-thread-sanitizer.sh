#!/usr/bin/env bash
# Ssh.cli_concurrency under ThreadSanitizer, because ASan and UBSan do not detect data races and a slow
# client must share nothing with another session. The Host CI job runs this. It does not run in a container
# under emulation, where ThreadSanitizer cannot map its shadow memory.
set -euo pipefail

if [[ "$(uname -s)" != Linux ]]; then
    printf 'These checks are configured for Linux only; reopen this repository in the Dev Container.\n' >&2
    exit 2
fi

repo="$(git rev-parse --show-toplevel)"
export CRUX_CACHE_DIR="${CRUX_CACHE_DIR:-${repo}/../.crux-cache}"
build="$repo/build/ssh-cli-tsan"
cmake -S "$repo" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Debug \
    -DFETCHCONTENT_FULLY_DISCONNECTED=ON -DFETCHCONTENT_BASE_DIR="$CRUX_CACHE_DIR/cmake" \
    -DCMAKE_C_FLAGS=-fsanitize=thread -DCMAKE_CXX_FLAGS=-fsanitize=thread \
    -DCMAKE_EXE_LINKER_FLAGS=-fsanitize=thread
cmake --build "$build" --target ssh_cli_concurrency_test
# ThreadSanitizer cannot map its shadow memory at every address layout, so address randomisation is
# turned off for its run where the system allows that.
no_randomisation=()
if setarch "$(uname -m)" -R true 2>/dev/null; then
    no_randomisation=(setarch "$(uname -m)" -R)
fi
"${no_randomisation[@]}" ctest --test-dir "$build" --output-on-failure --no-tests=error \
    -R '^Ssh\.cli_concurrency$'
