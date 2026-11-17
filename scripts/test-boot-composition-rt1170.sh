#!/usr/bin/env bash
# Build, sign and check the RT1170-EVK counterpart of the boot composition of
# docs/boot-composition.md: MCUboot RAM load with revert and an application, on the RT1170-EVK
# (`mimxrt1170_evk@A//cm7`). Build only: nothing is programmed, booted, debugged or reset, and no
# key is generated or stored. The image is signed with MCUboot's published public test key, which
# sysbuild uses when no key argument is given. Images signed with it must never be programmed to
# a board.
#
# STATUS: the board files under zephyr/boot/boards/mimxrt1170_evk_mimxrt1176_cm7_A hold STARTING
# VALUES from the RT1170 design note "RT1170-EVK boot composition: memory-map choices", approved
# as recommended by the project owner on 15 November 2026 and to be corrected by the first CI
# build. The script runs in its own step of the runtime shard of the Zephyr
# workflow, separate from the RT1060 step (open point D3). It has not passed yet, so the
# composition is Not verified by build, and the first run may fail on the memory map; that is
# information for correcting the starting values.
#
# OPEN POINT D2, the application: the controller sample samples/controller/zephyr has no RT1170
# board configuration (samples/controller/zephyr/boards holds only RT1060 and native_sim files).
# This script builds that sample, passes no board configuration and does not change the sample.
# Whether the sample configures and builds for the RT1170 under sysbuild is Not verified. If it
# needs more than a board .conf, the application directory can be replaced without editing this
# script: set CRUX_BOOT_RT1170_APP to the application directory (default: the controller sample).
# The image name that sysbuild gives the application is the base name of that directory.
#
# Run outside CI it changes the local workspace: it adds +crux-boot to manifest.group-filter and
# pip-installs the MCUboot requirements into $CRUX_PYTHON (python3 by default).
#
# It reuses the structure and every check of scripts/test-boot-composition.sh (the RT1060-EVK
# Rev B check, which this script leaves untouched): the group route (no -DEXTRA_ZEPHYR_MODULES
# for MCUboot) and no key argument. The M7 reference composition is not touched.
set -euo pipefail

if [[ "$(uname -s)" != Linux ]]; then
    printf 'The boot composition check needs the Linux development environment.\n' >&2
    exit 2
fi

repo="$(git rev-parse --show-toplevel)"
workspace="${CRUX_WORKSPACE:-$(dirname "$repo")}"
python="${CRUX_PYTHON:-python3}"
board='mimxrt1170_evk@A//cm7'
files="$repo/zephyr/boot/boards/mimxrt1170_evk_mimxrt1176_cm7_A"
app="${CRUX_BOOT_RT1170_APP:-$repo/samples/controller/zephyr}"
build="$repo/build/zephyr/boot-composition-rt1170"
mcuboot="$workspace/bootloader/mcuboot"
public_key="$mcuboot/root-ec-p256.pem"

fail() {
    printf 'RT1170 boot composition check failed: %s\n' "$1" >&2
    exit 1
}

if [[ ! -f "$workspace/.west/config" || ! -d "$workspace/zephyr" ]]; then
    printf 'Run scripts/bootstrap-zephyr.sh before the boot composition check.\n' >&2
    exit 2
fi
[[ -d "$app" ]] || fail "the application directory does not exist: $app"
# West runs from the workspace, so a relative path is made absolute here, which also keeps a
# value such as "." from becoming the image name.
app="$(CDPATH= cd -- "$app" && pwd)"
image="$(basename "$app")"
for name in mcuboot.conf mcuboot.overlay app.conf app.overlay; do
    [[ -f "$files/$name" ]] || fail "$files/$name is missing"
done

# MCUboot is the plain project in the optional group crux-boot. This enables the group the way a
# workspace owner would, keeping a filter that is already set, and fetches only that project.
filter="$(cd "$workspace" && west config manifest.group-filter 2>/dev/null || true)"
if [[ ",$filter," != *",+crux-boot,"* ]]; then
    (cd "$workspace" && west config manifest.group-filter -- "${filter:+$filter,}+crux-boot")
fi
(cd "$workspace" && west update mcuboot)
[[ -f "$public_key" ]] || fail "the public test key is not in the MCUboot checkout: $public_key"

# imgtool imports packages that the Zephyr requirements do not list. The MCUboot checkout names
# them for its own revision.
"$python" -m pip install --disable-pip-version-check --no-cache-dir \
    -r "$mcuboot/scripts/requirements.txt"

