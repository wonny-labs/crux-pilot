#!/usr/bin/env bash
# Packaged web assets (Issue #189): the packager, its build function, the headers and routes on the
# real web core, and the assets on the real Zephyr server.
#
#   scripts/test-web-assets.sh [host|native|all]
#
# host:   the packager's and the build function's tests, the Host suites under the debug and sanitizer
#         presets for both content policies, clang-tidy on the new C, and the route review checks.
# native: the image of tests/native/web_assets on native_sim over plain HTTP on loopback, with
#         tests/native/web_assets/test_driver.py as the client: headers, encoding, the hash list, a
#         client that disconnects mid-asset and a reader that stops. It builds one image.
# all:    host and native.
set -euo pipefail

mode="${1:-all}"
case "$mode" in host | native | all) ;; *)
    printf 'usage: %s [host|native|all]\n' "$0" >&2
    exit 2
    ;;
esac
if [[ "$(uname -s)" != Linux ]]; then
    printf 'Web asset checks are configured for Linux only; reopen this repository in the Dev Container.\n' >&2
    exit 2
fi

repo="$(git rev-parse --show-toplevel)"
workspace="${CRUX_WORKSPACE:-$(dirname "$repo")}"
export CRUX_CACHE_DIR="${CRUX_CACHE_DIR:-${repo}/../.crux-cache}"
output="${CRUX_WEB_ASSETS_OUTPUT:-$repo/build/web-assets}"
jobs="${CRUX_WEB_JOBS:-4}"
tidy="${CLANG_TIDY:-clang-tidy-18}"
native_board='native_sim/native/64'

fail() {
    printf 'FAIL: %s\n' "$*" >&2
    exit 1
}

run_host() {
    local targets=(web_assets_strict web_assets_inline web_assets_cpp_header)
    for preset in host-debug host-sanitize; do
        cmake --preset "$preset" -S "$repo" -DFETCHCONTENT_FULLY_DISCONNECTED=ON
        cmake --build --preset "$preset" --target "${targets[@]}"
        ctest --test-dir "$repo/build/linux/${preset#host-}" --output-on-failure --no-tests=error -R '^Web\.assets'
    done
    "$tidy" -p "$repo/build/linux/debug" "$repo"/components/web/assets/*.c "$repo"/tests/web/assets/*.c
    python3 -m unittest discover -s "$repo/tests/build" -p test_web_assets.py
    python3 -m unittest discover -s "$repo/tests/build" -p test_web_routes.py
    python3 "$repo/scripts/check-web-routes.py"
    python3 "$repo/scripts/check-web-boundary.py"
}

run_native() {
    local build="$output/native"
    ZEPHYR_BASE="$workspace/zephyr" cmake -S "$repo/tests/native/web_assets" -B "$build" -G Ninja \
        -DBOARD="$native_board" "-DZEPHYR_MODULES=$repo" "-DPython3_EXECUTABLE=$(command -v python3)"
    ninja -C "$build" -j "$jobs"
    # The image must use Zephyr's fnmatch, as a target does. A test on the host's would show nothing.
    nm "$build/zephyr/zephyr.exe" > "$build/symbols.txt"
    if grep -q 'U fnmatch@' "$build/symbols.txt"; then
        fail "the image uses the host's fnmatch, which reads Zephyr's flags differently"
    fi
    grep -q ' [tT] fnmatch$' "$build/symbols.txt" || fail "the image has no Zephyr fnmatch"
    grep -qx 'CONFIG_NET_NATIVE_OFFLOADED_SOCKETS=y' "$build/zephyr/.config" ||
        fail "the image must use the native socket offload"
    hash_list="$(find "$build" -name crux-web-assets.json | head -1)"
    [[ -n "$hash_list" ]] || fail "the build wrote no asset hash list"
    python3 "$repo/tests/native/web_assets/test_driver.py" "$build/zephyr/zephyr.exe" \
        --site "$build/site" --hash-list "$hash_list" --results "$output/native-results.json"
    printf 'native_sim result, plain HTTP on loopback, not device evidence: %s\n' "$output/native-results.json"
}

mkdir -p "$output"
case "$mode" in
    host) run_host ;;
    native) run_native ;;
    all)
        run_host
        run_native
        ;;
esac
