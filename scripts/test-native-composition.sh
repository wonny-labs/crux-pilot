#!/usr/bin/env bash
# The management services on native_sim with real PSA providers (Issue #242), and the providers of their
# ports on their own. It operates no hardware: target builds an image and never flashes, resets or runs one.
#
#   scripts/test-native-composition.sh [host|mbed|wolf|target-mbed|target-wolf|all]
#
# host:   the PSA ports on TF-PSA-Crypto on Linux Host, under ASan and UBSan, with controls.
# mbed:   the ports and the composition on native_sim, on TF-PSA-Crypto and Mbed TLS.
# wolf:   the same on wolfPSA and wolfSSL, on a patched copy of Zephyr.
# target-*: the ports compiled for RT1060-EVKB, and the guard against a test random generator there.
# all:    host, mbed and wolf.
#
# mbed needs the mbedtls and tf-psa-crypto projects, so a default workflow can run it. wolf needs the
# crux-secure-management group, so a default workflow must not: see scripts/check-ssh-boundaries.py.
set -euo pipefail

mode="${1:-all}"
case "$mode" in host | mbed | wolf | target-mbed | target-wolf | all) ;; *)
    printf 'usage: %s [host|mbed|wolf|target-mbed|target-wolf|all]\n' "$0" >&2
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
    local control
    for control in 0 1; do
        if "$build/psa_port_cases_stub_$control" > "$out/host-ports-stub-$control.txt"; then
            fail "the ports passed on a stub provider, control $control"
        fi
        printf 'control ok: a stub provider, control %s, fails: %s\n' "$control" "$(tail -1 "$out/host-ports-stub-$control.txt")"
    done
}

run_ports() { # Twister tag
    (cd "$workspace" && west twister -j "$jobs" -p "$board" -T "$repo/tests/psa_ports/zephyr" \
        -O "$out/twister-ports-$1" --inline-logs --tag "$1")
}

run_composition() { # profile, ZEPHYR_BASE
    local profile="$1" build="$out/$1/build"
    mkdir -p "$out/$profile"
    (cd "$workspace" && ZEPHYR_BASE="$2" west build -p always -b "$board" -d "$build" -o="-j$jobs" "$composition" \
        -- "-DEXTRA_CONF_FILE=../tls_offload_probe/$profile.conf;profile-psa.conf") > "$out/$profile/build.log" 2>&1 ||
        { tail -40 "$out/$profile/build.log" >&2; fail "$profile: the build failed"; }
    python3 "$repo/scripts/check-composition-image.py" "$build/zephyr/zephyr.exe" --profile "$profile"
    python3 "$composition/run_composition.py" "$build/zephyr/zephyr.exe" --profile "$profile" --output "$out/$profile"
    grep -E '^CONFIG_(NET_NATIVE_OFFLOADED_SOCKETS|NET_SOCKETS_OFFLOAD|FAKE_ENTROPY_NATIVE_SIM|HWINFO_NATIVE|PSA_CRYPTO_PROVIDER_[A-Z]*|MBEDTLS|MBEDTLS_HEAP_SIZE|MBEDTLS_PSA_STATIC_KEY_SLOTS|WOLFSSL|WOLFPSA|WOLFPSA_THREAD_SAFE|CRUX_SYNC_MUTEX_COUNT|CRUX_FILE_FATFS_SIMULATOR)=' \
        "$build/zephyr/.config" > "$out/$profile/config.txt"
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
    grep -q 'A test random generator must not reach' "$out/target-$1-guard.log" ||
        fail "$1: the build failed for another reason: see $out/target-$1-guard.log"
    printf 'control ok: %s: a board cannot build with a test random generator\n' "$1"
    rm -rf "$build"
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
        run_composition mbed "$workspace/zephyr"
        ;;
    esac
    case "$mode" in wolf | all)
        run_ports psa-ports-wolfpsa
        "$repo/scripts/prepare-wolf-tls-zephyr.sh" "$out/zephyr-wolf"
        run_composition wolf "$out/zephyr-wolf"
        ;;
    esac
fi
printf 'Native composition (%s) finished.\n' "$mode"
