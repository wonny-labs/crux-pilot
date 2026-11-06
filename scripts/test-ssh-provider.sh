#!/usr/bin/env bash
# The wolfSSH provider (Issue #192), on native_sim against a real OpenSSH client and with the conformance
# cases of tests/ssh. --target compiles for RT1060-EVKB, and never flashes, resets or runs an image. It
# needs a workspace that enabled the optional stacks:
#   west config manifest.group-filter -- +crux-secure-management && west update
# No default workflow runs this. See docs/ssh-wolf-provider.md.
set -euo pipefail

usage() {
    printf 'Usage: %s [--native] [--measure] [--target] [--controls]\nWith no option, runs all of them.\n' "$0" >&2
    exit 2
}

run_native=false
run_measure=false
run_target=false
run_controls=false
for option in "$@"; do
    case "$option" in
        --native) run_native=true ;;
        --measure) run_measure=true ;;
        --target) run_target=true ;;
        --controls) run_controls=true ;;
        *) usage ;;
    esac
done
if [[ $# -eq 0 ]]; then
    run_native=true
    run_measure=true
    run_target=true
    run_controls=true
fi

if [[ "$(uname -s)" != Linux ]]; then
    printf 'Zephyr builds are configured for Linux only; reopen this repository in the Dev Container.\n' >&2
    exit 2
fi

repo="$(git rev-parse --show-toplevel)"
workspace="${CRUX_WORKSPACE:-$(dirname "$repo")}"
output="${CRUX_SSH_PROVIDER_OUTPUT:-$repo/build/zephyr/ssh-provider}"
jobs="${CRUX_SSH_PROVIDER_JOBS:-4}"
app="$repo/tests/zephyr/ssh_provider"
native_board='native_sim/native/64'
target_board='mimxrt1060_evk@B//qspi'
export ZEPHYR_BASE="$workspace/zephyr"

fail() {
    printf '%s\n' "$*" >&2
    exit 1
}

for module in wolfssl wolfssh; do
    [[ -e "$workspace/modules/crypto/$module/.git" ]] ||
        fail "$module is missing. Enable the optional stacks: west config manifest.group-filter -- +crux-secure-management && west update"
done
mkdir -p "$output"

build() { # directory, board, extra arguments after --
    local directory="$1" board="$2"
    shift 2
    west build -p auto -b "$board" -d "$directory" "-o=-j$jobs" "$app" -- "$@"
}

memory_report() { # log of a build
    grep -E '^ +(FLASH|RAM|SRAM|DTCM|ITCM|OCRAM|SDRAM):' "$1" || true
}

if [[ "$run_native" == true ]]; then
    build "$output/native" "$native_board" -DEXTRA_CONF_FILE=native_sim.conf
    "$output/native/zephyr/zephyr.exe" -testargs conformance | tee "$output/conformance.log"
    grep -qE '^[1-9][0-9]* cases, 0 failed$' "$output/conformance.log" ||
        fail 'The conformance cases did not all pass.'
    python3 -u "$app/openssh_test.py" "$output/native/zephyr/zephyr.exe" | tee "$output/openssh.log"
    tail -n 1 "$output/openssh.log" | grep -q '^All cases passed\.$' ||
        fail 'The OpenSSH cases did not all pass.'
    # The host key from the credential store on its PSA ports over wolfPSA, across restarts on one disk.
    [[ -e "$workspace/modules/crypto/wolfpsa/.git" && -e "$workspace/modules/crypto/mbedtls/.git" ]] ||
        fail 'The stored host key image needs wolfpsa, and mbedtls for the PSA options. Run: west update wolfpsa mbedtls'
    build "$output/stored" "$native_board" -DEXTRA_CONF_FILE=native_sim.conf -DCRUX_SSH_TEST_STORED_HOST_KEY=ON
    python3 -u "$app/openssh_test.py" "$output/stored/zephyr/zephyr.exe" --stored-host-key |
        tee "$output/stored-host-key.log"
    tail -n 1 "$output/stored-host-key.log" | grep -q '^All cases passed\.$' ||
        fail 'The stored host key cases did not all pass.'
fi

if [[ "$run_measure" == true ]]; then
    # Simulation results only: a 64-bit image, with a host stack under each thread.
    for window in 8192 16384 32768 131072; do
        build "$output/measure" "$native_board" -DEXTRA_CONF_FILE=native_sim.conf \
            "-DCRUX_SSH_TEST_WINDOW_BYTES=$window" -DCONFIG_COMMON_LIBC_MALLOC_ARENA_SIZE=1048576
        printf '%s\n' "--- channel window $window bytes"
        python3 -u "$app/openssh_test.py" "$output/measure/zephyr/zephyr.exe" --only resource_use
    done
    # An arena that three clients before keys can fill must refuse a login and recover.
    build "$output/measure" "$native_board" -DEXTRA_CONF_FILE=native_sim.conf \
        -DCRUX_SSH_TEST_WINDOW_BYTES=16384 -DCONFIG_COMMON_LIBC_MALLOC_ARENA_SIZE=151552
    python3 -u "$app/openssh_test.py" "$output/measure/zephyr/zephyr.exe" --small-arena
    rm -rf "$output/measure"
    # The cost of ML-KEM in the image. OpenSSH before 9.9 cannot choose it: see the design document.
    build "$output/mlkem-native" "$native_board" "-DEXTRA_CONF_FILE=native_sim.conf;mlkem.conf" \
        -DCRUX_SSH_TEST_OFFER_MLKEM=ON
    printf '%s\n' '--- mlkem768x25519-sha256 offered'
    python3 -u "$app/openssh_test.py" "$output/mlkem-native/zephyr/zephyr.exe" --only resource_use
fi

if [[ "$run_target" == true && "$run_measure" == true ]]; then
    build "$output/mlkem-target" "$target_board" "-DEXTRA_CONF_FILE=rt1060.conf;mlkem.conf" \
        -DCRUX_SSH_TEST_OFFER_MLKEM=ON 2>&1 | tee "$output/mlkem-target.log"
    memory_report "$output/mlkem-target.log"
    rm -rf "$output/mlkem-target"
fi

if [[ "$run_target" == true ]]; then
    # Compile only, with no TLS socket backend, so that no patch of Zephyr is needed.
    build "$output/target" "$target_board" -DEXTRA_CONF_FILE=rt1060.conf 2>&1 |
        tee "$output/target.log"
    memory_report "$output/target.log"
fi

if [[ "$run_controls" == true ]]; then
    # The Mbed profile must fail at configuration, for the stated reason. Without its module the
    # option is unknown, and the build would stop for that reason instead.
    [[ -e "$workspace/modules/crypto/mbedtls/.git" ]] ||
        fail 'The control needs the mbedtls module of the secure management group. Run: west update mbedtls tf-psa-crypto'
    printf '%s\n' 'CONFIG_MBEDTLS=y' > "$output/mbed.conf"
    if build "$output/control-mbed" "$native_board" \
        "-DEXTRA_CONF_FILE=native_sim.conf;$output/mbed.conf" >"$output/control-mbed.log" 2>&1; then
        fail 'Control: a build on the Mbed profile configured.'
    fi
    grep -q 'CLI and SFTP need the Wolf profile' "$output/control-mbed.log" ||
        fail 'Control: the Mbed build failed for another reason. See the log.'
    printf '%s\n' 'Control: a build on the Mbed profile fails at configuration, as it should.'
    rm -rf "$output/control-mbed"
fi

printf '%s\n' 'SSH provider checks finished. native_sim and compile results only. No board was operated.'
