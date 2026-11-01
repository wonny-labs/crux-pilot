#!/usr/bin/env bash
set -euo pipefail

repo="$(git rev-parse --show-toplevel)"
workspace="${CRUX_WORKSPACE:-$(dirname "$repo")}"
export ZEPHYR_BASE="$workspace/zephyr"
export ZEPHYR_TOOLCHAIN_VARIANT=host
littlefs="$(cd "$workspace" && west list littlefs -f '{abspath}')"
fatfs="$(cd "$workspace" && west list fatfs -f '{abspath}')"

# Actual VFS/LittleFS on both simulator widths; never a target board.
(cd "$workspace" && west twister -j 2 \
    -p native_sim/native -p native_sim/native/64 \
    -T "$repo/tests/native/file_littlefs" \
    -O "$repo/build/zephyr/twister-storage-file" --inline-logs \
    --extra-args="ZEPHYR_MODULES=$littlefs")

# Actual FatFs on a RAM disk on both simulator widths; never a target board.
(cd "$workspace" && west twister -j 2 \
    -p native_sim/native -p native_sim/native/64 \
    -T "$repo/tests/native/file_fatfs" \
    -O "$repo/build/zephyr/twister-storage-fatfs" --inline-logs \
    --extra-args="ZEPHYR_MODULES=$fatfs")

# The microSD composition sequence (mount order, fail-closed policy) on a RAM disk named "SD".
(cd "$workspace" && west twister -j 2 \
    -p native_sim/native -p native_sim/native/64 \
    -T "$repo/tests/native/file_sd_composition" \
    -O "$repo/build/zephyr/twister-storage-sd-composition" --inline-logs \
    --extra-args="ZEPHYR_MODULES=$fatfs")

# The Configuration fixture stages its existing three pinned source copies.
(cd "$workspace" && west twister -j 2 -p native_sim/native/64 \
    -T "$repo/tests/native/configuration_settings" \
    -O "$repo/build/zephyr/twister-storage-configuration" --inline-logs \
    --extra-args="ZEPHYR_MODULES=$repo")

python3 -m unittest discover -s "$repo/tests/native/configuration_settings" -p test_prepare.py -v

# Settings File on a FatFs RAM disk with the staged fault-propagation corrections. This is a
# 64-bit simulator fixture only; the corrected backend is not a product configuration.
(cd "$workspace" && west twister -j 2 -p native_sim/native/64 \
    -T "$repo/tests/native/configuration_settings_file" \
    -O "$repo/build/zephyr/twister-storage-settings-file" --inline-logs \
    --extra-args="ZEPHYR_MODULES=$fatfs")

python3 -m unittest discover -s "$repo/tests/native/configuration_settings_file" -p test_prepare.py -v

# Negative control: the same suite on the unpatched upstream backend must fail exactly the
# cases that characterise its defects and pass the rest.
control="$repo/build/zephyr/settings-file-control"
(cd "$workspace" && west build -b native_sim/native/64 -d "$control" -p always \
    "$repo/tests/native/configuration_settings_file" -- \
    -DZEPHYR_MODULES="$fatfs" -DCRUX_SETTINGS_FILE_CONTROL=ON > "$control.build.log" 2>&1)
if timeout 120 "$control/zephyr/zephyr.exe" > "$control.run.log" 2>&1; then
    echo "settings-file control unexpectedly passed on the unpatched backend" >&2
    exit 1
fi
python3 - "$control.run.log" <<'PY'
import re
import sys

expected = {
    'test_a_callback_error_is_returned_by_the_load',
    'test_compaction_close_error_is_not_success',
    'test_compaction_read_error_keeps_other_records',
    'test_compaction_read_fault_never_loses_a_record',
    'test_each_load_read_fault_preserves_active',
    'test_full_volume_never_acknowledges_a_lost_save',
    'test_interrupted_save_never_yields_a_wrong_value',
    'test_missing_file_with_a_compaction_leftover_is_an_error',
    'test_name_write_error_is_failure',
    'test_save_scan_error_is_not_success',
    'test_scan_read_error_is_not_missing',
    'test_short_write_is_failure',
    'test_zero_write_is_failure',
}
log = open(sys.argv[1], errors='replace').read()
failed = set(re.findall(r'^ - FAIL - \[configuration_settings_file\.(\w+)\]', log, re.M))
if failed != expected or 'pass = 12, fail = 13' not in log:
    raise RuntimeError(f'settings-file control differs: unexpected {sorted(failed - expected)}, '
                       f'missing {sorted(expected - failed)}')
print(f'settings-file control (unpatched backend): {len(failed)} of 25 cases fail as expected')
PY

# A filtered/build-only Twister run must not satisfy the provider gate.
python3 - "$repo/build/zephyr" <<'PY'
import json
from pathlib import Path
import sys

root = Path(sys.argv[1])
for directory, expected in [
    ('twister-storage-file', {'native_sim/native': 7, 'native_sim/native/64': 7}),
    ('twister-storage-fatfs', {'native_sim/native': 19, 'native_sim/native/64': 19}),
    ('twister-storage-sd-composition', {'native_sim/native': 8, 'native_sim/native/64': 8}),
    ('twister-storage-configuration', {'native_sim/native/64': 10}),
    ('twister-storage-settings-file', {'native_sim/native/64': 25}),
]:
    suites = json.loads((root / directory / 'twister.json').read_text())['testsuites']
    if len(suites) != len(expected) or {s['platform'] for s in suites} != set(expected):
        raise RuntimeError(f'{directory}: unexpected or missing simulator suites')
    for suite in suites:
        cases = suite.get('testcases', [])
        if (suite.get('status') != 'passed' or not suite.get('runnable') or
                len(cases) != expected[suite['platform']] or
                any(c.get('status') != 'passed' for c in cases)):
            raise RuntimeError(f'{directory}: provider cases did not all execute and pass')
        print(f"{directory} {suite['platform']}: {len(cases)} executed cases PASS")
PY

# Source staging must not patch the original Zephyr checkout.
git -C "$ZEPHYR_BASE" diff HEAD --exit-code -- \
    subsys/settings/src/settings_store.c subsys/settings/src/settings_nvs.c subsys/kvss/nvs/nvs.c \
    subsys/settings/src/settings_file.c subsys/settings/src/settings_line.c
