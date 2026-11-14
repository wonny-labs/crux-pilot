#!/usr/bin/env bash
# ADR-0004 phase 0 (Experimental): the Zephyr module builds the management service cores under
# CONFIG_CRUX_ACCESS. No hardware: it builds and runs native_sim images and never flashes or resets.
#
#   scripts/test-management-module.sh
#
# 1. The static checks of tests/build/test_management_module.py.
# 2. tests/native/management_module, on the Mbed profile: an application that names CONFIG_CRUX_ACCESS and
#    compiles none of Crux's service sources into its own target. It builds, its three cases pass, the
#    configuration shows Thread, Sync and Time selected by the service symbol, and the three module
#    libraries are in the link.
# 3. The refusals: each missing piece (its locks, Crux Logging, hwinfo, a File provider) stops the configuration with
#    the module's message, so a misconfiguration never reaches the compiler.
# 4. Default off: an application of the module that does not set the symbol links no Access symbol.
#
# The Mbed profile needs the mbedtls and tf-psa-crypto projects, so a workspace that enables them is
# required, as for scripts/test-native-composition.sh mbed. native_sim needs a Linux host.
set -euo pipefail

if [[ "$(uname -s)" != Linux ]]; then
    printf 'Run this script in the Linux Dev Container image.\n' >&2
    exit 2
fi

repo="$(git rev-parse --show-toplevel)"
workspace="${CRUX_WORKSPACE:-$(dirname "$repo")}"
out="${CRUX_MODULE_OUTPUT:-$repo/build/management-module}"
jobs="${CRUX_MODULE_JOBS:-4}"
board=native_sim/native/64
fixture="$repo/tests/native/management_module"
profile="-DEXTRA_CONF_FILE=../tls_offload_probe/mbed.conf;../management_composition/profile-psa.conf;../management_composition/mbed-threading.conf"
export ZEPHYR_TOOLCHAIN_VARIANT="${ZEPHYR_TOOLCHAIN_VARIANT:-host}"

fail() {
    printf 'FAIL: %s\n' "$*" >&2
    exit 1
}

mkdir -p "$out"
python3 -m unittest discover -s "$repo/tests/build" -p test_management_module.py ||
    fail 'the static checks of the module integration'

build="$out/fixture"
(cd "$workspace" && west build -p always -b "$board" -d "$build" -o="-j$jobs" "$fixture" -- "$profile") \
    > "$out/fixture-build.log" 2>&1 || { tail -40 "$out/fixture-build.log" >&2; fail 'the fixture failed to build'; }
rm -f "$out/disk.bin"
"$build/zephyr/zephyr.exe" --flash="$out/disk.bin" --seed=7 > "$out/fixture-run.log" 2>&1 ||
    { tail -40 "$out/fixture-run.log" >&2; fail 'the fixture failed to run'; }
grep -q 'PROJECT EXECUTION SUCCESSFUL' "$out/fixture-run.log" || fail 'the fixture did not report success'
grep -q 'pass = 3, fail = 0' "$out/fixture-run.log" || fail 'the fixture did not pass its three cases'
config="$build/zephyr/.config"
for symbol in CRUX_ACCESS CRUX_CREDENTIAL CRUX_SECURITY_EVENT CRUX_THREAD CRUX_SYNC CRUX_TIME; do
    grep -qx "CONFIG_$symbol=y" "$config" || fail "CONFIG_$symbol is not set in the fixture's configuration"
done
if grep -qE 'CONFIG_CRUX_(THREAD|SYNC|TIME)[A-Z_]*=' "$fixture/prj.conf"; then
    fail 'the fixture names a Thread, Sync or Time symbol itself: C6 says the service symbol selects them'
fi
! grep -qE '^CONFIG_(WOLF|CRUX_SSH=)' "$config" || fail 'a Wolf or SSH symbol is set in the fixture'
printf 'ok: the fixture builds and passes, and CONFIG_CRUX_ACCESS selected Thread, Sync and Time\n'

# Each refusal is a configuration error with the module's message, before any compilation.
refuse() { # override, expected text, and the profile, which defaults to the fixture's
    local log="$out/refuse.log" use="${3:-$profile}"
    if (cd "$workspace" && west build -p always -b "$board" -d "$out/refuse" "$fixture" -- "$use" "-D$1") \
        > "$log" 2>&1; then
        fail "$1 was accepted"
    fi
    grep -q "$2" "$log" || { tail -20 "$log" >&2; fail "$1 stopped, but not with: $2"; }
    printf 'ok: %s is refused: %s\n' "$1" "$2"
}
# The Mbed profile check has no case here: the provider is a choice that a -D override does not move,
# and the only other provider is the Wolf one, which this change does not fetch.
refuse CONFIG_CRUX_LOGGING=n 'CONFIG_CRUX_ACCESS needs CONFIG_CRUX_LOGGING'
refuse CONFIG_HWINFO=n 'CONFIG_CRUX_ACCESS needs CONFIG_HWINFO'
refuse CONFIG_CRUX_FILE_FATFS_SIMULATOR=n 'CONFIG_CRUX_ACCESS needs a Crux File provider'
# The Mbed profile without the locks that its core calls (the profile conf without mbed-threading.conf).
refuse CONFIG_CRUX_ACCESS=y 'CONFIG_CRUX_ACCESS needs the locks' \
    '-DEXTRA_CONF_FILE=../tls_offload_probe/mbed.conf;../management_composition/profile-psa.conf'

# Default off: the module is discovered and the symbol is not set, so no Access code is linked.
zephyr="$workspace/zephyr"
off="$out/default-off"
(cd "$workspace" && west build -p always -b "$board" -d "$off" "$zephyr/samples/hello_world" \
    -- "-DZEPHYR_EXTRA_MODULES=$repo") > "$out/default-off-build.log" 2>&1 ||
    { tail -40 "$out/default-off-build.log" >&2; fail 'the default-off build failed'; }
! grep -q '^CONFIG_CRUX_ACCESS=y' "$off/zephyr/.config" || fail 'CONFIG_CRUX_ACCESS is on by default'
if nm "$off/zephyr/zephyr.elf" | grep -qE ' (crux_access_init|crux_credential_start|crux_security_event_init)$'; then
    fail 'an image that leaves the symbol off links Access, the store or the events'
fi
printf 'ok: default off, an image of the module links none of the three. Native_sim only: no board was built or run.\n'
