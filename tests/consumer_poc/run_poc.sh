#!/usr/bin/env bash
# Copies this consumer outside the Crux tree and runs each software test level through
# supported entry points. Never flashes, resets or opens a device. See README.md.
#
#   run_poc.sh <new-output-dir>
#
# Run in a west workspace with ZEPHYR_BASE set. Set POC_REQUIRE_EXTRA_MODULE=1 where the
# manifest does not list Crux, to also prove that a build without the variable fails.
set -uo pipefail

fixture="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
crux="$(cd "$fixture/../.." && pwd)"
out="${1:?usage: run_poc.sh <new-output-dir>}"
board=native_sim/native/64
: "${ZEPHYR_BASE:=${CRUX_WORKSPACE:-$(dirname "$crux")}/zephyr}"
export ZEPHYR_BASE
failures=0

mkdir "$out" || exit 2
out="$(cd "$out" && pwd)"
case "$out/" in "$crux"/*) echo "output must be outside the Crux tree" >&2; exit 2 ;; esac
consumer="$out/consumer"
evidence="$out/evidence"
mkdir "$evidence"
cp -a "$fixture" "$consumer"

step() { # step <name> <expected exit code> <command...>
    local name="$1" expected="$2" started code
    shift 2
    started=$(date +%s)
    "$@" >"$evidence/$name.log" 2>&1
    code=$?
    if [[ "$code" == "$expected" ]]; then
        printf 'ok    %-22s exit=%s expected=%s %ss\n' "$name" "$code" "$expected" "$(($(date +%s) - started))"
    else
        printf 'FAIL  %-22s exit=%s expected=%s %ss\n' "$name" "$code" "$expected" "$(($(date +%s) - started))"
        tail -25 "$evidence/$name.log"
        failures=$((failures + 1))
    fi
}

host_levels() {
    cmake -S "$consumer" -B "$out/host" -G Ninja "-DCRUX_SOURCE_DIR=$crux" \
        -DFETCHCONTENT_FULLY_DISCONNECTED=ON &&
        cmake --build "$out/host" &&
        ctest --test-dir "$out/host" --output-on-failure -V \
            --output-junit "$evidence/ctest-junit.xml"
}

zephyr_image() { # zephyr_image <build-dir> [cmake arguments...]
    local build="$1"
    shift
    (cd "$(dirname "$ZEPHYR_BASE")" && west build -p always -b "$board" -d "$build" \
        "$consumer/zephyr" -- "-DEXTRA_ZEPHYR_MODULES=$crux" "$@")
}

twister_level() {
    (cd "$(dirname "$ZEPHYR_BASE")" && west twister -j 2 -p "$board" -T "$consumer/zephyr_test" \
        -O "$out/twister" --inline-logs "--extra-args=EXTRA_ZEPHYR_MODULES=$crux")
}

functional() { # functional <build-dir> <report-name>
    (cd "$consumer/functional" &&
        PYTHONPATH="$ZEPHYR_BASE/scripts/pylib/pytest-twister-harness/src" python3 -m pytest -v \
            -p no:cacheprovider -p twister_harness.plugin --twister-harness --device-type=native \
            "--build-dir=$1" "--junitxml=$evidence/$2.junit.xml" test_functional.py)
}

# The controlled defect must fail on the oracle's own assertion, not on a build,
# fixture or timeout error, or the expected exit code proves nothing.
defect_found_by_oracle() {
    grep -q 'errors="0" failures="1"' "$evidence/functional-defect.junit.xml" &&
        grep -q 'cycle 3: skipped 0, expected 2' "$evidence/functional-defect.junit.xml"
}

# Here nothing else can supply Crux, so this shows the variable is not redundant.
module_is_required() {
    (cd "$(dirname "$ZEPHYR_BASE")" && ! west build -p always -b "$board" -d "$out/no-module" \
        "$consumer/zephyr" --cmake-only)
}

{
    printf 'crux=%s\nzephyr_base=%s\n' "$crux" "$ZEPHYR_BASE"
    (cd "$(dirname "$ZEPHYR_BASE")" && west list -f '{name} {path} {revision}')
    python3 --version
    python3 -m pytest --version
    cmake --version | head -1
    west --version
    gcc --version | head -1
    "${CC:-cc}" --version | head -1
    git -C "$ZEPHYR_BASE" rev-parse HEAD
} >"$evidence/environment.txt" 2>&1

step host-unit-integration 0 host_levels
step twister-native-sim 0 twister_level
step image-normal 0 zephyr_image "$out/app"
step functional-normal 0 functional "$out/app" functional-normal
step image-defect 0 zephyr_image "$out/app-defect" -DCONSUMER_DEFECT_REBASE=ON
step functional-defect 1 functional "$out/app-defect" functional-defect
step defect-found-by-oracle 0 defect_found_by_oracle
if [[ -n "${POC_REQUIRE_EXTRA_MODULE:-}" ]]; then
    step module-is-required 0 module_is_required
fi

cp "$out/twister/twister.json" "$evidence/" 2>/dev/null
cp "$out/app/consumer-identity.json" "$evidence/identity-normal.json" 2>/dev/null
cp "$out/app-defect/consumer-identity.json" "$evidence/identity-defect.json" 2>/dev/null
cp "$out/app/handler.log" "$evidence/handler-normal.log" 2>/dev/null
cp "$out/app/zephyr_modules.txt" "$evidence/" 2>/dev/null

printf 'failures=%s\n' "$failures"
exit "$((failures != 0))"
