#!/usr/bin/env bash
# The Wolf profile's random source on native_sim, with an entropy device that fails (Row C5, docs/rt1060-wolf-entropy.md).
# It is native_sim only, and it never flashes, resets or runs anything on a board.
#
#   scripts/test-wolf-entropy.sh [build|run|all] [variant...]
#
# The variants are tests/psa_entropy/wolf's CRUX_WOLF_ENTROPY_VARIANT: runtime, init-fails, fallback (a control that
# must show wolfCrypt's sys_rand_get() fallback) and guard (a control whose build must be refused). No variant
# named means all four. Each is configured and built under its own folder, one after another.
#   build: configure and build each variant. The guard variant must FAIL to compile, with the boot check's reason.
#   run:   run each built image, and check its console for the fixed CRUX-ENTROPY line (tests/zephyr/entropy_boot).
#   all:   both.
# It needs a Linux host or the Dev Container, Zephyr at its pin, and wolfssl and wolfpsa at theirs from the optional
# west group, as scripts/test-ssh-integrated.sh does:
#   west config manifest.group-filter -- +crux-secure-management && west update
# No default workflow runs it, because wolfSSL is GPL and stays out of default CI: scripts/check-ssh-boundaries.py.
# CRUX_WORKSPACE names the workspace and CRUX_SSH_MODULES a folder that holds wolfssl and wolfpsa, when they are not
# under modules/crypto of the workspace. CRUX_WOLF_ENTROPY_OUTPUT is the output folder and
# CRUX_WOLF_ENTROPY_JOBS the build's parallelism (default 2: the machines that run this are small).
set -euo pipefail

mode="${1:-all}"
case "$mode" in build | run | all) ;; *)
    printf 'usage: %s [build|run|all] [runtime|init-fails|fallback|guard ...]\n' "$0" >&2
    exit 2
    ;;
esac
shift || true
variants=("$@")
[[ ${#variants[@]} -gt 0 ]] || variants=(runtime init-fails fallback guard)
for variant in "${variants[@]}"; do
    case "$variant" in runtime | init-fails | fallback | guard) ;; *)
        printf 'unknown variant: %s\n' "$variant" >&2
        exit 2
        ;;
    esac
done

repo="$(git rev-parse --show-toplevel)"
workspace="${CRUX_WORKSPACE:-$(dirname "$repo")}"
out="${CRUX_WOLF_ENTROPY_OUTPUT:-$repo/build/wolf-entropy}"
jobs="${CRUX_WOLF_ENTROPY_JOBS:-2}"
crypto="${CRUX_SSH_MODULES:-$workspace/modules/crypto}"
source_dir="$repo/tests/psa_entropy/wolf"

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
    local module actual
    [[ "$(git -C "$workspace/zephyr" rev-parse HEAD)" == "$(pinned_revision zephyr)" ]] ||
        fail 'the Zephyr checkout is not at the pin of west.yml'
    for module in wolfssl wolfpsa mbedtls tf-psa-crypto; do
        [[ -e "$crypto/$module/.git" ]] ||
            fail "$module is missing. Enable the optional stacks: west config manifest.group-filter -- +crux-secure-management && west update"
        # mbedtls and tf-psa-crypto are not built: the Kconfig of the PSA choice needs them to be found.
        actual="$(git -C "$crypto/$module" rev-parse HEAD)"
        [[ "$actual" == "$(pinned_revision "$module")" ]] || fail "$module is at $actual, and west.yml pins $(pinned_revision "$module")"
    done
}

