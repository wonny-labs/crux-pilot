#!/usr/bin/env bash
set -euo pipefail

if [[ "$(uname -s)" != Linux ]]; then
    printf 'Zephyr tests are configured for Linux only; reopen this repository in the Dev Container.\n' >&2
    exit 2
fi

repo="$(git rev-parse --show-toplevel)"
workspace="${CRUX_WORKSPACE:-$(dirname "$repo")}"
if [[ ! -f "$workspace/.west/config" || ! -d "$workspace/zephyr" ]]; then
    printf 'Run scripts/bootstrap-zephyr.sh before Zephyr tests.\n' >&2
    exit 2
fi
export ZEPHYR_BASE="$workspace/zephyr"
export ZEPHYR_TOOLCHAIN_VARIANT=host
platforms=(-p native_sim/native/64)
if [[ "${CRUX_STORAGE_32BIT:-0}" == 1 ]]; then
    platforms+=(-p native_sim/native)
fi

# This gate builds Reliance Edge, so a workspace without the optional tree fails rather than
# skipping those steps.
reliance="$workspace/modules/fs/reliance-edge"
if [[ ! -f "$reliance/include/redposix.h" ]]; then
    printf 'Reliance Edge source not found at %s. Enable the optional west group crux-reliance (west config manifest.group-filter -- +crux-reliance, then west update reliance-edge).\n' \
        "$reliance" >&2
    exit 2
fi

# The MMC stack against the eMMC simulator, with and without a cache, unmodified and with the
# build-local cache flush correction; never a target board.
(cd "$workspace" && west twister -j 2 "${platforms[@]}" \
    -T "$repo/tests/native/emmc_stack" \
    -O "$repo/build/zephyr/twister-emmc-stack" --inline-logs)

# The simulator's image across separate processes, with the unmodified and the corrected stack.
restart_dir="$repo/build/zephyr/emmc-restart"
restart_flush_dir="$repo/build/zephyr/emmc-restart-flush"
(cd "$workspace" && west build -p always -b native_sim/native/64 -d "$restart_dir" \
    "$repo/tests/native/emmc_restart")
(cd "$workspace" && west build -p always -b native_sim/native/64 -d "$restart_flush_dir" \
    "$repo/tests/native/emmc_restart" -- -DCONFIG_CRUX_MMC_CACHE_FLUSH=y)
python3 "$repo/tests/native/emmc_restart/run_restart.py" --exe "$restart_dir/zephyr/zephyr.exe" \
    --flush-exe "$restart_flush_dir/zephyr/zephyr.exe"

# The preparation of the corrected source refuses drift and output inside Zephyr.
python3 "$repo/tests/native/emmc_stack/test_prepare.py"

# A filtered or build-only Twister run must not satisfy the gate. The counts are executed cases;
# cases for the other profile skip themselves, and nothing else may be left unexecuted.
python3 - "$repo/build/zephyr/twister-emmc-stack/twister.json" <<'PY'
import json
import os
from pathlib import Path
import sys

executed = {
    'crux.emmc.stack.cached': 13,
    'crux.emmc.stack.nocache': 13,
    'crux.emmc.stack.flush.cached': 21,
    'crux.emmc.stack.flush.nocache': 13,
}
platforms = ['native_sim/native/64']
if os.environ.get('CRUX_STORAGE_32BIT') == '1':
    platforms.append('native_sim/native')
expected = {(scenario, platform): count
            for scenario, count in executed.items()
            for platform in platforms}

suites = json.loads(Path(sys.argv[1]).read_text())['testsuites']
found = {(suite['name'].rsplit('/', 1)[-1], suite['platform']): suite for suite in suites}
if len(suites) != len(found) or set(found) != set(expected):
    raise RuntimeError(f'unexpected or missing simulator suites: {sorted(found)}')
for key, suite in found.items():
    cases = suite.get('testcases', [])
    passed = [c for c in cases if c.get('status') == 'passed']
    if (suite.get('status') != 'passed' or not suite.get('runnable') or
            len(passed) != expected[key] or
            any(c.get('status') not in ('passed', 'skipped') for c in cases)):
        raise RuntimeError(f'{key}: the cases did not all execute and pass')
    print(f'{key[0]} {key[1]}: {len(passed)} executed cases PASS, '
          f'{len(cases) - len(passed)} skipped for the other profile')
