#!/usr/bin/env bash
# The command line (Issue #193) on the wolfSSH provider (Issue #192): on native_sim against a real OpenSSH
# client, then the shell rule's check on that image. --target compiles the same image for RT1060-EVKB, and
# never flashes, resets or runs one. It needs a workspace that enabled the optional stacks:
#   west config manifest.group-filter -- +crux-secure-management && west update
# No default workflow runs this, because no default workflow selects those stacks. See docs/cli-over-ssh.md.
set -euo pipefail

if [[ "$(uname -s)" != Linux ]]; then
    printf 'Zephyr builds are configured for Linux only; reopen this repository in the Dev Container.\n' >&2
    exit 2
fi

repo="$(git rev-parse --show-toplevel)"
workspace="${CRUX_WORKSPACE:-$(dirname "$repo")}"
output="${CRUX_SSH_CLI_OUTPUT:-$repo/build/zephyr/ssh-cli}"
jobs="${CRUX_SSH_CLI_JOBS:-4}"
app="$repo/tests/ssh/cli/openssh"
provider_test="$repo/tests/zephyr/ssh_provider"
export ZEPHYR_BASE="$workspace/zephyr"

run_native=true
run_target=false
for option in "$@"; do
    case "$option" in
        --target) run_target=true; run_native=false ;;
        --all) run_target=true ;;
        *) printf 'Usage: %s [--target|--all]\n' "$0" >&2; exit 2 ;;
    esac
done

[[ -e "$provider_test/openssh_test.py" ]] ||
    { printf 'This needs the wolfSSH provider of #192, which is not in this tree.\n' >&2; exit 1; }

if [[ "$run_native" == true ]]; then
    west build -p auto -b native_sim/native/64 -d "$output/native" "-o=-j$jobs" "$app" -- \
        "-DEXTRA_CONF_FILE=$provider_test/native_sim.conf"
    python3 "$repo/scripts/check-management-shell.py" --config "$output/native/zephyr/.config" \
        --elf "$output/native/zephyr/zephyr.elf"
    python3 "$app/openssh_test.py" "$output/native/zephyr/zephyr.exe" | tee "$output/openssh.log"
    tail -n 1 "$output/openssh.log" | grep -q '^All cases passed\.$'
fi

if [[ "$run_target" == true ]]; then
    west build -p auto -b 'mimxrt1060_evk@B//qspi' -d "$output/target" "-o=-j$jobs" "$app" -- \
        "-DEXTRA_CONF_FILE=$provider_test/rt1060.conf" 2>&1 | tee "$output/target.log"
    grep -E '^ +(FLASH|RAM|SRAM|DTCM|ITCM|OCRAM|SDRAM):' "$output/target.log" || true
    python3 "$repo/scripts/check-management-shell.py" --config "$output/target/zephyr/.config" \
        --elf "$output/target/zephyr/zephyr.elf" \
        --nm "$ZEPHYR_SDK_INSTALL_DIR/gnu/arm-zephyr-eabi/bin/arm-zephyr-eabi-nm"
fi

printf '%s\n' 'CLI over SSH checks finished. native_sim and compile results only. No board was operated.'
