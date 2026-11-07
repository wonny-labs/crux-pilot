#!/usr/bin/env bash
# One native_sim image with HTTPS web management, the command line over SSH and SFTP (Issue #197), driven from
# outside by OpenSSH and curl. It is native_sim only, and it never flashes, resets or runs anything on a board.
#
#   scripts/test-ssh-integrated.sh [guards|build|run|all]
#
# guards: the guards that refuse the test host key outside a declared native_sim test image, in CMake and in C.
#         They need cmake and cc only, so they run on any host.
# build:  the image, tests/native/management_ssh, on the Wolf profile: a patched copy of Zephyr for the TLS socket
#         backend (scripts/prepare-wolf-tls-zephyr.sh), then wolfSSL, wolfSSH and wolfPSA at their pins. Then the
#         configuration controls: the Mbed profile must stop, for its reason.
# run:    tests/native/management_ssh/driver.py against the built image: ssh, sftp and curl, and the listeners
#         together. It needs the OpenSSH client tools, curl and OpenSSL.
# all:    guards, build and run.
#
# It needs a workspace with the optional stacks, as scripts/test-ssh-provider.sh does:
#   west config manifest.group-filter -- +crux-secure-management && west update
# No default workflow runs it, because wolfSSH stays out of default CI: scripts/check-ssh-boundaries.py.
# CRUX_WORKSPACE names the workspace, and CRUX_SSH_MODULES a folder that holds wolfssl, wolfssh, wolfpsa and
# mbedtls, when they are not under modules/crypto of the workspace.
set -euo pipefail

mode="${1:-all}"
case "$mode" in guards | build | run | all) ;; *)
    printf 'usage: %s [guards|build|run|all]\n' "$0" >&2
    exit 2
    ;;
esac

repo="$(git rev-parse --show-toplevel)"
workspace="${CRUX_WORKSPACE:-$(dirname "$repo")}"
out="${CRUX_SSH_INTEGRATED_OUTPUT:-$repo/build/ssh-integrated}"
jobs="${CRUX_SSH_INTEGRATED_JOBS:-4}"
crypto="${CRUX_SSH_MODULES:-$workspace/modules/crypto}"
composition="$repo/tests/native/management_composition"
image_dir="$repo/tests/native/management_ssh"
build="$out/build"

fail() {
    printf 'FAIL: %s\n' "$*" >&2
    exit 1
}

run_guards() {
    python3 "$composition/check_ssh_guard.py"
}

need_linux() {
    [[ "$(uname -s)" == Linux ]] || { printf 'Run this script in the Linux Dev Container image.\n' >&2; exit 2; }
}

run_build() {
    need_linux
    local module
    for module in wolfssl wolfssh wolfpsa mbedtls; do
        [[ -e "$crypto/$module/.git" ]] ||
            fail "$module is missing. Enable the optional stacks: west config manifest.group-filter -- +crux-secure-management && west update"
    done
    [[ -e "$workspace/modules/fs/fatfs/.git" ]] || fail 'the fatfs module is missing: west update fatfs'
    mkdir -p "$out"
    export ZEPHYR_TOOLCHAIN_VARIANT=host
    "$repo/scripts/prepare-wolf-tls-zephyr.sh" "$out/zephyr-wolf"
    local modules="$crypto/wolfssl;$crypto/wolfssh;$crypto/wolfpsa;$crypto/mbedtls;$workspace/modules/fs/fatfs"
    ZEPHYR_BASE="$out/zephyr-wolf" cmake -S "$image_dir" -B "$build" -G Ninja -DBOARD=native_sim/native/64 \
        "-DZEPHYR_MODULES=$modules" "-DPython3_EXECUTABLE=$(command -v python3)" > "$out/configure.log" 2>&1 ||
        { tail -40 "$out/configure.log" >&2; fail 'the configuration failed'; }
    ninja -C "$build" -j "$jobs" > "$out/build.log" 2>&1 ||
        { grep -E 'error|FAILED' "$out/build.log" | head -20 >&2; fail 'the build failed'; }
    grep -E '^CONFIG_(CRUX_SSH[A-Z_]*|WOLFSSH|WOLFSSL|WOLFPSA|MBEDTLS|CRUX_WEB|CRUX_WEB_PORT|COMMON_LIBC_MALLOC_ARENA_SIZE)=' \
        "$build/zephyr/.config" > "$out/config.txt"
    ! grep -q '^CONFIG_MBEDTLS=y' "$build/zephyr/.config" || fail 'the image is on the Mbed profile'
    size "$build/zephyr/zephyr.elf" | tee "$out/size.txt"
    printf 'built: %s\n' "$build/zephyr/zephyr.exe"
    run_controls "$modules"
}

# The Mbed profile has no SSH provider, so a build with it must stop at configuration, for that reason, and an
# image that does not select CONFIG_CRUX_SSH must stop too, rather than build with no SSH.
run_controls() { # module list
    local mbed="$out/control-mbed.conf" log="$out/control-mbed.log"
    printf 'CONFIG_MBEDTLS=y\n' > "$mbed"
    if ZEPHYR_BASE="$out/zephyr-wolf" cmake -S "$image_dir" -B "$out/control-mbed" -G Ninja -DBOARD=native_sim/native/64 \
        "-DZEPHYR_MODULES=$1" "-DCRUX_SSH_TEST_EXTRA_CONF=$mbed" "-DPython3_EXECUTABLE=$(command -v python3)" > "$log" 2>&1; then
        fail 'Control: an image on the Mbed profile configured'
    fi
    tr '\n' ' ' < "$log" | tr -s ' ' | grep -q 'need the Wolf profile' ||
        fail "Control: the Mbed image stopped for another reason: see $log"
    printf 'control ok: CONFIG_MBEDTLS=y stops the image at configuration, for its reason\n'
    printf 'CONFIG_CRUX_SSH=n\n' > "$mbed"
    if ZEPHYR_BASE="$out/zephyr-wolf" cmake -S "$image_dir" -B "$out/control-mbed" -G Ninja -DBOARD=native_sim/native/64 \
        "-DZEPHYR_MODULES=$1" "-DCRUX_SSH_TEST_EXTRA_CONF=$mbed" "-DPython3_EXECUTABLE=$(command -v python3)" > "$log" 2>&1; then
        fail 'Control: an image without CONFIG_CRUX_SSH configured'
    fi
    tr '\n' ' ' < "$log" | tr -s ' ' | grep -q 'CONFIG_CRUX_SSH is not selected' ||
        fail "Control: the image without SSH stopped for another reason: see $log"
    printf 'control ok: an image that does not select CONFIG_CRUX_SSH stops at configuration\n'
    rm -rf "$out/control-mbed"
}

run_driver() {
    need_linux
    local executable="$build/zephyr/zephyr.exe"
    [[ -x "$executable" ]] || fail "no image at $executable: run the build step first"
    python3 -I "$image_dir/driver.py" "$executable" --output "$out"
}

case "$mode" in guards | all) run_guards ;; esac
case "$mode" in build | all) run_build ;; esac
case "$mode" in run | all) run_driver ;; esac
printf 'SSH integrated image (%s): native_sim only. No board was operated.\n' "$mode"
