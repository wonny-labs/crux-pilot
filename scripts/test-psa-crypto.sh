#!/usr/bin/env bash
# Qualification of the provider-neutral PSA Crypto conformance cases (Issue #142).
#
#   scripts/test-psa-crypto.sh [host|zephyr|target|all] [all|tf-psa-crypto]
#
# host:   no-provider and stub-provider controls, then TF-PSA-Crypto and wolfPSA with and without
#         ASan and UBSan, and a check that each provider's pin matches west.yml.
# zephyr: Twister on native_sim/native/64 for both providers, then the entropy declaration.
# target: compiles the conformance application for mimxrt1060_evk@B//qspi with both providers and
#         checks the resolved entropy source. It builds images and never flashes, resets or runs one.
# all:    host and zephyr. target also needs the Zephyr SDK and hal_nxp.
#
# Each part needs a west workspace with the optional stacks, and none implies another:
#   west config manifest.group-filter -- +crux-secure-management && west update
#
# The second argument limits a run to TF-PSA-Crypto, so that no GPL stack is needed. A default workflow
# must not select the wolf stacks, and scripts/check-ssh-boundaries.py holds it to that, so CI uses this:
#   west config manifest.project-filter -- +mbedtls,+tf-psa-crypto && west update
set -euo pipefail

mode="${1:-all}"
scope="${2:-all}"
case "$mode" in host | zephyr | target | all) ;; *)
    printf 'usage: %s [host|zephyr|target|all] [all|tf-psa-crypto]\n' "$0" >&2
    exit 2
    ;;
esac
case "$scope" in all | tf-psa-crypto) ;; *)
    printf 'usage: %s [host|zephyr|target|all] [all|tf-psa-crypto]\n' "$0" >&2
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
out="${CRUX_PSA_OUTPUT:-$repo/build/psa-crypto}"
jobs="${CRUX_PSA_JOBS:-4}"
modules="$workspace/modules/crypto"
host_source="$repo/tests/psa_crypto/host"
zephyr_source="$repo/tests/psa_crypto/zephyr"
board='mimxrt1060_evk@B//qspi'
native_board='native_sim/native/64'

# Case counts, so a run that silently shrinks fails. Host runs the cases, and Zephyr adds a list check.
case_count=41
tf_unsupported=3
wolf_deviations=2

fail() {
    printf 'FAIL: %s\n' "$*" >&2
    exit 1
}

require_module() { # name, path
    if [[ ! -d "$2/.git" && ! -f "$2/.git" ]]; then
        printf '%s is missing from %s. Enable the optional stacks: west config manifest.group-filter -- +crux-secure-management && west update\n' \
            "$1" "$2" >&2
        exit 2
    fi
}

pinned_revision() { # project name in west.yml
    awk -v name="$1" '
        $1 == "-" && $2 == "name:" { in_project = ($3 == name) }
        in_project && $1 == "revision:" { print $2; exit }
    ' "$repo/west.yml"
}

zephyr_pinned_revision() { # project name in Zephyr's own west.yml
    awk -v name="$1" '
        $1 == "-" && $2 == "name:" { in_project = ($3 == name) }
        in_project && $1 == "revision:" { print $2; exit }
    ' "$workspace/zephyr/west.yml"
}

