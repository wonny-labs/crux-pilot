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

# The unmodified MMC stack against the eMMC simulator, with and without a cache; never a target board.
(cd "$workspace" && west twister -j 2 "${platforms[@]}" \
    -T "$repo/tests/native/emmc_stack" \
    -O "$repo/build/zephyr/twister-emmc-stack" --inline-logs)

# The simulator's image across separate processes.
restart_dir="$repo/build/zephyr/emmc-restart"
(cd "$workspace" && west build -p always -b native_sim/native/64 -d "$restart_dir" \
    "$repo/tests/native/emmc_restart")
python3 "$repo/tests/native/emmc_restart/run_restart.py" --exe "$restart_dir/zephyr/zephyr.exe"

# A filtered or build-only Twister run must not satisfy the gate.
python3 - "$repo/build/zephyr/twister-emmc-stack/twister.json" <<'PY'
import json
import os
from pathlib import Path
import sys

counts = {'native_sim/native/64': 13}
if os.environ.get('CRUX_STORAGE_32BIT') == '1':
    counts['native_sim/native'] = 13
expected = {(scenario, platform): count
            for scenario in ('crux.emmc.stack.cached', 'crux.emmc.stack.nocache')
            for platform, count in counts.items()}

suites = json.loads(Path(sys.argv[1]).read_text())['testsuites']
found = {(suite['name'].rsplit('/', 1)[-1], suite['platform']): suite for suite in suites}
if len(suites) != len(found) or set(found) != set(expected):
    raise RuntimeError(f'unexpected or missing simulator suites: {sorted(found)}')
for key, suite in found.items():
    cases = suite.get('testcases', [])
    if (suite.get('status') != 'passed' or not suite.get('runnable') or
            len(cases) != expected[key] or any(c.get('status') != 'passed' for c in cases)):
        raise RuntimeError(f'{key}: the cases did not all execute and pass')
    print(f'{key[0]} {key[1]}: {len(cases)} executed cases PASS')
PY
