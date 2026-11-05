#!/usr/bin/env bash
# Web contract and private HTTP adapter (Issue #184).
#
#   scripts/test-web.sh [host|native|target|all] [mbedtls|wolfssl|both]
#
# host:   the suites under debug and sanitizer presets on a fake transport, clang-tidy, the boundary
#         check, the route generator's tests and the review lists.
# native: the adapter on the real Zephyr server, on native_sim over plain HTTP on loopback with the
#         native socket offload, with tests/native/web_http/test_driver.py as the client.
# target: compiles and links the adapter for mimxrt1060_evk@B//qspi on a TLS profile. It builds images
#         and never flashes, resets or runs one. mbedtls needs the optional Mbed TLS modules, and
#         wolfssl also the wolfSSL module and a copy of Zephyr with the socket patch. The PSA workflow runs
#         mbedtls, and no default workflow runs wolfssl, because none selects a GPL stack:
#         west config manifest.group-filter -- +crux-secure-management
# all:    host and native.
set -euo pipefail

mode="${1:-all}"
profile="${2:-both}"
case "$mode" in host | native | target | all) ;; *)
    printf 'usage: %s [host|native|target|all] [mbedtls|wolfssl|both]\n' "$0" >&2
    exit 2
    ;;
esac
case "$profile" in mbedtls | wolfssl | both) ;; *)
    printf 'usage: %s [host|native|target|all] [mbedtls|wolfssl|both]\n' "$0" >&2
    exit 2
    ;;
esac
if [[ "$(uname -s)" != Linux ]]; then
    printf 'Web checks are configured for Linux only; reopen this repository in the Dev Container.\n' >&2
    exit 2
fi

repo="$(git rev-parse --show-toplevel)"
workspace="${CRUX_WORKSPACE:-$(dirname "$repo")}"
export CRUX_CACHE_DIR="${CRUX_CACHE_DIR:-${repo}/../.crux-cache}"
output="${CRUX_WEB_OUTPUT:-$repo/build/web}"
jobs="${CRUX_WEB_JOBS:-4}"
modules="$workspace/modules"
tidy="${CLANG_TIDY:-clang-tidy-18}"
native_board='native_sim/native/64'
target_board='mimxrt1060_evk@B//qspi'
zephyr_revision=dccb09599635bdff17633fa7e9dab014b91dce90
# As in scripts/test-ssh-wolf-settings.sh. See patches/zephyr/README.md.
patch_url=https://raw.githubusercontent.com/wolfSSL/osp/8c32b060ac6017b1291a6da0875a48637bfb682d/zephyr/4.4/zephyr-tls-4.4.1.patch
patch_sha256=3d1cc0307bbeea5f000698bca5be717f73fdb8c47e90083b003336364e8fdbda
merged_tree=1d00f38c0d029156fb1d34a109fe41a6a7006312

fail() {
    printf 'FAIL: %s\n' "$*" >&2
    exit 1
}