# One pin for each provider. The Host archive, west.yml, Zephyr's manifest and the checkout must agree,
# or this tests a different provider from the one a Zephyr image builds.
check_pins() {
    local cmake_pin module pin actual
    cmake_pin="$(sed -n 's/^set(CRUX_TF_PSA_CRYPTO_REVISION \([0-9a-f]*\))$/\1/p' "$repo/cmake/CruxPsaCrypto.cmake")"
    [[ -n "$cmake_pin" ]] || fail 'no revision in cmake/CruxPsaCrypto.cmake'
    [[ "$cmake_pin" == "$(pinned_revision tf-psa-crypto)" ]] ||
        fail "cmake/CruxPsaCrypto.cmake pins $cmake_pin, and west.yml pins $(pinned_revision tf-psa-crypto)"
    for module in mbedtls tf-psa-crypto; do
        pin="$(pinned_revision "$module")"
        [[ "$pin" == "$(zephyr_pinned_revision "$module")" ]] ||
            fail "west.yml pins $module at $pin, and Zephyr's manifest at $(zephyr_pinned_revision "$module")"
    done
    local checked=(mbedtls tf-psa-crypto)
    [[ "$scope" != all ]] || checked+=(wolfssl wolfpsa)
    for module in "${checked[@]}"; do
        require_module "$module" "$modules/$module"
        pin="$(pinned_revision "$module")"
        actual="$(git -C "$modules/$module" rev-parse HEAD)"
        [[ "$actual" == "$pin" ]] || fail "modules/crypto/$module is at $actual, and west.yml pins $pin"
        printf 'pin ok: %s %s\n' "$module" "$pin"
    done
}

# The result has the expected counts, and nothing failed. Reports the summary line.
check_host_result() { # file, unsupported, deviations
    local want="$case_count cases, 0 failed, $2 declared unsupported, $3 declared deviations"
    grep -qxF "$want" "$1" || fail "$1 does not end with: $want"
    printf '%s: %s\n' "$(basename "$1" .txt)" "$want"
}

host_configure() { # build name, then CMake options
    local name="$1"
    shift
    cmake -S "$host_source" -B "$out/$name" -G Ninja -DCMAKE_BUILD_TYPE=Debug \
        "-DFETCHCONTENT_BASE_DIR=$cache_dir/cmake" "$@"
    cmake --build "$out/$name" -j "$jobs"
}

wolf_options=("-DCRUX_PSA_PROVIDER=wolfpsa" "-DCRUX_PSA_WOLFPSA_DIR=$modules/wolfpsa"
    "-DCRUX_PSA_WOLFSSL_DIR=$modules/wolfssl" "-DCRUX_PSA_WOLFPSA_JOBS=$jobs")
sanitize_options=('-DCMAKE_C_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer'
    '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined')

# Linux Host random comes from the kernel, so two processes must differ. native_sim is the opposite.
check_host_random_differs() { # program, name
    local first second
    first="$("$1" | grep '^PSA_RANDOM_PROBE ')"
    second="$("$1" | grep '^PSA_RANDOM_PROBE ')"
    [[ -n "$first" && "$first" != "$second" ]] || fail "$2: two Host processes gave the same random bytes"
    printf 'host random differs between processes: %s\n' "$2"
}

# Without declarations a provider's declared gaps must show as failures, and a declaration on a passing
# case must fail. provider_declarations.cmake then only states what a run does.
check_declaration_controls() { # build name
    local build="$out/$1"
    if "$build/psa_conformance_stale" > "$out/$1-stale.txt"; then
        fail "$1: a stale declaration went unnoticed"
    fi
    grep -q '^FAIL init_is_idempotent: passes, but this composition declares otherwise' "$out/$1-stale.txt" ||
        fail "$1: a stale declaration was not reported"
    printf 'control ok: %s: a stale declaration fails\n' "$1"
}

check_undeclared() { # build name, expected number of failures, text every failure must contain
    local build="$out/$1" count
    if "$build/psa_conformance_undeclared" > "$out/$1-undeclared.txt"; then
        fail "$1: a gap passed without a declaration"
    fi
    count="$(grep -c '^FAIL' "$out/$1-undeclared.txt" || true)"
    [[ "$count" == "$2" ]] || fail "$1: expected $2 failures without declarations, got $count"
    if grep '^FAIL' "$out/$1-undeclared.txt" | grep -vF "$3"; then
        fail "$1: an unexpected failure without declarations"
    fi
    printf 'control ok: %s: without declarations, %s cases fail\n' "$1" "$2"
}

