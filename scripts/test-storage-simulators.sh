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

# The File additions (roots, stat, size, rename, listing) on the same FatFs RAM disk.
(cd "$workspace" && west twister -j 2 \
    -p native_sim/native -p native_sim/native/64 \
    -T "$repo/tests/native/file_roots" \
    -O "$repo/build/zephyr/twister-storage-file-roots" --inline-logs \
    --extra-args="ZEPHYR_MODULES=$fatfs")

# The credential store's contract on the same FatFs, with its test-only protection (Issue #142).
(cd "$workspace" && west twister -j 2 \
    -p native_sim/native -p native_sim/native/64 \
    -T "$repo/tests/native/credential_fatfs" \
    -O "$repo/build/zephyr/twister-storage-credential" --inline-logs \
    --extra-args="ZEPHYR_MODULES=$fatfs")

# The microSD composition sequence (mount order, fail-closed policy) on a RAM disk named "SD".
(cd "$workspace" && west twister -j 2 \
    -p native_sim/native -p native_sim/native/64 \
    -T "$repo/tests/native/file_sd_composition" \
    -O "$repo/build/zephyr/twister-storage-sd-composition" --inline-logs \
    --extra-args="ZEPHYR_MODULES=$fatfs")

# The write/read/restart check of the same sample (boot counter and a 4 KiB pattern file).
(cd "$workspace" && west twister -j 2 \
    -p native_sim/native -p native_sim/native/64 \
    -T "$repo/tests/native/file_sd_check" \
    -O "$repo/build/zephyr/twister-storage-sd-check" --inline-logs \
    --extra-args="ZEPHYR_MODULES=$fatfs")

# The seek and remove check of the same sample (scratch file, seek, read back, remove).
(cd "$workspace" && west twister -j 2 \
    -p native_sim/native -p native_sim/native/64 \
    -T "$repo/tests/native/file_sd_ops" \
    -O "$repo/build/zephyr/twister-storage-sd-ops" --inline-logs \
    --extra-args="ZEPHYR_MODULES=$fatfs")

# The interrupted-write test: record format and recovery logic of samples/storage_sd_interrupt.
(cd "$workspace" && west twister -j 2 \
    -p native_sim/native -p native_sim/native/64 \
    -T "$repo/tests/native/file_sd_interrupt" \
    -O "$repo/build/zephyr/twister-storage-sd-interrupt" --inline-logs \
    --extra-args="ZEPHYR_MODULES=$fatfs")

# Its host script (parsing, pairing, port loss and return) runs against scripted boards, no hardware.
python3 -m unittest discover -s "$repo/samples/storage_sd_interrupt/host" -p test_interrupt_capture.py -v

# The replace-pattern power-cut test: classification logic of samples/storage_sd_replace.
(cd "$workspace" && west twister -j 2 \
    -p native_sim/native -p native_sim/native/64 \
    -T "$repo/tests/native/file_sd_replace" \
    -O "$repo/build/zephyr/twister-storage-sd-replace" --inline-logs \
    --extra-args="ZEPHYR_MODULES=$fatfs")

# Its host script (parsing, classification, cycle loop) runs against scripted boards, no hardware.
python3 -m unittest discover -s "$repo/samples/storage_sd_replace/host" -p test_replace_capture.py -v

# The probes for the remaining physical items: the workload and the removal state machine.
(cd "$workspace" && west twister -j 2 \
    -p native_sim/native -p native_sim/native/64 \
    -T "$repo/tests/native/file_sd_probe" \
    -O "$repo/build/zephyr/twister-storage-sd-probe" --inline-logs \
    --extra-args="ZEPHYR_MODULES=$fatfs")

# The recovery step for the one state a Settings File compaction can leave after a power cut (no
# settings file, a complete temporary), on the real unpatched Settings File backend.
(cd "$workspace" && west twister -j 2 \
    -p native_sim/native -p native_sim/native/64 \
    -T "$repo/tests/native/file_sd_settings_recover" \
    -O "$repo/build/zephyr/twister-storage-sd-settings-recover" --inline-logs \
    --extra-args="ZEPHYR_MODULES=$fatfs")

# The shared Configuration conformance cases, scalar and string, with the fake provider.
(cd "$workspace" && west twister -j 2 -p native_sim/native/64 \
    -T "$repo/tests/configuration/zephyr" \
    -O "$repo/build/zephyr/twister-configuration-software" --inline-logs)

# The status and configuration services with the real System, Time and Sync. Only here can a status
# poll be shown to leave the System contract's one permission to clear alone.
(cd "$workspace" && west twister -j 2 -p native_sim/native/64 \
    -T "$repo/tests/native/mgmt_services" \
    -O "$repo/build/zephyr/twister-mgmt-services" --inline-logs \
    --extra-args="ZEPHYR_MODULES=$repo")

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
if failed != expected or 'pass = 13, fail = 13' not in log:
    raise RuntimeError(f'settings-file control differs: unexpected {sorted(failed - expected)}, '
                       f'missing {sorted(expected - failed)}')
print(f'settings-file control (unpatched backend): {len(failed)} of 26 cases fail as expected')
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
    ('twister-storage-file-roots', {'native_sim/native': 5, 'native_sim/native/64': 5}),
    ('twister-storage-credential', {'native_sim/native': 2, 'native_sim/native/64': 2}),
    ('twister-storage-sd-composition', {'native_sim/native': 8, 'native_sim/native/64': 8}),
    ('twister-storage-sd-probe', {'native_sim/native': 4, 'native_sim/native/64': 4}),
    ('twister-storage-sd-check', {'native_sim/native': 8, 'native_sim/native/64': 8}),
    ('twister-storage-sd-ops', {'native_sim/native': 4, 'native_sim/native/64': 4}),
    ('twister-storage-sd-interrupt', {'native_sim/native': 7, 'native_sim/native/64': 7}),
    ('twister-storage-sd-replace', {'native_sim/native': 7, 'native_sim/native/64': 7}),
    ('twister-storage-sd-settings-recover', {'native_sim/native': 13, 'native_sim/native/64': 13}),
    ('twister-configuration-software', {'native_sim/native/64': 2}),
    ('twister-mgmt-services', {'native_sim/native/64': 4}),
    ('twister-storage-configuration', {'native_sim/native/64': 11}),
    ('twister-storage-settings-file', {'native_sim/native/64': 26}),
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
