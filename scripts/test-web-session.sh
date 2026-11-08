#!/usr/bin/env bash
# Login, sessions and request forgery protection (Issue #186).
#
#   scripts/test-web-session.sh [host|native|all]
#
# host:   the session suites under the debug preset, and under host-sanitize when the toolchain has
#         its runtime, on the real Web and Access cores with fake ports and a fake transport, with
#         clang-tidy and the review lists. scripts/test-web.sh host runs the same suites in CI.
# native: tests/native/web_session on native_sim, Mbed profile: the session module and the web adapter
#         over plain HTTP on loopback, on the management composition with the real Access core and its
#         real PSA ports, with tests/native/web_session/test_driver.py as the client. It needs the
#         mbedtls and tf-psa-crypto projects. Entropy there is test-only, so no token strength is shown.
#         It operates no hardware.
# all:    host and native.
set -euo pipefail

mode="${1:-all}"
case "$mode" in host | native | all) ;; *)
    printf 'usage: %s [host|native|all]\n' "$0" >&2
    exit 2
    ;;
esac
if [[ "$(uname -s)" != Linux ]]; then
    printf 'Run this script in the Linux Dev Container image.\n' >&2
    exit 2
fi

repo="$(git rev-parse --show-toplevel)"
workspace="${CRUX_WORKSPACE:-$(dirname "$repo")}"
export CRUX_CACHE_DIR="${CRUX_CACHE_DIR:-${repo}/../.crux-cache}"
output="${CRUX_WEB_SESSION_OUTPUT:-$repo/build/web-session}"
jobs="${CRUX_WEB_SESSION_JOBS:-4}"
tidy="${CLANG_TIDY:-clang-tidy-18}"
modules="$workspace/modules"
board=native_sim/native/64
image="$repo/tests/native/web_session"
suites=(web_session_login web_session_sessions web_session_json web_session_concurrency web_session_cpp_header)

fail() {
    printf 'FAIL: %s\n' "$*" >&2
    exit 1
}

run_host() {
    local presets=(host-debug) asan
    # The sanitizer runtime is not in every image, and CI's host workflow always has it.
    asan="$("${CC:-clang-18}" -print-file-name=libclang_rt.asan-x86_64.a 2>/dev/null || true)"
    if [[ -f "$asan" ]]; then
        presets+=(host-sanitize)
    else
        printf 'host-sanitize skipped: this toolchain has no ASan runtime\n'
    fi
    for preset in "${presets[@]}"; do
        cmake --preset "$preset" -S "$repo" -DFETCHCONTENT_FULLY_DISCONNECTED=ON > /dev/null
        cmake --build --preset "$preset" --target "${suites[@]}"
        ctest --test-dir "$repo/build/linux/${preset#host-}" --output-on-failure --no-tests=error \
            -R '^Web\.session_'
    done
    "$tidy" -p "$repo/build/linux/debug" "$repo"/components/web/session/*.c "$repo"/tests/web/session/*.c
    python3 "$repo/scripts/check-web-routes.py"
    python3 "$repo/scripts/check-web-routes.py" --reviewed "$image/reviewed-routes.txt" \
        "$repo/components/web/session/web_session_routes.c" "$image/src/test_routes.c"
}

run_native() {
    local build="$output/native" symbols
    mkdir -p "$output"
    ZEPHYR_BASE="$workspace/zephyr" cmake -S "$image" -B "$build" -G Ninja -DBOARD="$board" \
        "-DZEPHYR_MODULES=$repo;$modules/fs/fatfs;$modules/crypto/mbedtls;$modules/crypto/tf-psa-crypto" \
        "-DEXTRA_CONF_FILE=../tls_offload_probe/mbed.conf;../management_composition/profile-psa.conf;../management_composition/mbed-threading.conf" \
        "-DPython3_EXECUTABLE=$(command -v python3)" > "$output/native-configure.log" 2>&1 ||
        { tail -40 "$output/native-configure.log" >&2; fail 'configuring the image'; }
    ninja -C "$build" -j "$jobs" > "$output/native-build.log" 2>&1 ||
        { tail -40 "$output/native-build.log" >&2; fail 'building the image'; }
    grep -qx 'CONFIG_NET_NATIVE_OFFLOADED_SOCKETS=y' "$build/zephyr/.config" ||
        fail 'the image must use the native socket offload'
    grep -qx 'CONFIG_FAKE_ENTROPY_NATIVE_SIM=y' "$build/zephyr/.config" ||
        fail 'the entropy declaration changed: review docs/web-session.md'
    # The real providers, from their own sources: no fake port, and Zephyr's fnmatch.
    symbols="$output/native-symbols.txt"
    nm -l "$build/zephyr/zephyr.exe" > "$symbols"
    for pair in crux_web_session_port_random:components/web/ports/psa/web_session_psa.c \
        crux_web_session_port_hash:components/web/ports/psa/web_session_psa.c \
        crux_access_port_derive:components/access/ports/psa/access_psa.c \
        crux_access_port_random:components/access/ports/psa/access_psa.c \
        crux_web_session_authenticate:components/web/session/web_session.c \
        crux_access_login_password:components/access/src/access_login.c; do
        grep -E " [tT] ${pair%%:*}[[:space:]].*${pair#*:}" "$symbols" > /dev/null ||
            fail "${pair%%:*} is not from ${pair#*:}"
    done
    if grep -q 'U fnmatch@' "$symbols"; then
        fail "the image uses the host's fnmatch, which reads Zephyr's flags differently"
    fi
    python3 "$image/test_driver.py" "$build/zephyr/zephyr.exe" --results "$output/native-results.json"
    printf 'native_sim result, plain HTTP on loopback, test-only entropy, not device evidence: %s\n' \
        "$output/native-results.json"
    rm -rf "$build"
}

case "$mode" in
    host) run_host ;;
    native) run_native ;;
    all)
        run_host
        run_native
        ;;
esac
