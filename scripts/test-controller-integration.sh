#!/usr/bin/env bash
set -euo pipefail
if [[ "$(uname -s)" != Linux ]]; then
    printf 'Controller integration software checks require the Linux development environment.\n' >&2
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
python3 "$repo/tests/native/mdio_completion/test_driver.py"
(cd "$workspace" && west twister -j 2 -p native_sim/native/64 -T "$repo/tests/controller_m3" \
    -T "$repo/tests/controller_network" -T "$repo/tests/controller_network_stack" \
    -T "$repo/tests/controller_integrated" -T "$repo/tests/native/deferred_output" \
    -T "$repo/tests/native/network_output_burst" -T "$repo/tests/native/integrated_output" \
    -T "$repo/tests/native/logger_phase" \
    -O "$repo/build/zephyr/twister-m3" --inline-logs)
python3 "$repo/tests/native/deferred_output/verify_output.py" "$repo/build/zephyr/twister-m3"
python3 "$repo/tests/native/network_output_burst/verify_output.py" "$repo/build/zephyr/twister-m3"
python3 "$repo/tests/native/integrated_output/verify_output.py" "$repo/build/zephyr/twister-m3"
python3 "$repo/tests/native/logger_phase/verify_output.py" "$repo/build/zephyr/twister-m3"
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
# Keep the correction opt-in; retain the ordinary upstream-driver build above.
(cd "$workspace" && west build -p always -b 'mimxrt1060_evk@B//qspi' \
    -d "$repo/build/zephyr/m3-mdio-candidate-evkb" "$repo/samples/controller/zephyr" -- \
    -DZEPHYR_EXTRA_MODULES="$repo" \
    -DEXTRA_CONF_FILE="$repo/samples/controller/zephyr/m3-network.conf;$repo/samples/controller/zephyr/m3-mdio-completion.conf" \
    -DDTC_OVERLAY_FILE="$repo/samples/controller/zephyr/m3-network.overlay" \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON)
# A separate observation build must not replace the ordinary or corrected probes.
(cd "$workspace" && west build -p always -b 'mimxrt1060_evk@B//qspi' \
    -d "$repo/build/zephyr/m3-mdio-trace-evkb" "$repo/samples/controller/zephyr" -- \
    -DZEPHYR_EXTRA_MODULES="$repo" \
    -DEXTRA_CONF_FILE="$repo/samples/controller/zephyr/m3-network.conf;$repo/samples/controller/zephyr/m3-phy-diagnostic.conf;$repo/samples/controller/zephyr/m3-mdio-completion.conf;$repo/samples/controller/zephyr/m3-mdio-trace.conf" \
    -DDTC_OVERLAY_FILE="$repo/samples/controller/zephyr/m3-network.overlay" \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON)
# Deferred output remains a separate, unqualified startup diagnostic.
(cd "$workspace" && west build -p always -b 'mimxrt1060_evk@B//qspi' \
    -d "$repo/build/zephyr/m3-deferred-output-evkb" "$repo/samples/controller/zephyr" -- \
    -DZEPHYR_EXTRA_MODULES="$repo" \
    -DEXTRA_CONF_FILE="$repo/samples/controller/zephyr/m3-network.conf;$repo/samples/controller/zephyr/m3-phy-diagnostic.conf;$repo/samples/controller/zephyr/m3-mdio-completion.conf;$repo/samples/controller/zephyr/m3-mdio-trace.conf;$repo/samples/controller/zephyr/m3-deferred-output.conf" \
    -DDTC_OVERLAY_FILE="$repo/samples/controller/zephyr/m3-network.overlay" \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON)
# Full-network native output candidate, without startup or PHY diagnostic observers.
(cd "$workspace" && west build -p always -b 'mimxrt1060_evk@B//qspi' \
    -d "$repo/build/zephyr/m3-native-output-evkb" "$repo/samples/controller/zephyr" -- \
    -DZEPHYR_EXTRA_MODULES="$repo" \
    -DEXTRA_CONF_FILE="$repo/samples/controller/zephyr/m3-network.conf;$repo/samples/controller/zephyr/m3-mdio-completion.conf;$repo/samples/controller/zephyr/m3-native-output.conf" \
    -DDTC_OVERLAY_FILE="$repo/samples/controller/zephyr/m3-network.overlay" \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON)
for profile in integrated-probe integrated-normal; do
    # Keep the qualified client/MDIO path; integrated output has its own physical gate.
    configuration="$repo/samples/controller/zephyr/m3.conf;$repo/samples/controller/zephyr/m3-integrated.conf;$repo/samples/controller/zephyr/m3-mdio-completion.conf;$repo/samples/controller/zephyr/m3-native-output.conf"
    if [[ "$profile" == integrated-normal ]]; then
        configuration+=";$repo/samples/controller/zephyr/m3-normal.conf"
    fi
    (cd "$workspace" && west build -p always -b 'mimxrt1060_evk@B//qspi' \
        -d "$repo/build/zephyr/m3-$profile-evkb" "$repo/samples/controller/zephyr" -- \
        -DZEPHYR_EXTRA_MODULES="$repo" -DEXTRA_CONF_FILE="$configuration" \
        -DDTC_OVERLAY_FILE="$repo/samples/controller/zephyr/m3-network.overlay" \
        -DCMAKE_EXPORT_COMPILE_COMMANDS=ON)
done
python3 "$repo/scripts/record-build-provenance.py" --workspace "$workspace" --capture-inputs "$after"
cmp "$pending" "$after"
mv "$pending" "$inputs"