run_host() {
    local targets=(web_routing web_enforcement web_requests web_cpp_headers web_session_login web_session_sessions
        web_session_json web_session_concurrency web_session_cpp_header web_management_status web_management_config
        web_management_save web_management_account web_management_fuzz web_management_cpp_header)
    for preset in host-debug host-sanitize; do
        cmake --preset "$preset" -S "$repo" -DFETCHCONTENT_FULLY_DISCONNECTED=ON
        cmake --build --preset "$preset" --target "${targets[@]}"
        ctest --test-dir "$repo/build/linux/${preset#host-}" --output-on-failure --no-tests=error -R '^Web\.'
    done
    "$tidy" -p "$repo/build/linux/debug" "$repo"/components/web/src/*.c "$repo"/tests/web/*.c \
        "$repo"/components/web/session/*.c "$repo"/tests/web/session/*.c \
        "$repo"/components/web/management/*.c "$repo"/tests/web/management/*.c
    python3 "$repo/scripts/check-web-boundary.py"
    python3 -m unittest discover -s "$repo/tests/build" -p test_web_routes.py
    python3 "$repo/scripts/check-web-routes.py"
    python3 "$repo/scripts/check-web-routes.py" --reviewed "$repo/tests/native/web_http/reviewed-routes.txt" \
        "$repo"/tests/native/web_http/src/probe_routes.c "$repo"/tests/native/web_http/src/project_routes.c
}

run_native() {
    local build="$output/native"
    ZEPHYR_BASE="$workspace/zephyr" cmake -S "$repo/tests/native/web_http" -B "$build" -G Ninja \
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
    python3 "$repo/tests/native/web_http/test_driver.py" "$build/zephyr/zephyr.exe" \
        --results "$output/native-results.json"
    printf 'native_sim result, plain HTTP on loopback, not device evidence: %s\n' "$output/native-results.json"
}

prepare_patched_zephyr() {
    local tree="$output/zephyr-wolf" patch="$output/zephyr-tls-4.4.1.patch"
    if [[ ! -d "$tree/.git" ]]; then
        if [[ ! -f "$patch" ]]; then
            curl -fsSL -o "$patch" "$patch_url"
        fi
        printf '%s  %s\n' "$patch_sha256" "$patch" | sha256sum --check --quiet
        git clone -q --shared --no-checkout "$workspace/zephyr" "$tree"
        git -C "$tree" config core.sparseCheckout true
        git -C "$tree" config core.sparseCheckoutCone false
        printf '/*\n!/doc/\n!/tests/\n!/samples/\n' > "$tree/.git/info/sparse-checkout"
        git -C "$tree" checkout -q --detach "$zephyr_revision"
        git -C "$tree" apply --3way "$patch"
    fi
    [[ "$(git -C "$tree" write-tree)" == "$merged_tree" ]] || fail "$tree does not hold the expected merge"
    patched_zephyr="$tree"
}

run_target() { # profile
    local selected="$1" build="$output/target-$1" tree="$workspace/zephyr" list tls_symbol
    local nm="$ZEPHYR_SDK_INSTALL_DIR/gnu/arm-zephyr-eabi/bin/arm-zephyr-eabi-nm"
    list="$modules/hal/cmsis;$modules/hal/cmsis_6;$modules/hal/nxp;$repo"
    if [[ "$selected" == wolfssl ]]; then
        prepare_patched_zephyr
        tree="$patched_zephyr"
        list="$list;$modules/crypto/wolfssl"
        tls_symbol=wolfSSL_accept
    else
        list="$list;$modules/crypto/mbedtls;$modules/crypto/tf-psa-crypto"
        tls_symbol=mbedtls_ssl_handshake
    fi
    ZEPHYR_BASE="$tree" cmake -S "$repo/tests/zephyr/web_compile" -B "$build" -G Ninja -DBOARD="$target_board" \
        "-DEXTRA_CONF_FILE=$repo/tests/zephyr/web_compile/$selected.conf" "-DZEPHYR_MODULES=$list" \
        "-DPython3_EXECUTABLE=$(command -v python3)"
    ninja -C "$build" -j "$jobs" | tee "$build.log" | tail -4
    grep -E '^ *(FLASH|RAM):' "$build.log"
    grep -qx 'CONFIG_HTTP_SERVER_RESOURCE_WILDCARD=y' "$build/zephyr/.config" || fail "wildcard resources are off"
    grep -qx 'CONFIG_NET_SOCKETS_SOCKOPT_TLS=y' "$build/zephyr/.config" || fail "the TLS socket is off"
    if grep -q '^CONFIG_CRUX_WEB_PLAIN_HTTP_FOR_TESTS=y' "$build/zephyr/.config"; then
        fail "a target image must not hold plain HTTP"
    fi
    "$nm" "$build/zephyr/zephyr.elf" > "$build/symbols.txt"
    for symbol in fnmatch crux_web_zephyr_callback "$tls_symbol"; do
        grep -q " $symbol\$" "$build/symbols.txt" || fail "$symbol is missing from the image"
    done
    # The session module of #186 compiles for the target. It is not linked, see the CMakeLists.
    local session_objects=4
    if [[ "$selected" == mbedtls ]]; then session_objects=5; fi
    [[ "$(find "$build" -path '*crux_web_session_compile.dir*' -name '*.obj' | wc -l)" -eq "$session_objects" ]] ||
        fail "the session module did not compile for the target"
    printf 'Target compile result (not flashed): %s on the %s profile.\n' "$target_board" "$selected"
}

mkdir -p "$output"
case "$mode" in
    host) run_host ;;
    native) run_native ;;
    all)
        run_host
        run_native
        ;;
    target)
        if [[ "$profile" == mbedtls || "$profile" == both ]]; then run_target mbedtls; fi
        if [[ "$profile" == wolfssl || "$profile" == both ]]; then run_target wolfssl; fi
        ;;
esac
