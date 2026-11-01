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

# Negative control: the same suite on the unpatched upstream backend must fail.
control="$repo/build/zephyr/settings-file-control"
(cd "$workspace" && west build -b native_sim/native/64 -d "$control" -p always \
    "$repo/tests/native/configuration_settings_file" -- \
    -DZEPHYR_MODULES="$fatfs" -DCRUX_SETTINGS_FILE_CONTROL=ON > "$control.build.log" 2>&1)
if "$control/zephyr/zephyr.exe" > "$control.run.log" 2>&1; then
    echo "settings-file control unexpectedly passed on the unpatched backend" >&2
    exit 1
fi
grep -q 'pass = 12, fail = 11' "$control.run.log" || {
    echo "settings-file control did not fail as characterised (12 pass, 11 fail)" >&2
    exit 1
}
echo "settings-file control (unpatched backend): 11 of 23 cases fail as expected"

# A filtered/build-only Twister run must not satisfy the provider gate.
python3 - "$repo/build/zephyr" <<'PY'
import json
from pathlib import Path
import sys

root = Path(sys.argv[1])
for directory, expected in [
    ('twister-storage-file', {'native_sim/native': 7, 'native_sim/native/64': 7}),
    ('twister-storage-fatfs', {'native_sim/native': 19, 'native_sim/native/64': 19}),
    ('twister-storage-configuration', {'native_sim/native/64': 10}),
    ('twister-storage-settings-file', {'native_sim/native/64': 23}),
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
git -C "$ZEPHYR_BASE" diff --exit-code -- \
    subsys/settings/src/settings_store.c subsys/settings/src/settings_nvs.c subsys/kvss/nvs/nvs.c \
    subsys/settings/src/settings_file.c subsys/settings/src/settings_line.c
