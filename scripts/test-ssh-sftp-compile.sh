#!/usr/bin/env bash
# Compile-only evidence for SFTP over the file service (Issue #194). It compiles the real operations and
# the wolfSSH port for the target and for native_sim, with Crux's warning policy. It links no image, flashes
# nothing and runs nothing, because no composition holds the file service and the provider yet.
# Run scripts/test-ssh-wolf-settings.sh --target --native first: this uses the patched Zephyr it prepares.
set -euo pipefail

if [[ "$(uname -s)" != Linux ]]; then
    printf 'Zephyr builds are configured for Linux only; reopen this repository in the Dev Container.\n' >&2
    exit 2
fi

repo="$(git rev-parse --show-toplevel)"
workspace="${CRUX_WORKSPACE:-$(dirname "$repo")}"
settings_output="${CRUX_SSH_WOLF_OUTPUT:-$repo/build/zephyr/ssh-wolf-settings}"
patched_zephyr="${CRUX_SSH_ZEPHYR_WOLF:-$settings_output/zephyr-wolf}"
output="${CRUX_SSH_SFTP_COMPILE_OUTPUT:-$repo/build/zephyr/ssh-sftp-compile}"
jobs="${CRUX_SSH_SFTP_JOBS:-3}"
probe="$repo/tests/zephyr/ssh_wolf_probe"
modules="$workspace/modules"
module_list="$modules/hal/cmsis;$modules/hal/cmsis_6;$modules/hal/nxp;$modules/crypto/wolfssl;$modules/crypto/wolfssh"

compile() { # directory, Zephyr tree, board, extra arguments
    local directory="$1" tree="$2" board="$3"
    shift 3
    ZEPHYR_BASE="$tree" cmake -S "$probe" -B "$directory" -G Ninja "-DBOARD=$board" \
        "-DZEPHYR_MODULES=$module_list" "-DPython3_EXECUTABLE=$(command -v python3)" \
        -DCRUX_SSH_SFTP_FILE_SERVICE=ON "$@" > "$directory.configure.log"
    ninja -C "$directory" -j "$jobs" crux_ssh_entries crux_ssh_sftp_port | tail -1
    # The object of the real operations, not the refusing ones.
    find "$directory" -name 'crux_ssh_sftp.c.obj' | grep -q . ||
        { printf 'The real operations were not compiled in %s.\n' "$directory" >&2; exit 1; }
    if find "$directory" -name 'crux_ssh_sftp_refuse.c.obj' | grep -q .; then
        printf 'The refusing operations were compiled in %s.\n' "$directory" >&2
        exit 1
    fi
    printf 'Compiled, not linked: %s.\n' "$board"
}

mkdir -p "$output"
[[ -d "$patched_zephyr" ]] || { printf 'Run scripts/test-ssh-wolf-settings.sh --target first.\n' >&2; exit 2; }
compile "$output/target" "$patched_zephyr" 'mimxrt1060_evk@B//qspi'
compile "$output/native" "$workspace/zephyr" 'native_sim/native/64' "-DEXTRA_CONF_FILE=$probe/native_sim.conf"
