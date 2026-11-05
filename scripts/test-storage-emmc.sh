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
