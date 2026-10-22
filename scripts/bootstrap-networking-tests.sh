#!/usr/bin/env bash
# Test-only embedded libc, pinned by the existing Zephyr manifest (no upgrade).
set -euo pipefail
: "${CRUX_CACHE_DIR:?Set CRUX_CACHE_DIR to the external dependency cache}"
module="${CRUX_PICOLIBC_MODULE:-$CRUX_CACHE_DIR/networking/picolibc}"
revision=01254932e8e81085817ed61fd858648584ffe37c
if [[ ! -d "$module/.git" ]]; then
    mkdir -p "$module"
    git init -q "$module"
    git -C "$module" fetch --depth 1 https://github.com/zephyrproject-rtos/picolibc.git "$revision"
    git -C "$module" checkout --detach FETCH_HEAD
fi
if [[ "$(git -C "$module" rev-parse HEAD)" != "$revision" ]] ||
    [[ -n "$(git -C "$module" status --porcelain)" ]]; then
    printf 'Networking test Picolibc must be clean at %s: %s\n' "$revision" "$module" >&2
    exit 1
fi
printf '%s\n' "$module"
