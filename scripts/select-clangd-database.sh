#!/usr/bin/env bash
set -euo pipefail

if [[ "$(uname -s)" != Linux ]]; then
    printf 'clangd build databases are selected from Linux Dev Container builds only.\n' >&2
    exit 2
fi

if [[ $# -ne 1 || -z "${1:-}" ]]; then
    printf 'Usage: %s <build-directory>\n' "$0" >&2
    exit 2
fi

repo="$(git rev-parse --show-toplevel)"
build_dir="$1"
if [[ "$build_dir" != /* ]]; then
    build_dir="$repo/$build_dir"
fi
database="$build_dir/compile_commands.json"
if [[ ! -f "$database" ]]; then
    printf 'Compilation database not found: %s\n' "$database" >&2
    exit 2
fi

destination_dir="$repo/build/clangd"
destination="$destination_dir/compile_commands.json"
temporary="$destination.tmp.$$"
mkdir -p "$destination_dir"
trap 'rm -f "$temporary"' EXIT
cp "$database" "$temporary"
mv -f "$temporary" "$destination"
printf 'clangd compilation database selected: %s\n' "$build_dir"
