#!/usr/bin/env bash
# Issue #103 software-only feasibility probe. No target download, reset or erase.
set -euo pipefail
repo="$(git rev-parse --show-toplevel)"
workspace="${CRUX_WORKSPACE:-$(dirname "$repo")}"
export ZEPHYR_BASE="$workspace/zephyr"
out="$repo/build/zephyr/twister-spike-103"

# Pinned LittleFS + actual Zephyr UART and FS logging backends on native_sim.
littlefs="$(cd "$workspace" && west list littlefs -f '{abspath}')"
(cd "$workspace" && ZEPHYR_TOOLCHAIN_VARIANT=host west twister \
    -j 2 -p native_sim/native/64 \
    -T "$repo/tests/spike_logging_103/native_sim" \
    -O "$out" --inline-logs \
    --extra-args="ZEPHYR_MODULES=$littlefs")

python3 - "$out/twister.json" <<'PY'
import json
import sys
from pathlib import Path

suites = json.loads(Path(sys.argv[1]).read_text())["testsuites"]
if len(suites) != 1:
    raise SystemExit(f"SPIKE103: expected exactly one native_sim suite, got {len(suites)}")
suite = suites[0]
cases = suite.get("testcases", [])
if (suite.get("platform") != "native_sim/native/64" or
        suite.get("status") != "passed" or not suite.get("runnable") or
        len(cases) != 1 or any(case.get("status") != "passed" for case in cases)):
    raise SystemExit(f"SPIKE103: native backend runtime not fully executed/passed: {suite}")
print("SPIKE103: native_sim actual UART+FS backends 1/1 PASS")
PY

# RTT can be cross-built for the supported EVKB, but without a probe or board
# there is no RTT transfer, completeness or timing qualification.
mapfile -t modules < <(cd "$workspace" && west list cmsis cmsis_6 hal_nxp segger -f '{abspath}')
modules_text="$(IFS=';'; printf '%s' "${modules[*]}")"
rtt_build="$repo/build/zephyr/spike-103-rtt-evkb"
(cd "$workspace" && west build -b 'mimxrt1060_evk@B//qspi' \
    -d "$rtt_build" "$repo/tests/spike_logging_103/rtt_evkb" -- \
    -DZEPHYR_MODULES="$modules_text" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON)

config="$rtt_build/zephyr/.config"
for option in CONFIG_LOG_BACKEND_RTT CONFIG_USE_SEGGER_RTT \
              CONFIG_LOG_MODE_DEFERRED CONFIG_LOG_BACKEND_RTT_MODE_DROP; do
    grep -Fx "$option=y" "$config" >/dev/null || {
        echo "SPIKE103: resolved RTT config missing $option=y" >&2
        exit 1
    }
done
grep -Eq '^(# CONFIG_LOG_BACKEND_UART is not set|CONFIG_LOG_BACKEND_UART=n)
    echo "SPIKE103: RTT build unexpectedly enables UART logging" >&2
    exit 1
}
echo "SPIKE103: EVKB RTT cross-build PASS (no hardware output claimed)"
 "$config" || {
    echo "SPIKE103: RTT build unexpectedly enables UART logging" >&2
    exit 1
}
echo "SPIKE103: EVKB RTT cross-build PASS (no hardware output claimed)"
