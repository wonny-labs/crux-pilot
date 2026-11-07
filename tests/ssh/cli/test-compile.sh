#!/usr/bin/env bash
# Compiles the real SSH entry points of the command line (Issue #193) for native_sim and for the RT1060-EVKB,
# with no wolfSSH, and checks the flags that keep a session's context the size the CLI core was built with. It
# also holds the list of Zephyr shell commands that the Host tests use to what this Zephyr registers.
# It never flashes, resets or runs an image. tests/ssh/cli/test-openssh.sh is the run on a real provider.
set -euo pipefail

if [[ "$(uname -s)" != Linux ]]; then
    printf 'Zephyr builds are configured for Linux only; reopen this repository in the Dev Container.\n' >&2
    exit 2
fi

repo="$(git rev-parse --show-toplevel)"
workspace="${CRUX_WORKSPACE:-$(dirname "$repo")}"
output="${CRUX_SSH_CLI_OUTPUT:-$repo/build/zephyr/ssh-cli-compile}"
app="$repo/tests/ssh/cli/compile"
export ZEPHYR_BASE="$workspace/zephyr"

check_build() { # build directory
    python3 - "$1/compile_commands.json" <<'CHECK'
import json
import sys

commands = [entry["command"] for entry in json.load(open(sys.argv[1]))
            if entry["file"].endswith("components/ssh/cli/crux_ssh_cli.c")]
if len(commands) != 1:
    sys.exit(f"crux_ssh_cli.c was compiled {len(commands)} times")
for flag in ("-DCRUX_CLI_LINE_MAX=400", "-DCRUX_CLI_TOKENS_MAX=12"):
    if flag not in commands[0]:
        sys.exit(f"crux_ssh_cli.c was compiled without {flag}")
CHECK
    find "$1" -name 'crux_ssh_cli.c.obj' | grep -q . ||
        { printf 'No object for the entry points in %s\n' "$1" >&2; exit 1; }
    python3 "$repo/scripts/check-management-shell.py" --config "$1/zephyr/.config"
}

python3 "$repo/tests/ssh/cli/check_zephyr_shell_lines.py" --zephyr "$ZEPHYR_BASE"

for board in native_sim/native/64 'mimxrt1060_evk@B//qspi'; do
    build="$output/${board%%/*}"
    west build -p auto -b "$board" -d "$build" "$app"
    check_build "$build"
done
printf '%s\n' 'Compile results only. No board was operated.'
