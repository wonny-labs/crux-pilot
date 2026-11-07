#!/usr/bin/env bash
# The management services on native_sim with real PSA providers (Issue #242), and the providers of their
# ports on their own. It operates no hardware: target builds an image and never flashes, resets or runs one.
#
#   scripts/test-native-composition.sh [host|mbed|wolf|target-mbed|target-wolf|e2e-mbed|e2e-wolf|all]
#
# host:   the PSA ports on TF-PSA-Crypto on Linux Host, under ASan and UBSan, with controls, and the existing
#         Access and credential suites on them.
# mbed:   the ports and the composition on native_sim, on TF-PSA-Crypto and Mbed TLS, then the services image,
#         tests/native/management_services: every management service over HTTPS on the same providers, its
#         image check with --services, and its driver over TLS.
# wolf:   the same on wolfPSA and wolfSSL, on a patched copy of Zephyr.
# target-*: the ports compiled for RT1060-EVKB, and the guard against a test random generator there.
# e2e-*:  the Preact and Angular examples built, packaged into tests/native/management_services/e2e
#         (Preact strict, Angular inline), and driven end to end over HTTPS on that profile (Issue #220):
#         the image check, server_checks.py with no browser, and the shared and real-server browser cases
#         in Chrome. They need Node.js 24.22.2 and Chrome (or CRUX_CHROME), and record the mock's run of
#         the same cases apart. No CI workflow runs them yet.
# all:    host, mbed and wolf.
#
# mbed needs the mbedtls and tf-psa-crypto projects, so a default workflow can run it. wolf needs the
# crux-secure-management group, so a default workflow must not: see scripts/check-ssh-boundaries.py.
set -euo pipefail

mode="${1:-all}"
case "$mode" in host | mbed | wolf | target-mbed | target-wolf | e2e-mbed | e2e-wolf | all) ;; *)
    printf 'usage: %s [host|mbed|wolf|target-mbed|target-wolf|e2e-mbed|e2e-wolf|all]\n' "$0" >&2
    exit 2
    ;;
esac
if [[ "$(uname -s)" != Linux ]]; then
    printf 'Run this script in the Linux Dev Container image.\n' >&2
    exit 2
fi

repo="$(git rev-parse --show-toplevel)"
workspace="${CRUX_WORKSPACE:-$(dirname "$repo")}"
cache_dir="${CRUX_CACHE_DIR:-${repo}/../.crux-cache}"
out="${CRUX_COMPOSITION_OUTPUT:-$repo/build/native-composition}"
jobs="${CRUX_COMPOSITION_JOBS:-4}"
board=native_sim/native/64
target_board='mimxrt1060_evk@B//qspi'
composition="$repo/tests/native/management_composition"

fail() {
    printf 'FAIL: %s\n' "$*" >&2
    exit 1
}

run_host() {
    local build="$out/host-ports" cc="${CC:-gcc}"
    cmake -S "$repo/tests/psa_ports/host" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Debug \
        "-DCMAKE_C_COMPILER=$cc" "-DFETCHCONTENT_BASE_DIR=$cache_dir/cmake" \
        '-DCMAKE_C_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer' \
        '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined' > /dev/null
    cmake --build "$build" -j "$jobs" > "$out/host-ports.log" 2>&1 ||
        { tail -40 "$out/host-ports.log" >&2; fail 'the Host build of the ports failed'; }
    UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 "$build/psa_port_cases" ||
        fail 'the PSA ports on TF-PSA-Crypto on Host'
    run_suites_on_the_real_ports
    "$build/psa_port_seal_mapping" || fail 'what the seal port reports for a provider failure'
    check_refusals "$build"
    local control
    for control in 0 1; do
        if "$build/psa_port_cases_stub_$control" > "$out/host-ports-stub-$control.txt"; then
            fail "the ports passed on a stub provider, control $control"
        fi
        printf 'control ok: a stub provider, control %s, fails: %s\n' "$control" "$(tail -1 "$out/host-ports-stub-$control.txt")"
    done
}

