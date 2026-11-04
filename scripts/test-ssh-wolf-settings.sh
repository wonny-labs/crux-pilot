#!/usr/bin/env bash
# Compile-only evidence for the wolfSSL and wolfSSH settings arrangement (Issue #205). It builds images
# and never flashes, resets or runs one. It needs a workspace that enabled the optional stacks:
#   west config manifest.group-filter -- +crux-secure-management && west update
# No default workflow runs this. See docs/ssh-provider-contract.md.
set -euo pipefail

usage() {
    printf 'Usage: %s [--target] [--native] [--controls]\nWith no option, runs all three.\n' "$0" >&2
    exit 2
}

run_target=false
run_native=false
run_controls=false
for option in "$@"; do
    case "$option" in
        --target) run_target=true ;;
        --native) run_native=true ;;
        --controls) run_controls=true ;;
        *) usage ;;
    esac
done
if [[ $# -eq 0 ]]; then
    run_target=true
    run_native=true
    run_controls=true
fi

if [[ "$(uname -s)" != Linux ]]; then
    printf 'Zephyr builds are configured for Linux only; reopen this repository in the Dev Container.\n' >&2
    exit 2
fi

repo="$(git rev-parse --show-toplevel)"
workspace="${CRUX_WORKSPACE:-$(dirname "$repo")}"
output="${CRUX_SSH_WOLF_OUTPUT:-$repo/build/zephyr/ssh-wolf-settings}"
jobs="${CRUX_SSH_WOLF_JOBS:-3}"
probe="$repo/tests/zephyr/ssh_wolf_probe"
board='mimxrt1060_evk@B//qspi'
native_board='native_sim/native/64'
zephyr_revision=dccb09599635bdff17633fa7e9dab014b91dce90

# The wolfSSL Zephyr socket patch is fetched by revision and checked: it is not stored here. See
# patches/zephyr/README.md. merged_tree is the Git tree that a 3-way merge of it onto Zephyr v4.4.2 gives.
patch_url=https://raw.githubusercontent.com/wolfSSL/osp/8c32b060ac6017b1291a6da0875a48637bfb682d/zephyr/4.4/zephyr-tls-4.4.1.patch
patch_sha256=3d1cc0307bbeea5f000698bca5be717f73fdb8c47e90083b003336364e8fdbda
merged_tree=1d00f38c0d029156fb1d34a109fe41a6a7006312

modules="$workspace/modules"
module_list="$modules/hal/cmsis;$modules/hal/cmsis_6;$modules/hal/nxp;$modules/crypto/wolfssl;$modules/crypto/wolfssh"

fail() {
    printf '%s\n' "$*" >&2
    exit 1
}

require_module() { # name, path, revision
    if [[ ! -d "$2/.git" && ! -f "$2/.git" ]]; then
        printf '%s is missing from %s. Enable the optional stacks: west config manifest.group-filter -- +crux-secure-management && west update\n' \
            "$1" "$2" >&2
        exit 2
    fi
    [[ "$(git -C "$2" rev-parse HEAD)" == "$3" ]] || fail "$1 is not at the pinned revision $3"
}

# The proof that no vendor file was edited: the modules are clean at their pins, before and after.
check_modules_clean() {
    local module
    for module in wolfssl wolfssh; do
        [[ -z "$(git -C "$modules/crypto/$module" status --porcelain)" ]] ||
            fail "modules/crypto/$module has changes, and this arrangement must not edit a vendor file"
    done
}

pinned_revision() { # project name in west.yml
    python3 - "$repo/west.yml" "$1" <<'PY'
import re
import sys

text = open(sys.argv[1]).read().split("projects:", 1)[1]
match = re.search(r"- name: " + re.escape(sys.argv[2]) + r"\n(?:\s+\S.*\n)*?\s+revision: ([0-9a-f]{40})", text)
print(match.group(1))
PY
}

require_module wolfSSL "$modules/crypto/wolfssl" "$(pinned_revision wolfssl)"
require_module wolfSSH "$modules/crypto/wolfssh" "$(pinned_revision wolfssh)"
check_modules_clean
mkdir -p "$output"

# A separate copy of Zephyr with the socket patch, so the workspace checkout is never edited. How a real
# composition applies the patch stays with Issue #185.
prepare_patched_zephyr() {
    local tree="${CRUX_SSH_ZEPHYR_WOLF:-$output/zephyr-wolf}" patch="$output/zephyr-tls-4.4.1.patch"
    if [[ ! -d "$tree/.git" ]]; then
        if [[ ! -f "$patch" ]]; then
            curl -fsSL -o "$patch" "$patch_url"
        fi
        printf '%s  %s\n' "$patch_sha256" "$patch" | sha256sum --check --quiet
        git clone -q --shared --no-checkout "$workspace/zephyr" "$tree"
        git -C "$tree" config core.sparseCheckout true
        git -C "$tree" config core.sparseCheckoutCone false
        printf '/*\n!/doc/\n!/tests/\n!/samples/\n' > "$tree/.git/info/sparse-checkout"
        git -C "$tree" checkout -q --detach "$zephyr_revision"
        git -C "$tree" apply --3way "$patch"
    fi
    [[ "$(git -C "$tree" rev-parse HEAD)" == "$zephyr_revision" ]] || fail "$tree is not on the pinned Zephyr"
    [[ "$(git -C "$tree" write-tree)" == "$merged_tree" ]] || fail "$tree does not hold the expected merge"
    patched_zephyr="$tree"
}

configure() { # build directory, Zephyr tree, board, extra arguments
    local directory="$1" tree="$2" selected="$3"
    shift 3
    ZEPHYR_BASE="$tree" cmake -S "$probe" -B "$directory" -G Ninja -DBOARD="$selected" \
        "-DZEPHYR_MODULES=$module_list" "-DPython3_EXECUTABLE=$(command -v python3)" "$@"
}

symbols() { # nm, ELF
    "$1" "$2" | awk '{print $NF}'
}

require_symbol() { # symbol list file, name
    grep -qx "$2" "$1" || fail "expected symbol $2 is missing from the image"
}

reject_symbol() { # symbol list file, pattern
    if grep -Eq "$2" "$1"; then
        fail "image holds a symbol that the arrangement must keep out: $(grep -E "$2" "$1" | head -3 | tr '\n' ' ')"
    fi
}

require_config() { # .config, line
    grep -qx "$2" "$1" || fail "expected $2 in $1"
}

reject_config() { # .config, setting
    if grep -q "^$2=" "$1"; then
        fail "$2 must be unset in $1"
    fi
}

# Checks shared by each image with the SFTP user file system path. A linker drops unused file code, so
# what wolfSSL and wolfSSH compiled in is read from their libraries, not only from the image.
check_common_image() { # nm, build directory, symbol list
    local config="$2/zephyr/.config" compiled="$3.compiled"
    require_config "$config" 'CONFIG_WOLFSSL=y'
    require_config "$config" 'CONFIG_WOLFSSH=y'
    require_config "$config" 'CONFIG_WOLFSSH_SETTINGS_FILE="crux_wolfssh_settings.h"'
    require_config "$config" 'CONFIG_WOLFSSL_SETTINGS_FILE=""'
    reject_config "$config" CONFIG_MBEDTLS
    # wolfSSH server and SFTP, through the file port, the table, and the algorithms SSH needs.
    for symbol in wolfSSH_accept wolfSSH_SFTP_read wolfSSH_SetFilesystemHandle SFTP_GetAttributes \
        crux_ssh_callback_table crux_ssh_cli_shell wc_ed25519_verify_msg wc_ecc_verify_hash wc_AesCtrEncrypt; do
        require_symbol "$3" "$symbol"
    done
    "$1" --defined-only "$2"/modules/wolfssl/*.a "$2"/app/libapp.a 2>/dev/null | awk 'NF == 3 {print $3}' > "$compiled"
    # wolfSSL keeps NO_FILESYSTEM, and wolfSSH's direct Zephyr file code stays out.
    reject_symbol "$compiled" '^(wolfSSL_CTX_load_verify_locations|wolfSSL_CTX_use_certificate_file|wc_ReadDirFirst|wc_FileLoad)$'
    reject_symbol "$compiled" '^(wssh_z_|wolfSSH_ReadKey_file$)'
    reject_symbol "$3" '^fs_(open|stat|opendir)$'
    # SCP and the Mbed TLS library stay out of the image.
    reject_symbol "$compiled" '^(wolfSSH_SCP|wsScp)'
    reject_symbol "$3" '^mbedtls_'
}

build_target_image() {
    local directory="$output/target"
    prepare_patched_zephyr
    configure "$directory" "$patched_zephyr" "$board"
    ninja -C "$directory" -j "$jobs" | tee "$directory.build.log" | tail -4
    grep -E '^ *(FLASH|RAM):' "$directory.build.log" | tee "$output/target-memory.txt"
    local nm="$ZEPHYR_SDK_INSTALL_DIR/gnu/arm-zephyr-eabi/bin/arm-zephyr-eabi-nm"
    symbols "$nm" "$directory/zephyr/zephyr.elf" > "$output/target-symbols.txt"
    check_common_image "$nm" "$directory" "$output/target-symbols.txt"
    require_config "$directory/zephyr/.config" 'CONFIG_NET_SOCKETS_SOCKOPT_TLS=y'
    # The TLS layer that the socket backend calls, sharing the same wolfSSL.
    for symbol in wolfSSL_CTX_new wolfSSL_read wolfSSL_write; do
        require_symbol "$output/target-symbols.txt" "$symbol"
    done
    # wolfSSL keeps NO_FILESYSTEM and wolfSSH does not: nothing they share may depend on it.
    python3 "$repo/scripts/check-ssh-wolf-headers.py" "$directory"
    printf 'Target compile result (not flashed): %s combined image, with the socket backend.\n' "$board"
}

build_native_image() {
    local directory="$output/native"
    # No socket patch here, so the workspace's own unpatched Zephyr is enough.
    configure "$directory" "$workspace/zephyr" "$native_board" "-DEXTRA_CONF_FILE=$probe/native_sim.conf"
    ninja -C "$directory" -j "$jobs" | tee "$directory.build.log" | tail -4
    symbols nm "$directory/zephyr/zephyr.exe" > "$output/native-symbols.txt"
    check_common_image nm "$directory" "$output/native-symbols.txt"
    reject_config "$directory/zephyr/.config" CONFIG_NET_SOCKETS_SOCKOPT_TLS
    reject_config "$directory/zephyr/.config" CONFIG_POSIX_API
    require_config "$directory/zephyr/.config" 'CONFIG_NET_NATIVE_OFFLOADED_SOCKETS=y'
    reject_config "$directory/zephyr/.config" CONFIG_ETH_NATIVE_TAP
    printf 'native_sim build result (not run): %s, wolfSSH without the socket backend.\n' "$native_board"
}

# The combined image also builds on native_sim. No OpenSSH test depends on it yet.
build_native_combined_image() {
    local directory="$output/native-combined"
    prepare_patched_zephyr
    configure "$directory" "$patched_zephyr" "$native_board" "-DEXTRA_CONF_FILE=$probe/native_sim_combined.conf"
    ninja -C "$directory" -j "$jobs" | tee "$directory.build.log" | tail -4
    symbols nm "$directory/zephyr/zephyr.exe" > "$output/native-combined-symbols.txt"
    check_common_image nm "$directory" "$output/native-combined-symbols.txt"
    require_config "$directory/zephyr/.config" 'CONFIG_NET_SOCKETS_SOCKOPT_TLS=y'
    require_symbol "$output/native-combined-symbols.txt" wolfSSL_CTX_new
    printf 'native_sim build result (not run): %s, wolfSSH with the socket backend.\n' "$native_board"
}

# Each control shows that a part of the arrangement bears weight. A control that builds is a failure.
run_controls() {
    local directory
    prepare_patched_zephyr

    # The TLS socket backend must refuse the settings file that would otherwise be the plain answer.
    directory="$output/control-settings-file"
    printf 'CONFIG_WOLFSSL_SETTINGS_FILE="crux_wolfssh_settings.h"\n' > "$output/control-settings-file.conf"
    if configure "$directory" "$patched_zephyr" "$board" \
        "-DEXTRA_CONF_FILE=$output/control-settings-file.conf" > "$directory.log" 2>&1; then
        fail 'Control failed: the socket backend accepted CONFIG_WOLFSSL_SETTINGS_FILE'
    fi
    grep -q 'WOLFSSL_ALWAYS_VERIFY_CB' "$directory.log" && grep -q 'Aborting due to Kconfig warnings' "$directory.log" ||
        fail "Control failed for another reason: see $directory.log"
    printf 'Control passed: the socket backend rejects CONFIG_WOLFSSL_SETTINGS_FILE.\n'

    # Without the wolfSSH settings file, wolfSSH sees NO_FILESYSTEM and the probe stops at its check.
    directory="$output/control-no-wolfssh-settings"
    printf 'CONFIG_WOLFSSH_SETTINGS_FILE=""\n' > "$output/control-no-wolfssh-settings.conf"
    configure "$directory" "$patched_zephyr" "$board" \
        "-DEXTRA_CONF_FILE=$output/control-no-wolfssh-settings.conf" > "$directory.log" 2>&1
    if ninja -C "$directory" -j "$jobs" CMakeFiles/app.dir/src/main.c.obj >> "$directory.log" 2>&1; then
        fail 'Control failed: wolfSSH compiled without its settings file'
    fi
    grep -q 'wolfSSH must not see NO_FILESYSTEM' "$directory.log" ||
        fail "Control failed for another reason: see $directory.log"
    printf 'Control passed: without the settings file, wolfSSH sees NO_FILESYSTEM.\n'
}

if [[ "$run_target" == true ]]; then
    build_target_image
fi
if [[ "$run_native" == true ]]; then
    build_native_image
    build_native_combined_image
fi
if [[ "$run_controls" == true ]]; then
    run_controls
fi
check_modules_clean
printf 'The wolfSSL and wolfSSH modules are unedited at their pins.\n'