PY

# Power-cut replay on FatFs: the medium is rebuilt from the simulator's trace at chosen cut points
# and checked by a new process. FatFs results are observations of the model, never asserted; the
# flush and damaged-image controls must fail. fat-nofix is the unmodified stack.
fatfs="$(cd "$workspace" && west list fatfs -f '{abspath}')"
powercut_dir="$repo/build/zephyr/emmc-powercut"
(cd "$workspace" && west build -p always -b native_sim/native/64 -d "$powercut_dir/fat-fix" \
    "$repo/tests/native/emmc_powercut" -- -DZEPHYR_MODULES="$fatfs" \
    -DEXTRA_CONF_FILE=fatfs.conf -DCONFIG_CRUX_MMC_CACHE_FLUSH=y)
(cd "$workspace" && west build -p always -b native_sim/native/64 -d "$powercut_dir/fat-nofix" \
    "$repo/tests/native/emmc_powercut" -- -DZEPHYR_MODULES="$fatfs" \
    -DEXTRA_CONF_FILE=fatfs.conf -DCONFIG_CRUX_MMC_CACHE_FLUSH=n)
python3 "$repo/tests/native/emmc_powercut/test_replay_model.py" -v 2>&1 |
    tee "$powercut_dir/test_replay_model.log"
grep -Eq '^Ran 17 tests in ' "$powercut_dir/test_replay_model.log"
# 64 samples took about 25 s in an emulated amd64 container. The bound is 5 minutes on one CI
# runner.
python3 "$repo/tests/native/emmc_powercut/run_replay.py" \
    --exe-fix "$powercut_dir/fat-fix/zephyr/zephyr.exe" \
    --exe-nofix "$powercut_dir/fat-nofix/zephyr/zephyr.exe" \
    --fs fatfs --seed 1 --samples 64 --max-seconds 300 --out "$powercut_dir/replay"

# The replay must not pass with fewer cases or a missing profile. The case count is exact for seed
# 1 and 64 samples. It depends on the traces, so on the pinned Zephyr and FatFs, and on the
# algorithms of Python's random module, which draw the sampled positions.
python3 - "$powercut_dir/replay/fatfs.json" <<'PY'
import json
from pathlib import Path
import sys

report = json.loads(Path(sys.argv[1]).read_text())
models = {'cached': {'lost', 'prefix', 'reordered', 'torn'}, 'nocache': {'prefix', 'torn'}}
for workload in ('W1', 'W2', 'W3'):
    for profile, expected in models.items():
        groups = report['observations'][workload][profile]
        if set(groups) != expected or any(group['cases'] == 0 for group in groups.values()):
            raise RuntimeError(f'{workload} {profile}: missing observations')
controls = {control['name']: control['failed_as_required'] for control in report['controls']}
if controls != {'flush': True, 'damaged image': True}:
    raise RuntimeError(f'controls: {controls}')
if report['counts']['cases'] != 1182 or report['failures']:
    raise RuntimeError(f'counts {report["counts"]}, failures {report["failures"]}')
print(f'replay fatfs: {report["counts"]["cases"]} cases, '
      f'{report["counts"]["observed_failures"]} observed failures, '
      'both controls failed as required')
PY

# Power-cut replay on Reliance Edge: the invariants are asserted on red-fix for W1 to W3 on both
# profiles, and four controls must fail. red-nofix is the unmodified stack, allowed by the
# native_sim-only control variable. The images link the GPLv2 source and are test images only.
(cd "$workspace" && west build -p always -b native_sim/native/64 -d "$powercut_dir/red-fix" \
    "$repo/tests/native/emmc_powercut" -- -DEXTRA_CONF_FILE=reliance.conf \
    -DCONFIG_CRUX_MMC_CACHE_FLUSH=y)
(cd "$workspace" && west build -p always -b native_sim/native/64 -d "$powercut_dir/red-nofix" \
    "$repo/tests/native/emmc_powercut" -- -DEXTRA_CONF_FILE=reliance.conf \
    -DCONFIG_CRUX_MMC_CACHE_FLUSH=n -DCRUX_EMMC_FLUSH_CONTROL=ON)
