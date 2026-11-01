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
board='mimxrt1060_evk@B/mimxrt1062/qspi'
output="${CRUX_FILE_RT1060_OUTPUT:-$repo/build/zephyr/twister-file-rt1060}"

# Compile only: the File sources and the RT1060 FatFs/SDHC composition become an unlinked object.
# Nothing is linked into a runnable File provider and no board is programmed or reset.
(cd "$workspace" && west twister -p "$board" -T "$repo/tests/zephyr/file_fatfs_compile" \
    --build-only -O "$output" --inline-logs)

# A filtered testcase exits successfully with nothing built; require the configuration to build.
python3 - "$output/twister.json" "$board" <<'PY'
import json
import sys

suites = json.load(open(sys.argv[1]))['testsuites']
# A build-only testcase that built reports "not run"; filtered or failed ones report otherwise.
if (len(suites) != 1 or suites[0]['platform'] != sys.argv[2] or
        suites[0].get('status') != 'not run' or suites[0].get('runnable')):
    raise RuntimeError(f'RT1060 File compile check did not build on {sys.argv[2]}: '
                       f"{[(s['platform'], s.get('status')) for s in suites]}")
print(f"RT1060 File compile check built on {sys.argv[2]}: {suites[0]['name']}")
PY
