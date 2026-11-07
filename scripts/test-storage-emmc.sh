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

# The eMMC reference composition, samples/storage_emmc, on the eMMC simulator with Reliance Edge and
# the cache flush: the microSD composition's cases, a File sync that must send a flush command, and
# a card that fails its first initialisation. The images link the GPLv2 source and are test images
# only.
(cd "$workspace" && west twister -j 2 "${platforms[@]}" \
    -T "$repo/tests/native/file_emmc_composition" \
    -O "$repo/build/zephyr/twister-file-emmc-composition" --inline-logs)
python3 - "$repo/build/zephyr/twister-file-emmc-composition/twister.json" <<'PY'
import json
import os
from pathlib import Path
import sys

# The init_failure scenario must be a process of its own, so its one case is the only one that
# executes there and every other case skips itself; the main scenario skips that case instead.
executed = {'crux.file.emmc_composition': 9, 'crux.file.emmc_composition.init_failure': 1}
init_failure_case = 'h_card_init_failure_reports_no_media'
platforms = ['native_sim/native/64']
if os.environ.get('CRUX_STORAGE_32BIT') == '1':
    platforms.append('native_sim/native')
expected = {(scenario, platform): count
            for scenario, count in executed.items()
            for platform in platforms}

suites = json.loads(Path(sys.argv[1]).read_text())['testsuites']
found = {(suite['name'].rsplit('/', 1)[-1], suite['platform']): suite for suite in suites}
if len(suites) != len(found) or set(found) != set(expected):
    raise RuntimeError(f'unexpected or missing eMMC composition suites: {sorted(found)}')
for key, suite in found.items():
    cases = suite.get('testcases', [])
    passed = [c for c in cases if c.get('status') == 'passed']
    ran_init_failure = [c for c in passed if c['identifier'].endswith('.' + init_failure_case)]
    if (suite.get('status') != 'passed' or not suite.get('runnable') or
            len(passed) != expected[key] or len(cases) != 10 or
            any(c.get('status') not in ('passed', 'skipped') for c in cases) or
            bool(ran_init_failure) != key[0].endswith('.init_failure')):
        raise RuntimeError(f'{key}: the cases did not all execute and pass')
    print(f'{key[0]} {key[1]}: {len(passed)} executed cases PASS, '
          f'{len(cases) - len(passed)} skipped for the other scenario')
PY

# Configuration on the Settings File backend over Reliance Edge, through the target provider
# (CONFIG_CRUX_CONFIGURATION_SETTINGS_FILE) and not through the provider copy of the FatFs suite. The
# cases of tests/native/configuration_settings_file run here on a RAM disk named MMC, with the
# corrected backend that the module stages, and so do the cases of the target provider. 64-bit
# native_sim only, as the FatFs suite. The images link the GPLv2 source and are test images only.
(cd "$workspace" && west twister -j 2 -p native_sim/native/64 \
    -T "$repo/tests/native/configuration_settings_reliance" \
    -O "$repo/build/zephyr/twister-settings-reliance" --inline-logs)
python3 - "$repo/build/zephyr/twister-settings-reliance" <<'PY'
import json
from pathlib import Path
import subprocess
import sys