# A guard that refuses must refuse for its stated reason. These need no Zephyr: they compile one file or run
# the composition's CMake function, with the settings of a board.
check_refusals() { # Host build directory, for the provider's headers
    local psa_include="$cache_dir/cmake/tf_psa_crypto-src" source guarded log="$out/refusals.log" cc="${CC:-gcc}"
    local includes=("-I$psa_include/include" "-I$psa_include/drivers/builtin/include"
        "-I$repo/components/base/include" "-I$repo/components/file/include"
        "-I$repo/components/access/include" "-I$repo/components/access/adapter" "-I$repo/components/access/src"
        "-I$repo/components/credential/include" "-I$repo/components/credential/adapter"
        "-I$repo/components/credential/src" "-I$repo/components/credential/ports/psa")
    for source in components/access/ports/psa/access_psa.c components/credential/ports/psa/credential_psa.c; do
        for guarded in CONFIG_TEST_RANDOM_GENERATOR CONFIG_FAKE_ENTROPY_NATIVE_SIM; do
            if "$cc" -std=c17 -fsyntax-only "-D$guarded=1" "${includes[@]}" "$repo/$source" > "$log" 2>&1; then
                fail "$source compiled for a board with $guarded"
            fi
            grep -q 'A test random generator must not reach' "$log" || fail "$source: refused for another reason: $(cat "$log")"
            "$cc" -std=c17 -fsyntax-only "-D$guarded=1" -DCONFIG_BOARD_NATIVE_SIM=1 "${includes[@]}" "$repo/$source" ||
                fail "$source: refused on native_sim with $guarded"
        done
    done
    printf 'control ok: the PSA ports refuse a test random generator on a board\n'
    if "$cc" -std=c17 -fsyntax-only -x c "$repo/tests/native/tls_offload_probe/credentials/test_tls_credentials.h" > "$log" 2>&1; then
        fail 'the test certificate compiled outside native_sim'
    fi
    grep -q 'native_sim only' "$log" || fail "the test certificate: refused for another reason: $(cat "$log")"
    printf 'control ok: the test certificate refuses any board but native_sim\n'
    python3 "$composition/check_cmake_guard.py"
    python3 -m unittest discover -s "$repo/tests/build" -p test_composition_image.py
}

# The existing Access and credential suites, with the fakes' hooks kept and their crypto real.
run_suites_on_the_real_ports() {
    local build="$out/host-suites" targets=() suite
    for suite in conformance accounts security keys secrets events; do targets+=("psa_access_$suite"); done
    for suite in conformance_fake security faults_default_slot concurrency; do targets+=("psa_credential_$suite"); done
    cmake --preset host-debug -S "$repo" -B "$build" -DCRUX_TEST_SUITES_ON_PSA=ON > "$out/host-suites.log" 2>&1 ||
        { tail -40 "$out/host-suites.log" >&2; fail 'configuring the suites on the real ports'; }
    cmake --build "$build" -j "$jobs" --target "${targets[@]}" >> "$out/host-suites.log" 2>&1 ||
        { tail -40 "$out/host-suites.log" >&2; fail 'building the suites on the real ports'; }
    ctest --test-dir "$build" -R '^RealPorts\.' --no-tests=error --output-on-failure -j 2 | tail -3
    [[ "${PIPESTATUS[0]}" == 0 ]] || fail 'a suite on the real ports'
    rm -rf "$build"
}

run_ports() { # Twister tag
    (cd "$workspace" && west twister -j "$jobs" -p "$board" -T "$repo/tests/psa_ports/zephyr" \
        -O "$out/twister-ports-$1" --inline-logs --tag "$1")
}