export ZEPHYR_BASE="$workspace/zephyr"
(cd "$workspace" && west build --sysbuild -p always -b "$board" -d "$build" "$app" -- \
    -DZEPHYR_EXTRA_MODULES="$repo" \
    -DPython3_EXECUTABLE="$(command -v "$python")" \
    -DSB_CONFIG_BOOTLOADER_MCUBOOT=y \
    -DSB_CONFIG_MCUBOOT_MODE_RAM_LOAD_WITH_REVERT=y \
    -DSB_CONFIG_BOOT_SIGNATURE_TYPE_ECDSA_P256=y \
    "-Dmcuboot_EXTRA_CONF_FILE=$files/mcuboot.conf" \
    "-Dmcuboot_EXTRA_DTC_OVERLAY_FILE=$files/mcuboot.overlay" \
    "-D${image}_EXTRA_CONF_FILE=$files/app.conf" \
    "-D${image}_EXTRA_DTC_OVERLAY_FILE=$files/app.overlay")

# The bootloader is configured as the page says.
config="$build/mcuboot/zephyr/.config"
[[ -f "$config" ]] || fail "no MCUboot configuration at $config"
for line in \
    CONFIG_BOOT_RAM_LOAD=y \
    CONFIG_BOOT_RAM_LOAD_REVERT=y \
    CONFIG_BOOT_SIGNATURE_TYPE_ECDSA_P256=y \
    CONFIG_BOOT_IMAGE_EXECUTABLE_RAM_START=0x80000000 \
    CONFIG_BOOT_IMAGE_EXECUTABLE_RAM_SIZE=67108864; do
    grep -qx "$line" "$config" || fail "$line is not set in $config"
done
[[ -f "$build/mcuboot/zephyr/zephyr.bin" ]] || fail 'the build did not produce the MCUboot image'

# The application is signed, and the signature verifies with the public test key.
signed="$(find "$build/$image" -name zephyr.signed.bin -print -quit)"
[[ -n "$signed" && -f "$signed" ]] || fail 'the build did not produce zephyr.signed.bin'
"$python" "$mcuboot/scripts/imgtool.py" verify -k "$public_key" "$signed" ||
    fail 'the signed image does not verify with the public test key'

# The tool is discriminating. A key of another type and images changed after signing are
# refused. A refusal must be a plain verification failure, not a crash of the tool, so a
# traceback does not count. This checks the signing tool on the build output, not a boot result.
imgtool=("$python" "$mcuboot/scripts/imgtool.py")
expect_refused() {
    local output
    if output="$("${imgtool[@]}" verify -k "$1" "$2" 2>&1)"; then
        fail "$3"
    fi
    if grep -q Traceback <<<"$output"; then
        fail "imgtool crashed instead of refusing: $3"
    fi
}
other_key="$mcuboot/root-rsa-2048.pem"
[[ -f "$other_key" ]] || fail "the other test key is not in the MCUboot checkout: $other_key"
expect_refused "$other_key" "$signed" 'the signed image verified with a key of another type'
# 0x300 is in the header padding, 0x400 + 0x100 is in the payload.
for offset in 0x300 0x500; do
    modified="$build/zephyr.signed.modified.bin"
    "$python" - "$signed" "$modified" "$offset" <<'PY'
import sys
data = bytearray(open(sys.argv[1], "rb").read())
data[int(sys.argv[3], 16)] ^= 0xFF
open(sys.argv[2], "wb").write(data)
PY
    expect_refused "$public_key" "$modified" "an image changed after signing at $offset still verified"
    rm -f "$modified"
done

# The application's own files reached its configuration, not only the bootloader's.
app_config="$build/$image/zephyr/.config"
[[ -f "$app_config" ]] || fail "no application configuration at $app_config"
grep -qx CONFIG_RETENTION_BOOTLOADER_INFO_TYPE_MCUBOOT=y "$app_config" ||
    fail 'the application configuration lacks the bootloader information type'
[[ -s "$build/mcuboot/zephyr/zephyr.bin" ]] || fail 'the MCUboot image is empty'

printf 'RT1170 boot composition: MCUboot %s bytes, signed application %s bytes (build only, not booted).\n' \
    "$(stat -c %s "$build/mcuboot/zephyr/zephyr.bin")" "$(stat -c %s "$signed")"