output = Path(sys.argv[1])
# Twister scans only the cases of extra_cases.c. The others are in the shared main.c, and the exact
# names are required here, so that a case dropped from either source cannot go unnoticed.
shared = {
    'a_callback_error_is_returned_by_the_load', 'a_missing_settings_file_reads_as_missing',
    'callback_short_count_preserves_active', 'compaction_close_error_is_not_success',
    'compaction_read_error_keeps_other_records', 'compaction_read_fault_never_loses_a_record',
    'each_load_read_fault_preserves_active', 'full_volume_never_acknowledges_a_lost_save',
    'interrupted_save_never_yields_a_wrong_value', 'malformed_records_are_rejected',
    'many_saves_keep_every_key_across_compactions', 'missing_and_valid_roundtrip',
    'missing_file_with_a_compaction_leftover_is_an_error', 'name_write_error_is_failure',
    'read_fault_never_yields_a_stale_value', 'roundtrip_survives_remount_for_u32_and_bool',
    'save_after_a_torn_tail_records_the_outcome', 'save_scan_error_is_not_success',
    'scan_read_error_is_not_missing', 'short_write_is_failure', 'staging_limit_boundary',
    'truncated_tail_keeps_earlier_records', 'value_only_corruption_is_not_detected',
    'zero_write_is_failure',
}
extra = {
    'compaction_leaves_no_cmp_file',
    'start_on_a_medium_without_settings_dir_does_not_init_settings',
    'start_on_a_provisioned_medium_inits_settings_once',
}
init_failure = {'a_failed_settings_init_is_reported'}
# The main scenario skips the case of the other one, and that scenario skips the other cases, because
# the File root binds once per process.
expected = {
    'crux.configuration.settings_reliance': (shared | extra, init_failure),
    'crux.configuration.settings_reliance.init_failure': (init_failure, extra),
}

suites = json.loads((output / 'twister.json').read_text())['testsuites']
found = {suite['name'].rsplit('/', 1)[-1]: suite for suite in suites}
if len(suites) != len(found) or set(found) != set(expected):
    raise RuntimeError(f'unexpected or missing Settings File suites: {sorted(found)}')
for name, suite in found.items():
    prefix = name + '.configuration_settings_reliance.'
    cases = {c['identifier'][len(prefix):]: c['status'] for c in suite.get('testcases', [])}
    passed = {case for case, status in cases.items() if status == 'passed'}
    skipped = {case for case, status in cases.items() if status == 'skipped'}
    if (suite.get('platform') != 'native_sim/native/64' or suite.get('status') != 'passed' or
            not suite.get('runnable') or (passed, skipped) != expected[name] or
            len(cases) != len(passed) + len(skipped)):
        raise RuntimeError(f'{name}: passed {sorted(passed)}, skipped {sorted(skipped)}')
    print(f'{name}: {len(passed)} executed cases PASS, {len(skipped)} skipped for the other scenario')

# The main image holds the real provider and not the probe copy, no FatFs, and was built from the
# corrected copies of the Settings File sources and not from the pinned originals.
builds = [p for p in output.rglob('compile_commands.json')
          if p.parent.name == 'crux.configuration.settings_reliance']
if len(builds) != 1:
    raise RuntimeError(f'expected one main build, found {len(builds)}')
build = builds[0].parent
commands = json.loads(builds[0].read_text())
files = {str(Path(entry['directory'], entry['file']).resolve()) for entry in commands}
staged = {name for name in ('settings_file.c', 'settings_line.c', 'settings_store.c')
          if str((build / 'settings-file-candidate' / name).resolve()) in files}
originals = [f for f in files if f.endswith(('subsys/settings/src/settings_file.c',
                                                'subsys/settings/src/settings_line.c',
                                                'subsys/settings/src/settings_store.c'))]
if staged != {'settings_file.c', 'settings_line.c', 'settings_store.c'} or originals:
    raise RuntimeError(f'staged copies {sorted(staged)}, pinned originals built {originals}')
symbols = subprocess.run(['nm', str(build / 'zephyr/zephyr.exe')], check=True, capture_output=True,
                         text=True).stdout.split()
for wanted in ('crux_config_zephyr_init', 'crux_config_save', 'emmc_configuration_start'):
    if wanted not in symbols:
        raise RuntimeError(f'{wanted} is not in the image')
for unwanted in ('crux_probe_read_back', 'f_mount'):
    if unwanted in symbols:
        raise RuntimeError(f'{unwanted} is in the image of the target provider')
print('Settings File on Reliance Edge: the real provider with the staged corrections, no FatFs, '
      'no probe copy')
PY