run_composition() { # profile, ZEPHYR_BASE, more configuration
    local profile="$1" build="$out/$1/build"
    mkdir -p "$out/$profile"
    (cd "$workspace" && ZEPHYR_BASE="$2" west build -p always -b "$board" -d "$build" -o="-j$jobs" "$composition" \
        -- "-DEXTRA_CONF_FILE=../tls_offload_probe/$profile.conf;profile-psa.conf$3") > "$out/$profile/build.log" 2>&1 ||
        { tail -40 "$out/$profile/build.log" >&2; fail "$profile: the build failed"; }
    python3 "$repo/scripts/check-composition-image.py" "$build/zephyr/zephyr.exe" --profile "$profile"
    python3 "$composition/run_composition.py" "$build/zephyr/zephyr.exe" --profile "$profile" --output "$out/$profile"
    grep -E '^CONFIG_(NET_NATIVE_OFFLOADED_SOCKETS|NET_SOCKETS_OFFLOAD|FAKE_ENTROPY_NATIVE_SIM|HWINFO_NATIVE|PSA_CRYPTO_PROVIDER_[A-Z]*|MBEDTLS|MBEDTLS_HEAP_SIZE|MBEDTLS_PSA_STATIC_KEY_SLOTS|WOLFSSL|WOLFPSA|WOLFPSA_THREAD_SAFE|CRUX_SYNC_MUTEX_COUNT|CRUX_FILE_FATFS_SIMULATOR)=' \
        "$build/zephyr/.config" > "$out/$profile/config.txt"
    rm -rf "$build"
}

# Every management service over HTTPS on the composition (Issue #242): the image check holds each service, its
# ports, its random path and its route table, and the driver runs the services' cases and the web cases.
run_services() { # profile, ZEPHYR_BASE
    local profile="$1" build="$out/$1/services-build"
    mkdir -p "$out/$profile"
    (cd "$workspace" && ZEPHYR_BASE="$2" west build -p always -b "$board" -d "$build" -o="-j$jobs" \
        "$repo/tests/native/management_services" -- "-DCRUX_WEB_PROFILE=$profile") > "$out/$profile/services-build.log" 2>&1 ||
        { tail -40 "$out/$profile/services-build.log" >&2; fail "$profile: the services image failed to build"; }
    python3 "$repo/scripts/check-composition-image.py" "$build/zephyr/zephyr.exe" --profile "$profile" --services
    python3 "$repo/tests/native/management_services/driver.py" "$build/zephyr/zephyr.exe" --profile "$profile" \
        --output "$out/$profile"
    grep -E '^CONFIG_(NET_NATIVE_OFFLOADED_SOCKETS|FAKE_ENTROPY_NATIVE_SIM|TEST_RANDOM_GENERATOR|PSA_CRYPTO_PROVIDER_[A-Z]*|MBEDTLS_HEAP_SIZE|WOLFSSL|WOLFPSA|CRUX_WEB|CRUX_WEB_PORT|CRUX_WEB_PLAIN_HTTP_FOR_TESTS|CRUX_SYNC_MUTEX_COUNT|CRUX_THREAD_COUNT)=' \
        "$build/zephyr/.config" > "$out/$profile/services-config.txt"
    rm -rf "$build"
}

# Compile only, on the qualified entropy source. A build with a test generator must not compile.
run_target() { # profile: mbed or wolf, conf file
    local build="$out/target-$1" config
    mkdir -p "$out"
    (cd "$workspace" && west build -p always -b "$target_board" -d "$build" -o="-j$jobs" "$repo/tests/psa_ports/zephyr" \
        -- "-DEXTRA_CONF_FILE=../../psa_crypto/zephyr/$2.conf;target-device-id.conf") > "$out/target-$1.log" 2>&1 ||
        { tail -40 "$out/target-$1.log" >&2; fail "$1: the target compile failed"; }
    config="$build/zephyr/.config"
    grep -qx 'CONFIG_ENTROPY_MCUX_TRNG=y' "$config" || fail "$1: the RT1060 entropy driver is not selected"
    ! grep -q '^CONFIG_FAKE_ENTROPY_NATIVE_SIM=y' "$config" || fail "$1: a test entropy source on the target"
    printf '%s on %s, compile only: the ports and the device ID build, nothing run\n' "$1" "$target_board"
    if (cd "$workspace" && west build -p always -b "$target_board" -d "$build" -o="-j$jobs" "$repo/tests/psa_ports/zephyr" \
        -- "-DEXTRA_CONF_FILE=../../psa_crypto/zephyr/$2.conf;target-device-id.conf" -DCONFIG_TEST_RANDOM_GENERATOR=y) \
        > "$out/target-$1-guard.log" 2>&1; then
        fail "$1: a board built with a test random generator"
    fi
    grep -q 'A test random generator must not reach a build of these ports' "$out/target-$1-guard.log" ||
        fail "$1: the build failed for another reason: see $out/target-$1-guard.log"
    printf 'control ok: %s: a board cannot build with a test random generator\n' "$1"
    rm -rf "$build"
}