configure_and_build() { # variant
    local variant="$1" build="$out/$variant"
    rm -rf "$build"
    mkdir -p "$out"
    export ZEPHYR_TOOLCHAIN_VARIANT=host
    ZEPHYR_BASE="$workspace/zephyr" cmake -S "$source_dir" -B "$build" -G Ninja -DBOARD=native_sim/native/64 \
        "-DZEPHYR_MODULES=$crypto/wolfssl;$crypto/wolfpsa;$crypto/mbedtls;$crypto/tf-psa-crypto" "-DCRUX_WOLF_ENTROPY_VARIANT=$variant" \
        "-DPython3_EXECUTABLE=$(command -v python3)" > "$out/$variant-configure.log" 2>&1 ||
        { tail -30 "$out/$variant-configure.log" >&2; fail "$variant: the configuration failed"; }
    if ninja -C "$build" -j "$jobs" > "$out/$variant-build.log" 2>&1; then
        [[ "$variant" != guard ]] || fail 'guard: the boot check compiled in a composition where wolfCrypt would call sys_rand_get()'
    else
        if [[ "$variant" == guard ]]; then
            grep -q 'wolfCrypt would seed from sys_rand_get' "$out/$variant-build.log" ||
                { grep -E 'error|FAILED' "$out/$variant-build.log" | head -10 >&2
                  fail 'guard: the build failed for another reason'; }
            printf 'control ok: guard: the boot check refuses to compile without a chosen entropy device\n'
            return 0
        fi
        grep -E 'error|FAILED' "$out/$variant-build.log" | head -20 >&2
        fail "$variant: the build failed"
    fi
    # The properties that the cases rely on, from the resolved configuration.
    local config="$build/zephyr/.config"
    grep -q '^CONFIG_WOLFPSA=y' "$config" || fail "$variant: not the Wolf profile"
    ! grep -q '^CONFIG_MBEDTLS=y' "$config" || fail "$variant: the Mbed profile is selected"
    grep -E '^(# )?CONFIG_(WOLFSSL|WOLFPSA|ENTROPY_HAS_DRIVER|ENTROPY_NEEDS_PRNG|ENTROPY_GENERATOR|XOSHIRO_RANDOM_GENERATOR|TEST_RANDOM_GENERATOR|FAKE_ENTROPY_NATIVE_SIM|CRUX_TEST_ENTROPY[A-Z_]*)[= ]' \
        "$config" > "$out/$variant-config.txt"
    printf 'built: %s\n' "$build/zephyr/zephyr.exe"
}

line_pattern='^CRUX-ENTROPY first-read=(ok|failed) source=[^ ]+ compatible=[^ ]+ bytes=32 status=-?[0-9]+ device-ready=[01]$'

run_variant() { # variant
    local variant="$1" exe="$out/$1/zephyr/zephyr.exe" log="$out/$1-run.txt" status=0
    [[ "$variant" != guard ]] || return 0
    [[ -x "$exe" ]] || fail "no image at $exe: run the build step first"
    "$exe" > "$log" 2>&1 || status=$?
    tail -25 "$log"
    local lines; lines="$(grep -c '^CRUX-ENTROPY' "$log" || true)"
    local bad; bad="$(grep '^CRUX-ENTROPY' "$log" | grep -cvE "$line_pattern" || true)"
    [[ "$status" -eq 0 ]] || fail "$variant: the cases failed (exit $status), see $log"
    [[ "$bad" -eq 0 ]] || fail "$variant: a CRUX-ENTROPY line is not in the fixed format"
    case "$variant" in
    runtime)
        grep -q '^CRUX-ENTROPY first-read=ok ' "$log" && grep -q '^CRUX-ENTROPY first-read=failed ' "$log" ||
            fail 'runtime: the console lacks the ok line or the failed line'
        ;;
    init-fails)
        grep -q '^CRUX-ENTROPY first-read=failed .* device-ready=0$' "$log" || fail 'init-fails: no failed line with device-ready=0'
        ;;
    fallback)
        [[ "$lines" -eq 0 ]] || fail 'fallback: a boot line was printed by an image without the boot check'
        ;;
    esac
    printf 'run ok: %s (%s CRUX-ENTROPY lines)\n' "$variant" "$lines"
}

check_pins
for variant in "${variants[@]}"; do
    case "$mode" in build | all) configure_and_build "$variant" ;; esac
    case "$mode" in run | all) run_variant "$variant" ;; esac
done
printf 'Wolf entropy test (%s: %s): native_sim only. No board was operated.\n' "$mode" "${variants[*]}"
