#!/usr/bin/env bash
set -euo pipefail

repo="$(git rev-parse --show-toplevel)"
formatter="${CLANG_FORMAT:-clang-format-18}"
sources=()
while IFS= read -r source; do
    sources+=("$repo/$source")
done < <(git -C "$repo" ls-files --cached --others --exclude-standard -- '*.c' '*.h' '*.cpp' '*.hpp')
if ((${#sources[@]} == 0)); then
    printf 'No owned C/C++ files found.\n'
    exit 0
fi
"$formatter" --dry-run --Werror "${sources[@]}"