# The examples end to end on native_sim over HTTPS (Issue #220). Each example is packaged as a product would
# package it, the image is checked as the services image is, and the browser cases run against it.
e2e_prepare() {
    local node_version
    node_version="$(node --version 2>/dev/null || true)"
    [[ "$node_version" == "v$(sed -n 's/.*"node": "\([0-9.]*\)".*/\1/p' "$repo/web/package.json")" ]] ||
        fail "the examples need Node.js $(sed -n 's/.*"node": "\([0-9.]*\)".*/\1/p' "$repo/web/package.json"), found ${node_version:-none}"
    [[ -n "${CRUX_CHROME:-}" ]] || command -v google-chrome > /dev/null || fail 'the browser cases need Chrome, or CRUX_CHROME'
    [[ -d "$repo/web/node_modules" ]] || (cd "$repo/web" && npm ci --no-audit --no-fund) > "$out/e2e-npm.log" 2>&1 ||
        fail 'npm ci in web'
    (cd "$repo/web" && node tools/build.ts) > "$out/e2e-web-build.log" 2>&1 || fail 'the Preact build'
    local angular="$repo/samples/web/angular-minimal"
    [[ -d "$angular/node_modules" ]] || (cd "$angular" && npm ci --no-audit --no-fund) >> "$out/e2e-npm.log" 2>&1 ||
        fail 'npm ci in the Angular example'
    (cd "$angular" && npm run build) > "$out/e2e-angular-build.log" 2>&1 || fail 'the Angular build'
    python3 -m unittest "$repo/tests/build/test_e2e_server_checks.py" "$repo/tests/build/test_project_sample.py"
    # The services image's response deadline, 1,500 ms, sizes the largest asset at 49,152 bytes, which the
    # Angular script exceeds. The e2e image takes the default 3,000 ms. This control holds that finding.
    if python3 "$repo/components/web/tools/web_assets.py" "$angular/dist" --style inline --response-deadline-ms 1500 \
        > "$out/e2e-angular-1500.log" 2>&1; then
        fail 'the Angular example fitted a 1,500 ms deadline: update docs/e2e-native-220.md and e2e.conf'
    fi
    python3 "$repo/components/web/tools/web_assets.py" "$angular/dist" --style inline --response-deadline-ms 3000 \
        > "$out/e2e-angular-3000.log" 2>&1 || fail 'the Angular example does not fit the default deadline'
    printf 'control ok: the Angular example needs the default 3,000 ms deadline: %s\n' \
        "$(grep -m1 'exceeds' "$out/e2e-angular-1500.log" || true)"
}

