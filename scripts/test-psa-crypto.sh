#!/usr/bin/env bash
# Trial qualification of the provider-neutral PSA Crypto conformance cases (Issue #142).
#
# The provider sources are not part of this repository and west.yml is not changed. They come
# from a trial directory outside the source tree (default /opt/crux-cache/psa-trial) holding
# tf-psa-crypto and mbedtls at the revisions the pinned Zephyr manifest names. Run inside the
# Linux Dev Container image with the west workspace configured.
#
#   scripts/test-psa-crypto.sh [host|zephyr|all]
#
# Host:   no-provider and stub-provider controls, then TF-PSA-Crypto with and without ASan/UBSan.
# Zephyr: Twister on native_sim/native/64 with the same cases.
# Neither part operates hardware, and neither implies the other.
set -euo pipefail

repo="$(git rev-parse --show-toplevel)"
workspace="${CRUX_WORKSPACE:-$(dirname "$repo")}"
trial="${CRUX_PSA_TRIAL_DIR:-${CRUX_CACHE_DIR:-/opt/crux-cache}/psa-trial}"
mode="${1:-all}"
tfpsa="$trial/tf-psa-crypto"
mbedtls="$trial/mbedtls"
out="$repo/build/psa-crypto"

expected_tfpsa=dc575a2ddcc8cb16275d24c42a52eaf79ebe2231
expected_mbedtls=a3e190fe44c78d1ba67f55979e1257328cc7d0d8

case "$mode" in host | zephyr | all) ;; *)
    printf 'usage: %s [host|zephyr|all]\n' "$0" >&2
    exit 2
    ;;
esac
if [[ "$(uname -s)" != Linux ]]; then
    printf 'Run this script in the Linux Dev Container image.\n' >&2
    exit 2
fi

check_revision() {
    local dir="$1" expected="$2" actual
    actual="$(git -C "$dir" rev-parse HEAD 2>/dev/null || true)"
    if [[ "$actual" != "$expected" ]]; then
        printf '%s is at %s, expected %s\n' "$dir" "${actual:-<not a git checkout>}" "$expected" >&2
        exit 1
    fi
}
check_revision "$tfpsa" "$expected_tfpsa"
if [[ "$mode" != host ]]; then
    check_revision "$mbedtls" "$expected_mbedtls"
fi
printf 'Crux revision: %s\n' "$(git -C "$repo" rev-parse HEAD)"
printf 'tf-psa-crypto: %s\n' "$expected_tfpsa"
[[ "$mode" == host ]] || printf 'mbedtls module: %s\n' "$expected_mbedtls"

host_configure() {
    local name="$1"
    shift
    cmake -S "$repo/tests/psa_crypto/host" -B "$out/$name" -G Ninja -DCMAKE_BUILD_TYPE=Debug \
        -DCRUX_PSA_TF_PSA_CRYPTO_DIR="$tfpsa" "$@"
    cmake --build "$out/$name"
}

run_host() {
    rm -rf "$out"
    mkdir -p "$out"

    # Control 1: with no provider selected the cases cannot be configured.
    if cmake -S "$repo/tests/psa_crypto/host" -B "$out/none" -G Ninja > "$out/none.log" 2>&1; then
        printf 'FAIL: configuring without a provider unexpectedly succeeded\n' >&2
        exit 1
    fi
    grep -q 'CRUX_PSA_PROVIDER is not set' "$out/none.log"
    printf 'control ok: no provider selected fails to configure\n'

    # Control 2: a provider that supports nothing fails every case.
    host_configure stub-unsupported -DCRUX_PSA_PROVIDER=stub-unsupported > /dev/null
    if "$out/stub-unsupported/psa_conformance" > "$out/stub-unsupported.txt"; then
        printf 'FAIL: the unsupported stub passed the cases\n' >&2
        exit 1
    fi
    if grep -q '^PASS' "$out/stub-unsupported.txt"; then
        printf 'FAIL: a case passed against the unsupported stub\n' >&2
        exit 1
    fi
    printf 'control ok: unsupported stub fails every case\n'

    # Control 3: a provider that accepts everything may pass only the positive
    # cases whose expected status is success.
    host_configure stub-permissive -DCRUX_PSA_PROVIDER=stub-permissive > /dev/null
    if "$out/stub-permissive/psa_conformance" > "$out/stub-permissive.txt"; then
        printf 'FAIL: the permissive stub passed the cases\n' >&2
        exit 1
    fi
    passed="$({ grep '^PASS' "$out/stub-permissive.txt" || true; } | awk '{print $2}' | sort | tr '\n' ' ')"
    allowed='ecdsa_p256_verify_hash_known_answer init_is_idempotent '
    if [[ "$passed" != "$allowed" ]]; then
        printf 'FAIL: unexpected cases passed against the permissive stub: %s\n' "$passed" >&2
        exit 1
    fi
    printf 'control ok: permissive stub fails every case that expects an error status\n'

    # The real provider, without and with sanitizers.
    host_configure tf-psa-crypto -DCRUX_PSA_PROVIDER=tf-psa-crypto > /dev/null
    ctest --test-dir "$out/tf-psa-crypto" --output-on-failure
    "$out/tf-psa-crypto/psa_conformance" | tee "$out/tf-psa-crypto.txt"

    host_configure tf-psa-crypto-sanitize -DCRUX_PSA_PROVIDER=tf-psa-crypto \
        -DCMAKE_C_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
        -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined' > /dev/null
    ctest --test-dir "$out/tf-psa-crypto-sanitize" --output-on-failure
    "$out/tf-psa-crypto-sanitize/psa_conformance" > "$out/tf-psa-crypto-sanitize.txt"
    printf 'Host TF-PSA-Crypto: %s\n' "$(tail -1 "$out/tf-psa-crypto-sanitize.txt")"
}

run_zephyr() {
    export ZEPHYR_BASE="$workspace/zephyr"
    export ZEPHYR_TOOLCHAIN_VARIANT=host
    (cd "$workspace" && west twister -j 2 -p native_sim/native/64 \
        -T "$repo/tests/psa_crypto/zephyr" \
        -O "$repo/build/zephyr/twister-psa-crypto" --inline-logs \
        --extra-args="ZEPHYR_MODULES=$mbedtls;$tfpsa")
}

[[ "$mode" == zephyr ]] || run_host
[[ "$mode" == host ]] || run_zephyr
printf 'PSA Crypto trial (%s) finished.\n' "$mode"
