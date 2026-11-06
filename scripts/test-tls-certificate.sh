#!/usr/bin/env bash
# The TLS certificate module (Issue #190): its Host suites under the debug and sanitizer presets, and
# its PSA ports on TF-PSA-Crypto with what OpenSSL makes of the certificate, the request and the key
# that the real builders produce. It operates no hardware.
#
#   scripts/test-tls-certificate.sh [host|psa|all]
#
# host: the TlsCertificate.* suites, including the differential suite against OpenSSL where its headers
#       exist and the adapter boundary probes, and the security event conformance suite, debug and sanitize,
#       then clang-tidy on the module and its suites.
# psa:  tests/tls_certificate/psa on TF-PSA-Crypto under ASan and UBSan, clang-tidy on the PSA ports,
#       then OpenSSL on the certificate, request and key it writes, and a TLS 1.3 handshake.
#
# CC chooses the compiler for the sanitizer builds. A clang without its sanitizer runtime fails to
# link them, so use gcc there.
set -euo pipefail

mode="${1:-all}"
case "$mode" in host | psa | all) ;; *)
    printf 'usage: %s [host|psa|all]\n' "$0" >&2
    exit 2
    ;;
esac

repo="$(git rev-parse --show-toplevel)"
cache_dir="${CRUX_CACHE_DIR:-${repo}/../.crux-cache}"
out="${CRUX_TLS_CERTIFICATE_OUTPUT:-$repo/build/tls-certificate}"
jobs="${CRUX_TLS_CERTIFICATE_JOBS:-4}"
tidy="${CLANG_TIDY:-clang-tidy-18}"
mkdir -p "$out"

fail() {
    printf 'FAIL: %s\n' "$*" >&2
    exit 1
}