run_host() {
    local passed allowed
    mkdir -p "$out"

    # Control 1: with no provider selected the cases cannot be configured.
    if cmake -S "$host_source" -B "$out/none" -G Ninja > "$out/none.log" 2>&1; then
        fail 'configuring without a provider unexpectedly succeeded'
    fi
    grep -q 'CRUX_PSA_PROVIDER is not set' "$out/none.log"
    printf 'control ok: no provider selected fails to configure\n'

    # Control 2: a provider that supports nothing fails every case.
    host_configure stub-unsupported -DCRUX_PSA_PROVIDER=stub-unsupported > /dev/null
    if "$out/stub-unsupported/psa_conformance" > "$out/stub-unsupported.txt"; then
        fail 'the unsupported stub passed the cases'
    fi
    if grep -q '^PASS' "$out/stub-unsupported.txt"; then
        fail 'a case passed against the unsupported stub'
    fi
    printf 'control ok: unsupported stub fails every case\n'

    # Control 3: a provider that accepts everything may pass only the positive
    # cases whose expected status is success.
    host_configure stub-permissive -DCRUX_PSA_PROVIDER=stub-permissive > /dev/null
    if "$out/stub-permissive/psa_conformance" > "$out/stub-permissive.txt"; then
        fail 'the permissive stub passed the cases'
    fi
    passed="$({ grep '^PASS' "$out/stub-permissive.txt" || true; } | awk '{print $2}' | sort | tr '\n' ' ')"
    allowed='ecdsa_p256_verify_hash_known_answer init_is_idempotent '
    [[ "$passed" == "$allowed" ]] || fail "unexpected cases passed against the permissive stub: $passed"
    printf 'control ok: permissive stub fails every case that expects an error status\n'

    # Control 4: a declaration cannot hide a gap, and cannot go stale.
    check_declaration_controls stub-permissive

    # The archive the Host build fetched is the same source that west checks out.
    diff -r --exclude=.git "$cache_dir/cmake/tf_psa_crypto-src" "$modules/tf-psa-crypto" > "$out/archive-diff.txt" ||
        fail "the TF-PSA-Crypto archive differs from the west checkout, see $out/archive-diff.txt"
    printf 'archive ok: same files as the west checkout\n'

    # The real providers, without and with sanitizers.
    host_configure tf-psa-crypto -DCRUX_PSA_PROVIDER=tf-psa-crypto > /dev/null
    "$out/tf-psa-crypto/psa_conformance" > "$out/tf-psa-crypto.txt" || { cat "$out/tf-psa-crypto.txt"; fail 'TF-PSA-Crypto on Host'; }
    check_host_result "$out/tf-psa-crypto.txt" "$tf_unsupported" 0
    check_host_random_differs "$out/tf-psa-crypto/psa_conformance" tf-psa-crypto
    check_undeclared tf-psa-crypto "$tf_unsupported" 'does not declare that'

    host_configure tf-psa-crypto-sanitize -DCRUX_PSA_PROVIDER=tf-psa-crypto "${sanitize_options[@]}" > /dev/null
    UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 "$out/tf-psa-crypto-sanitize/psa_conformance" > "$out/tf-psa-crypto-sanitize.txt" || { cat "$out/tf-psa-crypto-sanitize.txt"; fail 'TF-PSA-Crypto on Host with sanitizers'; }
    check_host_result "$out/tf-psa-crypto-sanitize.txt" "$tf_unsupported" 0

    if [[ "$scope" != all ]]; then
        printf 'not run: wolfPSA on Host, because this run is limited to %s\n' "$scope"
        return
    fi
    host_configure wolfpsa "${wolf_options[@]}" > /dev/null
    "$out/wolfpsa/psa_conformance" > "$out/wolfpsa.txt" || { cat "$out/wolfpsa.txt"; fail 'wolfPSA on Host'; }
    check_host_result "$out/wolfpsa.txt" 0 "$wolf_deviations"
    check_host_random_differs "$out/wolfpsa/psa_conformance" wolfpsa
    check_undeclared wolfpsa "$wolf_deviations" 'psa_cases.c'

    host_configure wolfpsa-sanitize "${wolf_options[@]}" "${sanitize_options[@]}" > /dev/null
    # UBSan reports inside the wolfSSL sources are recorded and do not halt: see the qualification
    # record. AddressSanitizer still ends the run on an error.
    "$out/wolfpsa-sanitize/psa_conformance" > "$out/wolfpsa-sanitize.txt" 2> "$out/wolfpsa-sanitize-reports.txt" || { cat "$out/wolfpsa-sanitize.txt" "$out/wolfpsa-sanitize-reports.txt"; fail 'wolfPSA on Host with sanitizers'; }
    check_host_result "$out/wolfpsa-sanitize.txt" 0 "$wolf_deviations"
    if grep -E 'runtime error|ERROR: AddressSanitizer' "$out/wolfpsa-sanitize-reports.txt" | grep -v '/modules/crypto/wolfssl/'; then
        fail 'a sanitizer report outside the wolfSSL sources'
    fi
    grep -c 'runtime error' "$out/wolfpsa-sanitize-reports.txt" | sed 's/^/wolfSSL UBSan reports, recorded: /' || true
}

