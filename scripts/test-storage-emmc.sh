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
grep -Eq '^Ran 14 tests in ' "$powercut_dir/test_replay_model.log"
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