# Negative control: the same suite on the unpatched upstream backend (the native_sim-only
# CRUX_SETTINGS_FILE_CONTROL, which the module refuses on any other board) must fail exactly the
# cases that characterise its defects on this file system, and pass the rest. It is not a Twister
# scenario, because a scenario that must fail would stop the gate.
control="$repo/build/zephyr/settings-reliance-control"
(cd "$workspace" && west build -b native_sim/native/64 -d "$control" -p always \
    "$repo/tests/native/configuration_settings_reliance" -- \
    -DCRUX_SETTINGS_FILE_CONTROL=ON > "$control.build.log" 2>&1)
if timeout 180 "$control/zephyr/zephyr.exe" > "$control.run.log" 2>&1; then
    echo "settings-reliance control unexpectedly passed on the unpatched backend" >&2
    exit 1
fi
python3 - "$control.run.log" <<'PY'
import re
import sys

# The 13 cases that fail on the FatFs RAM disk (scripts/test-storage-simulators.sh) less one. The
# 12 below fail here for the same reason: the unpatched backend turns an I/O fault that the fixture
# injects into success, into a missing key or into a lost record, and the injection is the same.
# test_full_volume_never_acknowledges_a_lost_save fails on FatFs and passes here. The backend's
# write handler returns a negative result and turns a byte count into success. FatFs answers a
# write that finds no room with a short or a zero count, which is lost without an error. Reliance
# Edge answers a write that finds no free block with -ENOSPC, which the unpatched backend returns,
# so on this file system the case ends in an error that reaches the caller. The unpatched handler
# still hides a short count here, which test_short_write_is_failure shows with an injected one. The
# set is exact, so a change in what this file system does at a full volume shows up as a
# difference to review. The ones that skip are the init-failure case, which has a scenario of its
# own.
expected = {
    'test_a_callback_error_is_returned_by_the_load',
    'test_compaction_close_error_is_not_success',
    'test_compaction_read_error_keeps_other_records',
    'test_compaction_read_fault_never_loses_a_record',
    'test_each_load_read_fault_preserves_active',
    'test_interrupted_save_never_yields_a_wrong_value',
    'test_missing_file_with_a_compaction_leftover_is_an_error',
    'test_name_write_error_is_failure',
    'test_save_scan_error_is_not_success',
    'test_scan_read_error_is_not_missing',
    'test_short_write_is_failure',
    'test_zero_write_is_failure',
}
log = open(sys.argv[1], errors='replace').read()
failed = set(re.findall(r'^ - FAIL - \[configuration_settings_reliance\.(\w+)\]', log, re.M))
if failed != expected or 'pass = 15, fail = 12, skip = 1, total = 28' not in log:
    raise RuntimeError(f'settings-reliance control differs: unexpected {sorted(failed - expected)}, '
                       f'missing {sorted(expected - failed)}')
print(f'settings-reliance control (unpatched backend): {len(failed)} of 28 cases fail as expected, '
      '15 pass and 1 skips')
PY

# Power-cut replay of W4: three keys saved through the Settings File backend, which compacts its
# file through a temporary and a rename. The images are built from reliance.conf and settings.conf,
# so they hold the corrected backend that the module stages. The invariants are asserted on
# red-fix-settings for both profiles, and three controls must fail as required. W4 is a run of its
# own with its own JSON, so the counts and the controls of the W1 to W3 run above do not move.
(cd "$workspace" && west build -p always -b native_sim/native/64 -d "$powercut_dir/red-fix-settings" \
    "$repo/tests/native/emmc_powercut" -- "-DEXTRA_CONF_FILE=reliance.conf;settings.conf" \
    -DCONFIG_CRUX_MMC_CACHE_FLUSH=y)
(cd "$workspace" && west build -p always -b native_sim/native/64 -d "$powercut_dir/red-nofix-settings" \
    "$repo/tests/native/emmc_powercut" -- "-DEXTRA_CONF_FILE=reliance.conf;settings.conf" \
    -DCONFIG_CRUX_MMC_CACHE_FLUSH=n -DCRUX_EMMC_FLUSH_CONTROL=ON)