# native_sim entropy is declared test-only, and this keeps that declaration honest. The source warns
# that it is unsafe and gives the same bytes in every run, and a chosen seed only selects another fixed
# sequence. If this ever stops holding, the declaration needs a new look, so this fails.
check_native_sim_entropy() { # name, zephyr.exe
    local name="$1" exe="$2" log="$out/entropy-$1.txt" a b c d e f
    probe() { timeout 300 "$exe" "$@" 2>&1 | grep -E 'PSA_RANDOM_PROBE|not safe - entropy' || true; }
    probe > "$log.1"
    grep -q 'Using a test - not safe - entropy source' "$log.1" ||
        fail "$name: the native_sim entropy warning is gone. Review the test-only declaration."
    a="$(grep PSA_RANDOM_PROBE "$log.1")"
    b="$(probe | grep PSA_RANDOM_PROBE)"
    c="$(probe --seed=7 | grep PSA_RANDOM_PROBE)"
    d="$(probe --seed=7 | grep PSA_RANDOM_PROBE)"
    e="$(probe --seed-random | grep PSA_RANDOM_PROBE)"
    f="$(probe --seed-random | grep PSA_RANDOM_PROBE)"
    rm -f "$log.1"
    printf 'default\t%s\ndefault\t%s\nseed=7\t%s\nseed=7\t%s\nseed-random\t%s\nseed-random\t%s\n' \
        "$a" "$b" "$c" "$d" "$e" "$f" > "$log"
    cat "$log"
    [[ -n "$a" && "$a" == "$b" ]] ||
        fail "$name: two default runs gave different random bytes. Review the test-only declaration."
    [[ -n "$c" && "$c" == "$d" && "$c" != "$a" ]] ||
        fail "$name: a chosen seed does not select one fixed sequence. Review the test-only declaration."
    [[ -n "$e" && "$e" != "$f" ]] || printf 'note: %s: --seed-random repeated\n' "$name"
    printf 'entropy declaration holds: %s, native_sim random is test-only\n' "$name"
}

