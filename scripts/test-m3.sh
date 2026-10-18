#!/usr/bin/env bash
set -euo pipefail
if [[ "$(uname -s)" != Linux ]]; then
    printf 'M3 software checks require the Linux development environment.\n' >&2
    exit 2
fi
repo="$(git rev-parse --show-toplevel)"
workspace="${CRUX_WORKSPACE:-$(dirname "$repo")}"
export ZEPHYR_BASE="$workspace/zephyr"
inputs="$repo/build/evidence/m3-inputs.json"
pending="$repo/build/evidence/m3-inputs.pending.json"
after="$repo/build/evidence/m3-inputs.after.json"
rm -f "$inputs" "$pending" "$after"
python3 "$repo/scripts/record-build-provenance.py" --workspace "$workspace" --capture-inputs "$pending"
(cd "$workspace" && west twister -j 2 -p native_sim/native/64 -T "$repo/tests/controller_m3" \
    -T "$repo/tests/controller_network" -T "$repo/tests/controller_network_stack" \
    -O "$repo/build/zephyr/twister-m3" --inline-logs)
for profile in probe normal; do
    configuration="$repo/samples/controller/zephyr/m3.conf"
    if [[ "$profile" == normal ]]; then
        configuration+=";$repo/samples/controller/zephyr/m3-normal.conf"
    fi
    (cd "$workspace" && west build -p always -b 'mimxrt1060_evk@B//qspi' \
        -d "$repo/build/zephyr/m3-$profile-evkb" "$repo/samples/controller/zephyr" -- \
        -DZEPHYR_EXTRA_MODULES="$repo" -DEXTRA_CONF_FILE="$configuration" \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON)
done
(cd "$workspace" && west build -p always -b 'mimxrt1060_evk@B//qspi' \
    -d "$repo/build/zephyr/m3-network-evkb" "$repo/samples/controller/zephyr" -- \
    -DZEPHYR_EXTRA_MODULES="$repo" \
    -DEXTRA_CONF_FILE="$repo/samples/controller/zephyr/m3-network.conf" \
    -DDTC_OVERLAY_FILE="$repo/samples/controller/zephyr/m3-network.overlay" \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON)
python3 "$repo/scripts/record-build-provenance.py" --workspace "$workspace" --capture-inputs "$after"
cmp "$pending" "$after"
mv "$pending" "$inputs"