# 64 samples took about 97 s in an emulated amd64 container, most of it the mandatory positions.
python3 "$repo/tests/native/emmc_powercut/run_replay.py" \
    --exe-fix "$powercut_dir/red-fix/zephyr/zephyr.exe" \
    --exe-nofix "$powercut_dir/red-nofix/zephyr/zephyr.exe" \
    --fs reliance --seed 1 --samples 64 --max-seconds 300 --out "$powercut_dir/replay"

# The counts are exact for seed 1 and 64 samples. They depend on the traces, so on the pinned
# Zephyr and Reliance Edge, and on the algorithms of Python's random module.
python3 - "$powercut_dir/replay/reliance.json" <<'PY'
import json
from pathlib import Path
import sys

report = json.loads(Path(sys.argv[1]).read_text())
models = {'cached': {'lost', 'prefix', 'reordered', 'torn'}, 'nocache': {'prefix', 'torn'}}
for workload in ('W1', 'W2', 'W3'):
    for profile, expected in models.items():
        groups = report['observations'][workload][profile]
        if (set(groups) != expected or
                any(group['cases'] == 0 or group['passed'] != group['cases']
                    for group in groups.values())):
            raise RuntimeError(f'{workload} {profile}: missing or failing cases')
        trace = report['traces'][workload][profile]
        if trace['metaroot_positions'] == 0 or (profile == 'cached' and trace['commits'] == 0):
            raise RuntimeError(f'{workload} {profile}: no metaroot write was recognised')
controls = {control['name']: control['failed_as_required'] for control in report['controls']}
expected_controls = {'flush': True, 'barrier': True, 'early acknowledgement': True,
                     'damaged image': True}
if controls != expected_controls:
    raise RuntimeError(f'controls: {controls}')
barrier = next(control for control in report['controls'] if control['name'] == 'barrier')
observed = sorted(entry['workload'] for entry in report['barrier_observations'])
created = report['created_empty']
staged = created['staged']
if (barrier['workload'] != 'W2' or observed != ['W1', 'W3'] or created['cases'] != 253 or
        sorted(created['steps']) != ['11', '2', '4', '6'] or
        not created['acknowledged_intact'] or len(created['checks']) != 16 or
        not created['checks_failed_as_required'] or not staged['as_required'] or
        (staged['cases'], staged['accepted_empty'], staged['refused_empty']) != (67, 4, 51)):
    raise RuntimeError(f'barrier on {barrier["workload"]}, created-empty {created["cases"]}, '
                       f'staged {staged["cases"]}')
if report['counts']['cases'] != 4947 or report['failures']:
    raise RuntimeError(f'counts {report["counts"]}, failures {report["failures"]}')
print(f'replay reliance: {report["counts"]["cases"]} cases, invariants held on red-fix, '
      f'{created["cases"]} W3 cases with a created, still empty file, all four controls '
      'failed as required')
PY

# Reliance Edge behind the Zephyr VFS, and the File provider on it, on a RAM disk named MMC. The
# images link the GPLv2 source and are test images only.
(cd "$workspace" && west twister -j 2 "${platforms[@]}" \
    -T "$repo/tests/native/file_reliance" \
    -O "$repo/build/zephyr/twister-file-reliance" --inline-logs)
python3 - "$repo/build/zephyr/twister-file-reliance/twister.json" <<'PY'
import json
import os
from pathlib import Path
import sys

# 22 adapter cases and 33 File cases in each scenario; the offset scenario runs the same cases on a
# volume that starts 128 sectors into the disk.
executed = {'crux.file.reliance': 55, 'crux.file.reliance.offset': 55}
platforms = ['native_sim/native/64']
if os.environ.get('CRUX_STORAGE_32BIT') == '1':
    platforms.append('native_sim/native')
expected = {(scenario, platform): count
            for scenario, count in executed.items()
            for platform in platforms}

