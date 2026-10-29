#!/usr/bin/env bash
set -euo pipefail

# Check only changed Crux C source/header lines using the pinned Zephyr checkpatch.
repo="$(git rev-parse --show-toplevel)"
base="${1:-HEAD^}"
head="${2:-HEAD}"
if (($# > 2)); then
    printf "Usage: %s [BASE [HEAD]]\n" "$0" >&2
    exit 2
fi

if [[ -z "${ZEPHYR_BASE:-}" || ! -f "$ZEPHYR_BASE/scripts/checkpatch.pl" ]]; then
    printf "Set ZEPHYR_BASE to the prepared pinned Zephyr checkout (scripts/checkpatch.pl required).\n" >&2
    exit 2
fi
for revision in "$base" "$head"; do
    if ! git -C "$repo" rev-parse --verify -q "${revision}^{commit}" >/dev/null; then
        printf "Missing Git commit %s. Fetch the comparison history before running this check.\n" "$revision" >&2
        exit 2
    fi
done

if git -C "$repo" diff --quiet --no-ext-diff "$base" "$head" -- '*.c' '*.h'; then
    printf "No changed C source or header files to check.\n"
    exit 0
fi

# Preserve normal patch context for checkpatch statement analysis.
git -C "$repo" diff --no-ext-diff --no-color "$base" "$head" -- '*.c' '*.h' |
    perl "$ZEPHYR_BASE/scripts/checkpatch.pl" --no-tree --no-signoff --types BRACES -
