#!/usr/bin/env bash
set -euo pipefail

if [[ "$(uname -s)" != Linux ]]; then
    printf 'Zephyr firmware builds are configured for Linux only; reopen this repository in the Dev Container.\n' >&2
    exit 2
fi

usage() {
    printf 'Usage: %s <validated-board-target> [--app <application-dir>] [--debug-optimizations]\n' "$0" >&2
    exit 2
}

if [[ $# -lt 1 || -z "${1:-}" || "$1" == -* ]]; then
    usage
fi

repo="$(git rev-parse --show-toplevel)"
workspace="${CRUX_WORKSPACE:-$(dirname "$repo")}"
board="$1"
shift
app="$repo/samples/time"
custom_app=0
debug_optimizations=0
while [[ $# -gt 0 ]]; do
    case "$1" in
        --app)
            [[ $# -ge 2 && -n "$2" ]] || usage
            app="$2"
            custom_app=1
            shift 2
            ;;
        --debug-optimizations)
            debug_optimizations=1
            shift
            ;;
        *)
            usage
            ;;
    esac
done

if [[ "$custom_app" -eq 1 ]]; then
    if [[ ! -f "$app/CMakeLists.txt" ]]; then
        printf 'No CMakeLists.txt in the application directory: %s\n' "$app" >&2
        exit 2
    fi
    app="$(cd "$app" && pwd -P)"
fi

zephyr="$workspace/zephyr"
sanitize() { printf '%s' "$1" | sed -E 's/[^A-Za-z0-9_.-]+/_/g'; }
# With neither option the build directory is the one that the VS Code profile and the
# clean task name. --app and --debug-optimizations each add a part to the name, so a
# reused directory never carries a stale choice.
build_key="$(sanitize "$board")"
if [[ "$custom_app" -eq 1 ]]; then
    repo_real="$(cd "$repo" && pwd -P)"
    case "$app" in
        "$repo_real"/*) app_key="$(sanitize "${app#"$repo_real"/}")" ;;
        *) app_key="$(sanitize "$app")" ;;
    esac
    build_key="$build_key--$app_key"
fi
if [[ "$debug_optimizations" -eq 1 ]]; then
    build_key="$build_key--og"
fi
build_dir="$repo/build/zephyr/$build_key"

if [[ ! -f "$workspace/.west/config" || ! -f "$zephyr/zephyr-env.sh" ]]; then
    printf 'Run scripts/bootstrap-zephyr.sh before building firmware.\n' >&2
    exit 2
fi

printf 'Incremental board build: %s\n' "$board"
printf 'Application:          %s\n' "$app"
if [[ "$debug_optimizations" -eq 1 ]]; then
    printf 'Optimisation:         -Og (CONFIG_DEBUG_OPTIMIZATIONS); not verification evidence\n'
fi
printf 'Build directory:      %s\n' "$build_dir"

# A development build invalidates the previous verification bundle, even on failure.
rm -f "$build_dir/provenance.json"

# Extra CMake arguments force west to reconfigure. Supply them only when creating
# the build system; Ninja tracks source/configuration changes on later launches.
cmake_args=()
if [[ ! -f "$build_dir/build.ninja" ]]; then
    cmake_args=(-- -DZEPHYR_EXTRA_MODULES="$repo" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON)
    if [[ "$debug_optimizations" -eq 1 ]]; then
        cmake_args+=(-DEXTRA_CONF_FILE="$repo/scripts/debug-optimizations.conf")
    fi
fi

# Disable pristine builds so repeated F5 launches reuse CMake/Ninja outputs.
(cd "$workspace" && west build --pristine=never -b "$board" -d "$build_dir" \
    "$app" "${cmake_args[@]}")

"$repo/scripts/select-clangd-database.sh" "$build_dir"

elf="$build_dir/zephyr/zephyr.elf"
if [[ ! -f "$elf" ]]; then
    printf 'Expected firmware ELF was not produced: %s\n' "$elf" >&2
    exit 1
fi

printf '\nFirmware ELF ready for a debug profile:\n%s\n' "$elf"
sha256sum "$elf"
