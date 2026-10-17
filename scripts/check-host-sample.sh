#!/usr/bin/env bash
set -euo pipefail

repo="$(git rev-parse --show-toplevel)"
host_preset="${1:-host-debug}"
if [[ $# -gt 2 ]]; then
    printf 'Usage: %s [host-debug|host-sanitize [build-dir]]\n' "$0" >&2
    exit 2
fi
if [[ $# -eq 2 ]]; then
    build_dir="$2"
    if [[ "$build_dir" != /* ]]; then
        build_dir="$repo/$build_dir"
    fi
else
    case "$host_preset" in
        host-debug)
            build_dir="$repo/build/linux/debug"
            ;;
        host-sanitize)
            build_dir="$repo/build/linux/sanitize"
            ;;
        *)
            printf 'Unsupported host preset: %s\n' "$host_preset" >&2
            exit 2
            ;;
    esac
fi

sample="$build_dir/samples/time/host/crux-time-host"
if [[ ! -x "$sample" ]]; then
    printf 'Host sample is missing or not executable: %s\n' "$sample" >&2
    exit 2
fi

output="$(printf '%s\n' \
    'time get' \
    'time set 2026-10-16T14:30:00' \
    'time get' \
    'time get' \
    'help' | "$sample")"

grep -Fq 'host simulation' <<<"$output"
grep -Fq 'CRUX_TIME_READY composition=host-simulation' <<<"$output"
grep -Fq 'ERR NOT_SET' <<<"$output"
grep -Fq 'Host simulation uses a controlled clock; it does not follow wall time or advance automatically.' <<<"$output"
[[ "$(grep -Fxc '2026-10-16T14:30:00 UTC' <<<"$output")" -eq 2 ]]
[[ "$(grep -Fxc 'OK' <<<"$output")" -eq 1 ]]

printf 'Host sample stdin/stdout scenario passed (%s).\n' "$host_preset"
