#!/usr/bin/env bash
# Row C11 (docs/rt1060-isn-fail-closed.md): the TCP stack's initial sequence number when sys_csrand_get() fails, on
# native_sim, on the unpatched pinned Zephyr tree (the red run: it must FAIL) and on a copy with
# patches/zephyr/zephyr-isn-fail-closed.patch (the green run: it must PASS). It never flashes, resets or runs anything
# on a board.
#
#   scripts/test-isn-fail-closed.sh [unpatched|patched|all]
#
#   unpatched: ZEPHYR_BASE is the pinned checkout, read-only. The cases must fail, and the failures must be the
#              ones the patch is for (a SYN|ACK to a SYN with a failed read, a SYN from a failed connect, a sequence
#              number that is not made from the key even after the read works again).
#   patched:   a copy of the pinned tree, with the patch applied by scripts/prepare-isn-fail-closed.py. The cases must pass.
#   all:       both, one after another (default).
# It needs a Linux host or the Dev Container, Zephyr at its pin, and mbedtls and tf-psa-crypto at theirs (the optional
# west group), as scripts/test-wolf-entropy.sh does. CRUX_WORKSPACE names the workspace and CRUX_SSH_MODULES a folder that
# holds mbedtls and tf-psa-crypto when they are not under modules/crypto of the workspace. CRUX_ISN_OUTPUT is the output
# folder (default build/isn-fail-closed in the repository, and the copy of the tree is made inside it and removed at
# the end) and CRUX_ISN_JOBS the build's parallelism (default 2).
set -euo pipefail

mode="${1:-all}"
case "$mode" in unpatched | patched | all) ;; *)
    printf 'usage: %s [unpatched|patched|all]\n' "$0" >&2
    exit 2
    ;;
esac

repo="$(git rev-parse --show-toplevel)"
workspace="${CRUX_WORKSPACE:-$(dirname "$repo")}"
out="${CRUX_ISN_OUTPUT:-$repo/build/isn-fail-closed}"
jobs="${CRUX_ISN_JOBS:-2}"
crypto="${CRUX_SSH_MODULES:-$workspace/modules/crypto}"
source_dir="$repo/tests/native/isn_fail_closed"

fail() {
    printf 'FAIL: %s\n' "$*" >&2
    exit 1
}

[[ "$(uname -s)" == Linux ]] || { printf 'Run this script in the Linux Dev Container image.\n' >&2; exit 2; }

pinned_revision() { # project name in west.yml
    awk -v name="$1" '$1 == "-" && $2 == "name:" { in_project = ($3 == name) }
        in_project && $1 == "revision:" { print $2; exit }' "$repo/west.yml"
}

check_pins() {
    local module
    [[ "$(git -C "$workspace/zephyr" rev-parse HEAD)" == "$(pinned_revision zephyr)" ]] ||
        fail 'the Zephyr checkout is not at the pin of west.yml'
    for module in mbedtls tf-psa-crypto; do
        [[ -e "$crypto/$module/.git" ]] ||
            fail "$module is missing. Enable the optional stacks: west config manifest.group-filter -- +crux-secure-management && west update"
        [[ "$(git -C "$crypto/$module" rev-parse HEAD)" == "$(pinned_revision "$module")" ]] ||
            fail "$module is not at the pin of west.yml"
    done
}