run_host() {
    local preset
    for preset in ${CRUX_TLS_CERTIFICATE_PRESETS:-host-debug host-sanitize}; do
        local build="$out/$preset"
        cmake --preset "$preset" -B "$build" "-DFETCHCONTENT_BASE_DIR=$cache_dir/cmake" > "$out/$preset.log" 2>&1 ||
            { tail -40 "$out/$preset.log" >&2; fail "configuring $preset"; }
        local targets=(tls_certificate_x509 tls_certificate_service tls_certificate_install tls_certificate_store
            tls_certificate_events tls_certificate_cpp_headers security_event_conformance)
        # The differential suite exists only where CMake found OpenSSL's headers.
        if cmake --build "$build" --target help | grep -q '^tls_certificate_differential:'; then
            targets+=(tls_certificate_differential)
        else
            printf 'note: %s has no OpenSSL headers, so the differential suite is not built\n' "$preset"
        fi
        cmake --build "$build" -j "$jobs" --target "${targets[@]}" >> "$out/$preset.log" 2>&1 ||
            { tail -40 "$out/$preset.log" >&2; fail "building $preset"; }
        ctest --test-dir "$build" -R '^(TlsCertificate\.|SecurityEvent\.conformance)' --no-tests=error \
            --output-on-failure -j 2 || fail "the suites under $preset"
    done
    local first
    first="$out/$(printf '%s\n' ${CRUX_TLS_CERTIFICATE_PRESETS:-host-debug host-sanitize} | head -1)"
    "$tidy" -p "$first" --quiet "$repo"/components/tls_certificate/src/*.c "$repo"/tests/tls_certificate/*.c ||
        fail 'clang-tidy on the module and its suites'
}

run_psa() {
    local build="$out/psa" files="$out/psa-files" cc="${CC:-gcc}"
    cmake -S "$repo/tests/tls_certificate/psa" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Debug \
        "-DCMAKE_C_COMPILER=$cc" "-DFETCHCONTENT_BASE_DIR=$cache_dir/cmake" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
        '-DCMAKE_C_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer' \
        '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined' > "$out/psa.log" 2>&1 ||
        { tail -40 "$out/psa.log" >&2; fail 'configuring the PSA composition'; }
    cmake --build "$build" -j "$jobs" >> "$out/psa.log" 2>&1 ||
        { tail -40 "$out/psa.log" >&2; fail 'building the PSA composition'; }
    "$tidy" -p "$build" --quiet "$repo/components/tls_certificate/ports/psa/tls_certificate_psa.c" \
        "$repo/tests/tls_certificate/psa/psa_cases.c" || fail 'clang-tidy on the PSA ports'
    rm -rf "$files" && mkdir -p "$files"
    UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 "$build/tls_certificate_psa_cases" "$files" \
        > "$out/psa-cases.txt" || { cat "$out/psa-cases.txt" >&2; fail 'the PSA ports on TF-PSA-Crypto'; }
    tail -1 "$out/psa-cases.txt"

    command -v openssl > /dev/null || fail 'OpenSSL is needed for the interoperability checks'
    local cert="$files/certificate.der" request="$files/request.der" key="$files/key.der"
    openssl x509 -inform DER -in "$cert" -out "$files/certificate.pem" || fail 'OpenSSL does not parse the certificate'
    openssl x509 -in "$files/certificate.pem" -noout -text > "$files/certificate.txt"
    grep -q 'Version: 3' "$files/certificate.txt" || fail 'not version 3'
    grep -q 'ecdsa-with-SHA256' "$files/certificate.txt" || fail 'not ecdsa-with-SHA256'
    grep -q 'NIST CURVE: P-256' "$files/certificate.txt" || fail 'not P-256'
    grep -q 'DNS:localhost, IP Address:127.0.0.1' "$files/certificate.txt" || fail 'alternative names'
    grep -q 'TLS Web Server Authentication' "$files/certificate.txt" || fail 'extended key usage'
    grep -q 'CA:FALSE' "$files/certificate.txt" || fail 'basic constraints'
    grep -q 'Not After : Dec 31 23:59:59 9999 GMT' "$files/certificate.txt" || fail 'no expiry'
    grep -q 'Not Before: Jan  1 00:00:00 2026 GMT' "$files/certificate.txt" || fail 'not before'
    # A self-signed certificate verifies against itself, for the server purpose.
    openssl verify -purpose sslserver -CAfile "$files/certificate.pem" "$files/certificate.pem" ||
        fail 'OpenSSL does not verify the self-signed certificate'
    openssl req -inform DER -in "$request" -noout -verify 2>&1 | grep -q 'verify OK' ||
        fail 'OpenSSL does not verify the request'
    openssl req -inform DER -in "$request" -noout -text | grep -q 'DNS:localhost, IP Address:127.0.0.1' ||
        fail 'the request lacks the alternative names'
    openssl pkey -inform DER -in "$key" -noout || fail 'OpenSSL does not parse the PKCS #8 key'
    cmp <(openssl pkey -inform DER -in "$key" -pubout) <(openssl x509 -in "$files/certificate.pem" -noout -pubkey) ||
        fail 'the key does not match the certificate'
    openssl req -inform DER -in "$request" -noout -pubkey | cmp - <(openssl pkey -inform DER -in "$key" -pubout) ||
        fail 'the key does not match the request'
    # A TLS 1.3 handshake with OpenSSL on both ends, the client trusting only this certificate, as a
    # browser does once its user accepts it. This is OpenSSL's stack, not Mbed TLS or wolfSSL.
    local port=$((20000 + RANDOM % 20000)) server
    openssl s_server -quiet -tls1_3 -accept "127.0.0.1:$port" -cert "$files/certificate.pem" \
        -key "$key" -keyform DER -naccept 1 > "$out/s_server.log" 2>&1 &
    server=$!
    sleep 1
    printf 'Q\n' | timeout 20 openssl s_client -connect "127.0.0.1:$port" -tls1_3 -servername localhost \
        -verify_hostname localhost -CAfile "$files/certificate.pem" -verify_return_error \
        > "$out/s_client.log" 2>&1 || true
    kill "$server" 2> /dev/null || true
    wait "$server" 2> /dev/null || true
    grep -q 'Verify return code: 0 (ok)' "$out/s_client.log" ||
        { tail -20 "$out/s_client.log" >&2; fail 'the handshake with OpenSSL'; }
    grep -q 'Peer signature type: ECDSA' "$out/s_client.log" || fail 'the server did not sign with ECDSA'
    # The fingerprint text is what OpenSSL, and so a browser's viewer, shows.
    local ours theirs
    ours="$(sed -n 's/^fingerprint //p' "$out/psa-cases.txt")"
    theirs="$(openssl x509 -in "$files/certificate.pem" -noout -fingerprint -sha256 | cut -d= -f2)"
    [[ -n "$ours" && "$ours" == "$theirs" ]] || fail "fingerprint text: '$ours' against '$theirs'"
    printf 'OpenSSL %s: certificate, request and key accepted, fingerprint %s\n' \
        "$(openssl version | cut -d' ' -f2)" "$ours"
    rm -rf "$files"
}

case "$mode" in
host) run_host ;;
psa) run_psa ;;
all)
    run_host
    run_psa
    ;;
esac