run_zephyr() {
    local name exe report="$out/twister" tag=psa-all expected=3 names=(tf_psa_crypto wolfpsa)
    if [[ "$scope" != all ]]; then
        tag=psa-tf-psa-crypto
        expected=1
        names=(tf_psa_crypto)
    fi
    require_module Zephyr "$workspace/zephyr"
    export ZEPHYR_BASE="$workspace/zephyr"
    export ZEPHYR_TOOLCHAIN_VARIANT=host
    mkdir -p "$out"
    (cd "$workspace" && west twister -j "$jobs" -p "$native_board" -T "$zephyr_source" \
        -O "$report" --inline-logs --tag "$tag")
    python3 - "$report/twister.json" "$expected" <<'PY'
import json, sys
suites = json.load(open(sys.argv[1]))["testsuites"]
names = sorted(s["name"] for s in suites)
if len(names) != int(sys.argv[2]) or any(s["status"] != "passed" for s in suites):
    sys.exit(f"expected {sys.argv[2]} passed configurations, got {[(s['name'], s['status']) for s in suites]}")
for s in suites:
    states = [t["status"] for t in s["testcases"]]
    print(s["name"], {k: states.count(k) for k in sorted(set(states))})
PY
    [[ "$scope" == all ]] || printf 'not run: wolfPSA on native_sim, because this run is limited to %s\n' "$scope"
    for name in "${names[@]}"; do
        exe="$(find "$report" -path "*conformance.$name/zephyr/zephyr.exe" -print -quit)"
        [[ -n "$exe" ]] || fail "no native_sim executable for $name"
        check_native_sim_entropy "$name" "$exe"
    done
}

# Compile only. Nothing here touches a board. The configuration and devicetree are the evidence for the
# RT1060-EVKB entropy source and for hardware acceleration being off.
run_target() {
    local provider build config providers=(tf-psa-crypto wolfpsa)
    [[ "$scope" == all ]] || providers=(tf-psa-crypto)
    require_module Zephyr "$workspace/zephyr"
    export ZEPHYR_BASE="$workspace/zephyr"
    unset ZEPHYR_TOOLCHAIN_VARIANT
    mkdir -p "$out"
    for provider in "${providers[@]}"; do
        build="$out/rt1060-$provider"
        (cd "$workspace" && west build -p always -b "$board" -d "$build" "$zephyr_source" -- \
            "-DEXTRA_CONF_FILE=$provider.conf")
        config="$build/zephyr/.config"
        grep -qx 'CONFIG_ENTROPY_MCUX_TRNG=y' "$config" || fail "$provider: the RT1060 entropy driver is not selected"
        ! grep -q '^CONFIG_FAKE_ENTROPY_NATIVE_SIM=y' "$config" || fail "$provider: a test entropy source on the target"
        ! grep -q '^CONFIG_CRYPTO_MCUX_DCP=y' "$config" || fail "$provider: DCP is on, so the unsupported declaration needs review"
        python3 - "$build/zephyr/zephyr.dts" <<'PY'
import re, sys
text = open(sys.argv[1]).read()
if not re.search(r'zephyr,entropy\s*=\s*&trng\s*;', text):
    sys.exit("zephyr,entropy is not &trng")
if not re.search(r'trng:\s*random@400cc000\s*\{[^}]*compatible\s*=\s*"nxp,kinetis-trng"', text):
    sys.exit("trng is not the nxp,kinetis-trng block at 0x400cc000")
PY
        printf '%s on %s, compile only: ENTROPY_MCUX_TRNG, zephyr,entropy = &trng at 0x400cc000, no DCP, nothing run\n' \
            "$provider" "$board"
        grep -E '^CONFIG_(ENTROPY_MCUX_TRNG|ENTROPY_NEEDS_PRNG|MBEDTLS_PSA_CRYPTO_LEGACY_RNG|MBEDTLS_PSA_CRYPTO_EXTERNAL_RNG|MBEDTLS_PSA_DRIVER_GET_ENTROPY|CSPRNG_ENABLED|XOSHIRO_RANDOM_GENERATOR|WOLFPSA|WOLFSSL_AES_TOUCH_LINES|MBEDTLS_PSA_STATIC_KEY_SLOTS|PSA_CRYPTO_PROVIDER_[A-Z]*)=' \
            "$config" | tee "$out/rt1060-$provider-config.txt"
    done
}

printf 'Crux revision: %s\n' "$(git -C "$repo" rev-parse HEAD)"
check_pins
case "$mode" in
    host) run_host ;;
    zephyr) run_zephyr ;;
    target) run_target ;;
    all)
        run_host
        run_zephyr
        ;;
esac
printf 'PSA Crypto qualification (%s) finished.\n' "$mode"