run_e2e_app() { # profile, ZEPHYR_BASE, app, folder, style, test file
    local profile="$1" app="$3" build="$out/$1/e2e-$3-build" results="$out/$1"
    mkdir -p "$results"
    (cd "$workspace" && ZEPHYR_BASE="$2" west build -p always -b "$board" -d "$build" -o="-j$jobs" \
        "$repo/tests/native/management_services/e2e" -- "-DCRUX_WEB_PROFILE=$profile" "-DCRUX_E2E_FOLDER=$4" \
        "-DCRUX_E2E_STYLE=$5") > "$results/e2e-$app-build.log" 2>&1 ||
        { tail -40 "$results/e2e-$app-build.log" >&2; fail "$profile: the $app image failed to build"; }
    python3 "$repo/scripts/check-composition-image.py" "$build/zephyr/zephyr.exe" --profile "$profile" --services
    cp "$build/crux_web_assets/app/crux-web-assets.json" "$results/e2e-$app-assets.json"
    local status=0
    python3 "$repo/tests/native/management_services/e2e/server_checks.py" "$build/zephyr/zephyr.exe" \
        --build "$build" --style "$5" --profile "$profile" --app "$app" --output "$results" || status=1
    local upper
    upper="$(printf '%s' "$app" | tr '[:lower:]' '[:upper:]')"
    rm -f "$results/e2e-$app-browser.jsonl"
    (cd "$repo/web" && env "CRUX_E2E_IMAGE_$upper=$build/zephyr/zephyr.exe" CRUX_REQUIRE_NATIVE=1 \
        "CRUX_E2E_RESULTS=$results/e2e-$app-browser.jsonl" "CRUX_E2E_PROFILE=$profile" \
        node --test --test-concurrency=1 --test-reporter=spec "e2e/$6") > "$results/e2e-$app-browser.log" 2>&1 || status=1
    tail -12 "$results/e2e-$app-browser.log"
    python3 "$repo/tests/native/management_services/e2e/collect_results.py" "$results/e2e-$app-browser.jsonl" \
        --target native_sim --profile "$profile" --output "$results/e2e-$app-browser-results.json"
    rm -rf "$build"
    return "$status"
}

run_e2e() { # profile, ZEPHYR_BASE
    local status=0
    run_e2e_app "$1" "$2" preact "$repo/web/reference/dist" strict native-reference.test.ts || status=1
    run_e2e_app "$1" "$2" angular "$repo/samples/web/angular-minimal/dist" inline native-angular.test.ts || status=1
    # The mock's run of the same shared cases, recorded apart: it is not TLS, session or limit evidence.
    rm -f "$out/$1/e2e-mock-browser.jsonl"
    (cd "$repo/web" && CRUX_E2E_RESULTS="$out/$1/e2e-mock-browser.jsonl" CRUX_REQUIRE_ANGULAR=1 \
        node --test --test-concurrency=1 --test-reporter=spec e2e/reference-shared.test.ts e2e/angular-shared.test.ts) \
        > "$out/$1/e2e-mock-browser.log" 2>&1 || status=1
    tail -8 "$out/$1/e2e-mock-browser.log"
    python3 "$repo/tests/native/management_services/e2e/collect_results.py" "$out/$1/e2e-mock-browser.jsonl" \
        --target mock --profile none --output "$out/$1/e2e-mock-browser-results.json"
    [[ "$status" == 0 ]] || fail "$1: the examples end to end"
}

mkdir -p "$out"
printf 'Crux revision: %s\n' "$(git -C "$repo" rev-parse HEAD)"
export ZEPHYR_BASE="$workspace/zephyr"
case "$mode" in host | all) run_host ;; esac
if [[ "$mode" == target-* ]]; then
    unset ZEPHYR_TOOLCHAIN_VARIANT
    case "$mode" in
        target-mbed) run_target mbed tf-psa-crypto ;;
        target-wolf) run_target wolf wolfpsa ;;
    esac
else
    export ZEPHYR_TOOLCHAIN_VARIANT=host
    case "$mode" in mbed | all)
        run_ports psa-ports-tf-psa-crypto
        run_composition mbed "$workspace/zephyr" ";mbed-threading.conf"
        run_services mbed "$workspace/zephyr"
        ;;
    esac
    case "$mode" in wolf | all)
        run_ports psa-ports-wolfpsa
        "$repo/scripts/prepare-wolf-tls-zephyr.sh" "$out/zephyr-wolf"
        run_composition wolf "$out/zephyr-wolf" ""
        run_services wolf "$out/zephyr-wolf"
        ;;
    esac
    case "$mode" in
        e2e-mbed)
            e2e_prepare
            run_e2e mbed "$workspace/zephyr"
            ;;
        e2e-wolf)
            e2e_prepare
            "$repo/scripts/prepare-wolf-tls-zephyr.sh" "$out/zephyr-wolf"
            run_e2e wolf "$out/zephyr-wolf"
            ;;
    esac
fi
printf 'Native composition (%s) finished.\n' "$mode"