suites = json.loads(Path(sys.argv[1]).read_text())['testsuites']
found = {(suite['name'].rsplit('/', 1)[-1], suite['platform']): suite for suite in suites}
if len(suites) != len(found) or set(found) != set(expected):
    raise RuntimeError(f'unexpected or missing Reliance Edge suites: {sorted(found)}')
for key, suite in found.items():
    cases = suite.get('testcases', [])
    passed = [c for c in cases if c.get('status') == 'passed']
    if (suite.get('status') != 'passed' or not suite.get('runnable') or
            len(passed) != expected[key] or len(cases) != expected[key]):
        raise RuntimeError(f'{key}: the cases did not all execute and pass')
    print(f'{key[0]} {key[1]}: {len(passed)} executed cases PASS')
PY

# The File provider on Reliance Edge links no FatFs: no image of the suite holds f_mount, and each
# holds the File provider, so the check looks at the right images. One image per scenario and
# platform; platforms holds a "-p name" pair per platform.
images=()
while IFS= read -r image; do
    images+=("$image")
done < <(find "$repo/build/zephyr/twister-file-reliance" -type f -path '*/zephyr/zephyr.exe' \
    -path '*crux.file.reliance*' | sort)
expected_images=$((2 * (${#platforms[@]} / 2)))
if (( ${#images[@]} != expected_images )); then
    printf 'Expected %d Reliance Edge images, found %d\n' "$expected_images" "${#images[@]}" >&2
    exit 1
fi
for image in "${images[@]}"; do
    symbols="$(nm "$image")"
    if grep -q 'f_mount' <<<"$symbols"; then
        printf 'FatFs is linked into %s\n' "$image" >&2
        exit 1
    fi
    grep -Eq '[[:space:]]crux_file_open$' <<<"$symbols" || {
        printf 'The File provider is not in %s\n' "$image" >&2
        exit 1
    }
done
printf 'Reliance Edge images: %d without FatFs, each with the File provider\n' "${#images[@]}"

# Negative builds, configuration only. Reliance Edge on an MMC disk without the cache flush
# correction must fail, naming the option; the correction or the fixture-only control lets it
# configure, so the failure is the guard's.
guard_dir="$repo/build/zephyr/reliance-guard"
guard_log="$guard_dir.log"
if (cd "$workspace" && west build --cmake-only -p always -b native_sim/native/64 -d "$guard_dir" \
        "$repo/tests/native/emmc_stack" -- -DCONFIG_FILE_SYSTEM=y \
        -DCONFIG_CRUX_FS_RELIANCE_EDGE=y) >"$guard_log" 2>&1; then
    printf 'Reliance Edge on an MMC disk configured without CONFIG_CRUX_MMC_CACHE_FLUSH\n' >&2
    exit 1
fi
# CMake wraps a message over lines, so whitespace is folded before matching.
folded="$(tr -s '[:space:]' ' ' <"$guard_log")"
[[ "$folded" == *'Reliance Edge on an MMC disk requires CONFIG_CRUX_MMC_CACHE_FLUSH=y'* ]] || {
    printf 'The MMC guard did not fail for the stated reason; see %s\n' "$guard_log" >&2
    exit 1
}
for allowed in -DCONFIG_CRUX_MMC_CACHE_FLUSH=y -DCRUX_EMMC_FLUSH_CONTROL=ON; do
    (cd "$workspace" && west build --cmake-only -p always -b native_sim/native/64 -d "$guard_dir" \
        "$repo/tests/native/emmc_stack" -- -DCONFIG_FILE_SYSTEM=y \
        -DCONFIG_CRUX_FS_RELIANCE_EDGE=y "$allowed") >"$guard_log" 2>&1 || {
        printf 'The MMC guard refused %s; see %s\n' "$allowed" "$guard_log" >&2
        exit 1
    }
done
printf 'Reliance Edge MMC guard: refused without the correction, allowed with it or the control\n'

# A tree that is not there is a configuration error that names the west group.
missing_dir="$repo/build/zephyr/reliance-missing"
missing_log="$missing_dir.log"
if (cd "$workspace" && west build --cmake-only -p always -b native_sim/native/64 -d "$missing_dir" \
        "$repo/tests/native/file_reliance" -- -DCRUX_RELIANCE_EDGE_DIR=/nonexistent) \
        >"$missing_log" 2>&1; then
    printf 'A build without the Reliance Edge tree configured\n' >&2
    exit 1
fi
folded="$(tr -s '[:space:]' ' ' <"$missing_log")"
[[ "$folded" == *'Enable the optional west group crux-reliance'* ]] || {
    printf 'A missing Reliance Edge tree did not name the west group; see %s\n' "$missing_log" >&2
    exit 1
}
printf 'Reliance Edge missing tree: refused, naming the west group crux-reliance\n'

# The File provider's build checks on Reliance Edge. Each build must fail for its own reason: a
# handle count below the File pools, and a VFS name limit below the File name limit. The build
# keeps going after the first error, so that every check that fires is in the log.
selection_dir="$repo/build/zephyr/reliance-file-checks"
selection_log="$selection_dir.log"
check_refused_build() {
    local reason="$1"
    shift
    if (cd "$workspace" && west build -p always -b native_sim/native/64 -d "$selection_dir" \
            -o=-k0 "$repo/tests/native/file_reliance" -- "$@") >"$selection_log" 2>&1; then
        printf 'The File provider on Reliance Edge built with %s\n' "$*" >&2
        exit 1
    fi
    folded="$(tr -s '[:space:]' ' ' <"$selection_log")"
    [[ "$folded" == *"$reason"* ]] || {
        printf 'The build with %s did not fail for the stated reason; see %s\n' "$*" \
            "$selection_log" >&2
        exit 1
    }
}
check_refused_build 'CONFIG_CRUX_FS_RELIANCE_EDGE_HANDLE_COUNT must cover the File handle pools' \
    -DCONFIG_CRUX_FS_RELIANCE_EDGE_HANDLE_COUNT=5
check_refused_build 'set CONFIG_FILE_SYSTEM_MAX_FILE_NAME to at least the File name limit' \
    -DCONFIG_FILE_SYSTEM_MAX_FILE_NAME=62
printf 'Reliance Edge File checks: refused below the File pools and below the File name limit\n'

# The selection excludes the other File selections in Kconfig, which drops the Reliance Edge
# selection with a warning naming the dependency. With the flash simulator alone the selection
# stays, so the drop with the VFS simulator selection is the exclusion's.
flash=(-DCONFIG_FLASH=y -DCONFIG_FLASH_SIMULATOR=y)
selection_config="$selection_dir/zephyr/.config"
(cd "$workspace" && west build --cmake-only -p always -b native_sim/native/64 -d "$selection_dir" \
    "$repo/tests/native/file_reliance" -- "${flash[@]}") >"$selection_log" 2>&1 || {
    printf 'The Reliance Edge suite did not configure with the flash simulator; see %s\n' \
        "$selection_log" >&2
    exit 1
}
grep -qx 'CONFIG_CRUX_FILE_RELIANCE_EDGE=y' "$selection_config" || {
    printf 'The flash simulator alone dropped the Reliance Edge selection; see %s\n' \
        "$selection_log" >&2
    exit 1
}
(cd "$workspace" && west build --cmake-only -p always -b native_sim/native/64 \
    -d "$selection_dir" "$repo/tests/native/file_reliance" -- "${flash[@]}" \
    -DCONFIG_CRUX_FILE_SIMULATOR=y) >"$selection_log" 2>&1 || true
if [[ ! -f "$selection_config" ]] || grep -qx 'CONFIG_CRUX_FILE_RELIANCE_EDGE=y' \
        "$selection_config"; then
    printf 'The Reliance Edge selection was kept together with CONFIG_CRUX_FILE_SIMULATOR\n' >&2
    exit 1
fi
folded="$(tr -s '[:space:]' ' ' <"$selection_log")"
[[ "$folded" == *"CRUX_FILE_RELIANCE_EDGE"*"was assigned the value 'y' but got the value 'n'"*'(!CRUX_FILE_SIMULATOR)'* ]] || {
    printf 'The Reliance Edge selection was not dropped by the exclusion; see %s\n' \
        "$selection_log" >&2
    exit 1
}
printf 'Reliance Edge File selection: dropped together with another File selection\n'
