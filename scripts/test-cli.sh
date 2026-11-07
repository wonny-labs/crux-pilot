#!/usr/bin/env bash
# Runs the command line's Host suites under the debug and sanitizer presets, its clang-tidy selection,
# its boundary and session-only checks and the shell rule's tests. With --zephyr, it instead checks the
# rule on real builds and compiles the serial wiring for the RT1060-EVKB. Nothing here flashes or
# resets a board, and no image built here runs a console.
set -euo pipefail

if [[ "$(uname -s)" != Linux ]]; then
    printf 'CLI checks are configured for Linux only; reopen this repository in the Dev Container.\n' >&2
    exit 2
fi

repo="$(git rev-parse --show-toplevel)"
export CRUX_CACHE_DIR="${CRUX_CACHE_DIR:-${repo}/../.crux-cache}"

if [[ "${1:-}" == --zephyr ]]; then
    workspace="${CRUX_WORKSPACE:-$(dirname "$repo")}"
    export ZEPHYR_BASE="$workspace/zephyr"
    CRUX_WORKSPACE="$workspace" CRUX_CLI_COMPOSITION_TESTS=1 \
        python3 "$repo/tests/build/test_cli_composition.py"
    exit 0
fi
if [[ $# -ne 0 ]]; then
    printf 'Usage: %s [--zephyr]\n' "$0" >&2
    exit 2
fi

tidy="${CLANG_TIDY:-clang-tidy-18}"
targets=(crux_cli_core crux_cli_console cli_parser cli_registry cli_rejection cli_permissions cli_login
    cli_login_real cli_ssh_shape cli_fuzz cli_console cli_console_pty cli_cpp_headers)

for preset in host-debug host-sanitize; do
    cmake --preset "$preset" -S "$repo" -DFETCHCONTENT_FULLY_DISCONNECTED=ON
    cmake --build --preset "$preset" --target "${targets[@]}"
    ctest --preset "$preset" -R '^Cli\.'
done

"$tidy" -p "$repo/build/linux/debug" \
    "$repo"/components/cli/src/*.c \
    "$repo"/components/cli/console/*.c \
    "$repo"/tests/cli/*.c

python3 "$repo/scripts/check-cli-boundary.py"
python3 "$repo/scripts/check-cli-session-commands.py"
python3 -m unittest discover -s "$repo/tests/build" -p 'test_management_shell.py' -v
