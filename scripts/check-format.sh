#!/usr/bin/env bash
set -euo pipefail

repo="$(git rev-parse --show-toplevel)"
formatter="${CLANG_FORMAT:-clang-format-18}"
sources=()
while IFS= read -r source; do
    # Git still lists the old path of a deleted or unstaged-renamed file.
    if [[ -f "$repo/$source" ]]; then
        sources+=("$repo/$source")
    fi
done < <(git -C "$repo" ls-files --cached --others --exclude-standard -- '*.c' '*.h' '*.cpp' '*.hpp')
if ((${#sources[@]} == 0)); then
    printf 'No owned C/C++ files found.\n'
    exit 0
fi
"$formatter" --dry-run --Werror "${sources[@]}"
