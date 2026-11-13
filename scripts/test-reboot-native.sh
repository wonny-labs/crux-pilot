#!/usr/bin/env bash
# Stage A of the reboot pilot (ADR-0002 P9): the existing reboot suites of tests/reboot, run on
# native_sim/native/64 over the Crux Thread and Sync Zephyr providers, with AddressSanitizer and
# UndefinedBehaviorSanitizer on. The sources of the service and of the suites are the Host ones.
#
#   scripts/test-reboot-native.sh [--plain]
#
# --plain builds without the sanitizers, for a timing comparison only.
#
# A reboot case runs in a forked child on the Host, because the service has no stop. Zephyr has no
# fork(), so each case runs in its own process of the image: this script asks the image for the cases
# of a suite, then starts it once for each. The number of cases must equal the number that the Host
# source of the suite runs, so that a case cannot be left out here without this script failing.
#
# native_sim runs one Zephyr thread at a time, so a data race cannot interleave in it, and it has no
# ThreadSanitizer. ASan and UBSan find memory and undefined-behaviour defects. This is not a Host result
# and not a physical-board result.
set -euo pipefail

if [[ "$(uname -s)" != Linux ]]; then
    printf 'Zephyr tests are configured for Linux only; reopen this repository in the Dev Container.\n' >&2
    exit 2
fi

mode="${1:-}"
if [[ -n "$mode" && "$mode" != --plain ]]; then
    printf 'Usage: %s [--plain]\n' "$0" >&2
    exit 2
fi

repo="$(git rev-parse --show-toplevel)"
workspace="${CRUX_WORKSPACE:-$(dirname "$repo")}"
if [[ ! -f "$workspace/.west/config" || ! -d "$workspace/zephyr" ]]; then
    printf 'Run scripts/bootstrap-zephyr.sh before Zephyr tests.\n' >&2
    exit 2
fi
export ZEPHYR_BASE="$workspace/zephyr"
mapfile -t dependencies < <(cd "$workspace" && west list cmsis cmsis_6 hal_nxp -f '{abspath}')
if [[ ${#dependencies[@]} != 3 ]]; then
    printf 'Expected three pinned west dependencies.\n' >&2
    exit 1
fi
dependencies+=("$repo")
export ZEPHYR_MODULES
ZEPHYR_MODULES="$(IFS=';'; printf '%s' "${dependencies[*]}")"
unset ZEPHYR_EXTRA_MODULES EXTRA_ZEPHYR_MODULES

variant=sanitized
extra_conf=(-DEXTRA_CONF_FILE=sanitize.conf)
if [[ "$mode" == --plain ]]; then
    variant=plain
    extra_conf=()
fi
out="${CRUX_REBOOT_NATIVE_OUT:-$repo/build/zephyr/reboot-native}/$variant"
mkdir -p "$(dirname "$out")"
# What Twister sets for a native_sim run: UBSan must stop the run, or it reports and carries on. The
# leak check at exit is off by default for 64-bit GCC builds, and stays so.
export ASAN_OPTIONS=detect_leaks=0
export UBSAN_OPTIONS=log_path=stdout:halt_on_error=1

# Image, then the suites that it holds. The suite names are those of the tests/reboot sources.
images=(core:service,steps,concurrency,reason lock_faults:lock_faults pending:pending sleep_provider:)
total=0
failed=0
started=$SECONDS
for entry in "${images[@]}"; do
    image="${entry%%:*}"
    suites="${entry#*:}"
    build_started=$SECONDS
    (cd "$workspace" && west build -b native_sim/native/64 -d "$out/$image" \
        "$repo/tests/reboot/zephyr" -- -DREBOOT_IMAGE="$image" "${extra_conf[@]}") >"$out-$image.build.log" 2>&1 || {
        cat "$out-$image.build.log" >&2
        printf 'Reboot native: the %s image did not build.\n' "$image" >&2
        exit 1
    }
    printf 'image %s built in %d s\n' "$image" $((SECONDS - build_started))
    exe="$out/$image/zephyr/zephyr.exe"
    if [[ "$image" == sleep_provider ]]; then
        # One case, and no service state to isolate.
        total=$((total + 1))
        if ! "$exe" >"$out-$image.log" 2>&1 || ! grep -q 'PROJECT EXECUTION SUCCESSFUL' "$out-$image.log"; then
            failed=$((failed + 1))
            cat "$out-$image.log"
        fi
        continue
    fi
    IFS=, read -ra suite_list <<<"$suites"
    for suite in "${suite_list[@]}"; do
        filter="reboot_${suite}::test_run"
        listed="$("$exe" -test="$filter" -reboot-list 2>&1 | sed -n 's/^SUITE .* CASES \([0-9]*\)$/\1/p')"
        host_cases="$(grep -cE '(reboot|net)_run_case\(' "$repo/tests/reboot/$suite.c")"
        if [[ -z "$listed" || "$listed" != "$host_cases" ]]; then
            printf 'Reboot native: suite %s lists %s cases, the Host source runs %s.\n' \
                "$suite" "${listed:-none}" "$host_cases" >&2
            exit 1
        fi
        for ((index = 0; index < listed; ++index)); do
            total=$((total + 1))
            if ! "$exe" -test="$filter" -reboot-case="$index" >"$out-case.log" 2>&1 ||
               ! grep -q 'PROJECT EXECUTION SUCCESSFUL' "$out-case.log"; then
                failed=$((failed + 1))
                printf 'FAILED: %s case %d\n' "$suite" "$index"
                cat "$out-case.log"
            fi
        done
        printf 'suite %s: %d cases\n' "$suite" "$listed"
    done
done
printf 'Reboot native (%s, native_sim/native/64): %d cases, %d failed, %d s.\n' \
    "$variant" "$total" "$failed" $((SECONDS - started))
[[ "$failed" == 0 ]]