# 8 samples took about 32 s in an emulated amd64 container, most of it the mandatory positions.
python3 "$repo/tests/native/emmc_powercut/run_replay.py" \
    --exe-fix "$powercut_dir/red-fix-settings/zephyr/zephyr.exe" \
    --exe-nofix "$powercut_dir/red-nofix-settings/zephyr/zephyr.exe" \
    --fs reliance --workload W4 --seed 1 --samples 8 --max-seconds 300 --out "$powercut_dir/replay"

# The counts are exact for seed 1 and 8 samples. They depend on the traces, so on the pinned Zephyr
# and Reliance Edge, and on the algorithms of Python's random module. A cut that leaves the
# backend's temporary behind is counted, and is not a failure. A missing active file would be.
python3 - "$powercut_dir/replay/reliance-w4.json" <<'PY'
import json
from pathlib import Path
import sys

report = json.loads(Path(sys.argv[1]).read_text())
if report.get('workload') != 'W4' or list(report['observations']) != ['W4']:
    raise RuntimeError(f'not a W4 report: {sorted(report)}')
models = {'cached': {'lost', 'prefix', 'reordered', 'torn'}, 'nocache': {'prefix', 'torn'}}
leftovers = 0
for profile, expected in models.items():
    groups = report['observations']['W4'][profile]
    if (set(groups) != expected or
            any(group['cases'] == 0 or group['passed'] != group['cases'] for group in groups.values())):
        raise RuntimeError(f'W4 {profile}: missing or failing cases')
    leftovers += sum(group.get('cmp_leftover', 0) for group in groups.values())
    trace = report['traces']['W4'][profile]
    if trace['metaroot_positions'] == 0 or (profile == 'cached' and trace['commits'] == 0):
        raise RuntimeError(f'W4 {profile}: no metaroot write was recognised')
if leftovers == 0:
    raise RuntimeError('no W4 cut left a temporary behind, so none reached the compaction')
controls = {control['name']: control['failed_as_required'] for control in report['controls']}
if controls != {'flush': True, 'early acknowledgement': True, 'damaged image': True}:
    raise RuntimeError(f'controls: {controls}')
staged = report['staged_w4']
if (not staged['as_required'] or
        sorted(case['damage'] for case in staged['cases']) != ['leftover', 'missing', 'none']):
    raise RuntimeError(f'staged W4 states: {staged}')
if report['counts']['cases'] != 2232 or report['failures']:
    raise RuntimeError(f'counts {report["counts"]}, failures {report["failures"]}')
print(f'replay reliance W4: {report["counts"]["cases"]} cases, invariants held on red-fix-settings, '
      f'{leftovers} cases with a temporary left behind, all three controls failed as required')
PY

# The Settings File selection exists on Reliance Edge only. Without the file system, Kconfig drops
# the selection and warns, naming the dependency, and the build is refused because the assignment of
# the suite is not honoured. A selection that did not depend on Reliance Edge would configure.
selection_dir="$repo/build/zephyr/settings-selection"
selection_log="$selection_dir.log"
if (cd "$workspace" && west build --cmake-only -p always -b native_sim/native/64 -d "$selection_dir" \
        "$repo/tests/native/configuration_settings_reliance" -- -DCONFIG_CRUX_FS_RELIANCE_EDGE=n) \
        >"$selection_log" 2>&1; then
    printf 'The Settings File selection configured without CONFIG_CRUX_FS_RELIANCE_EDGE\n' >&2
    exit 1
fi
folded="$(tr -s '[:space:]' ' ' <"$selection_log")"
[[ "$folded" == *"warning: CRUX_CONFIGURATION_SETTINGS_FILE (defined at"*"was assigned the value 'y' but got the value 'n'. Check these unsatisfied dependencies: CRUX_FS_RELIANCE_EDGE (=n)"* ]] || {
    printf 'The Settings File selection was not dropped for the missing file system; see %s\n' \
        "$selection_log" >&2
    exit 1
}
printf 'Settings File selection: dropped without Reliance Edge, naming the dependency\n'