build_and_run() { # variant ZEPHYR_BASE
    local variant="$1" base="$2" build="$out/$1" status=0
    rm -rf "$build"
    mkdir -p "$out"
    export ZEPHYR_TOOLCHAIN_VARIANT=host
    ZEPHYR_BASE="$base" cmake -S "$source_dir" -B "$build" -G Ninja -DBOARD=native_sim/native/64 \
        "-DZEPHYR_MODULES=$crypto/mbedtls;$crypto/tf-psa-crypto" "-DPython3_EXECUTABLE=$(command -v python3)" \
        > "$out/$variant-configure.log" 2>&1 || { tail -30 "$out/$variant-configure.log" >&2; fail "$variant: the configuration failed"; }
    ninja -C "$build" -j "$jobs" > "$out/$variant-build.log" 2>&1 ||
        { grep -E 'error|FAILED' "$out/$variant-build.log" | head -20 >&2; fail "$variant: the build failed"; }
    grep -E '^CONFIG_(NET_TCP_ISN_RFC6528|NET_TEST|NET_TEST_PROTOCOL|CSPRNG_[A-Z_]*|PSA_CSPRNG_GENERATOR|NET_IPV4|NET_IPV6)[= ]|^# CONFIG_(NET_TEST|NET_TEST_PROTOCOL) ' \
        "$build/zephyr/.config" > "$out/$variant-config.txt"
    grep -q '^CONFIG_NET_TCP_ISN_RFC6528=y' "$build/zephyr/.config" || fail "$variant: the RFC 6528 sequence number is not on"
    ! grep -q '^CONFIG_NET_TEST=y\|^CONFIG_NET_TEST_PROTOCOL=y' "$build/zephyr/.config" || fail "$variant: NET_TEST skips tcp_init_isn()"
    "$build/zephyr/zephyr.exe" > "$out/$variant-run.txt" 2>&1 || status=$?
    printf '%s' "$status" > "$out/$variant-exit.txt"
    sha256sum "$base/subsys/net/ip/tcp.c" > "$out/$variant-tcp-c-sha256.txt"
    grep -E 'ISN-REPORT|FAIL|PASS|PROJECT EXECUTION|Assertion|Expectation|expected' "$out/$variant-run.txt" || true
    return "$status"
}

unpatched_run() {
    local status=0
    printf '== unpatched pinned tree (the red run): %s\n' "$workspace/zephyr"
    build_and_run unpatched "$workspace/zephyr" || status=$?
    [[ "$status" -ne 0 ]] || fail 'unpatched: the cases PASSED on the pinned tree, so the test does not show the zero key'
    local log="$out/unpatched-run.txt"
    # The red run must fail for the reasons the patch is for, and not for another one. On the pinned tree the sequence number
    # is never made from a key: tcp.c asks psa_hash_compute() for a SHA-256 digest into a 16-byte buffer, which every PSA
    # provider refuses (PSA_ERROR_BUFFER_TOO_SMALL), and it ignores that result. The zero key of a failed read is then
    # latent: it is what a fix of the buffer alone would expose.
    grep -q 'ISN-REPORT phase=A-read-fails path=passive sent=SYN|ACK isn=unkeyed' "$log" ||
        fail 'unpatched: the SYN with a failed read was not answered with a SYN|ACK'
    grep -q 'ISN-REPORT phase=A-read-fails path=active sent=SYN isn=unkeyed' "$log" ||
        fail 'unpatched: the connect with a failed read did not send a SYN'
    grep -q 'ISN-REPORT phase=B-read-works path=passive sent=SYN|ACK isn=unkeyed' "$log" ||
        fail 'unpatched: the sequence number was made from the key although the read worked'
    printf 'red run as expected (exit %s): the pinned tree answers and connects with a failed read, and never keys the sequence number\n' "$status"
}

patched_run() {
    local copy="$out/zephyr-isn" status=0
    printf '== patched copy (the green run)\n'
    rm -rf "$copy"
    mkdir -p "$copy"
    git -C "$workspace/zephyr" archive HEAD ':!doc' ':!samples' ':!tests' | tar -x -C "$copy"
    python3 "$repo/scripts/prepare-isn-fail-closed.py" "$copy"
    build_and_run patched "$copy" || status=$?
    rm -rf "$copy"
    [[ "$status" -eq 0 ]] || fail "patched: the cases failed (exit $status), see $out/patched-run.txt"
    local log="$out/patched-run.txt"
    grep -q 'ISN-REPORT phase=A-read-fails path=passive sent=RST' "$log" || fail 'patched: no RST for a SYN with a failed read'
    grep -q 'ISN-REPORT phase=A-read-fails path=active sent=nothing isn=- connect=-5' "$log" ||
        fail 'patched: the connect with a failed read was not refused with -EIO and no SYN'
    grep -q 'ISN-REPORT phase=B-read-works path=passive sent=SYN|ACK isn=keyed' "$log" || fail 'patched: no keyed SYN|ACK after the read worked again'
    printf 'green run: the patched tree refuses and recovers\n'
}

check_pins
case "$mode" in unpatched | all) unpatched_run ;; esac
case "$mode" in patched | all) patched_run ;; esac
printf 'ISN fail-closed test (%s): native_sim only. No board was operated.\n' "$mode"
