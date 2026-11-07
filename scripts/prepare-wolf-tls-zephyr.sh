#!/usr/bin/env bash
# Writes a copy of the pinned Zephyr tree with the wolfSSL TLS socket backend applied, for a test of the
# Wolf profile to use as ZEPHYR_BASE. The west checkout stays unedited, and the patch stays out of this
# repository: patches/zephyr/README.md holds its source, its hashes and why. It is for Host and native_sim
# tests only. How a product composition applies the patch is decided under Issue #185.
#
#   scripts/prepare-wolf-tls-zephyr.sh OUTPUT_DIR
set -euo pipefail

output="${1:?usage: prepare-wolf-tls-zephyr.sh OUTPUT_DIR}"
repo="$(git rev-parse --show-toplevel)"
workspace="${CRUX_WORKSPACE:-$(dirname "$repo")}"
osp_revision=8c32b060ac6017b1291a6da0875a48637bfb682d
patch_name=zephyr-tls-4.4.1.patch
patch_sha256=3d1cc0307bbeea5f000698bca5be717f73fdb8c47e90083b003336364e8fdbda
# sockets_tls.c after the patch on Zephyr v4.4.2. A change means the base moved, so review the result.
result_sha256=106acdbb725fc72e4eca242125fd02283b0cd67403b205c7c231b2db6328552a
zephyr_pin="$(awk '$1 == "-" && $2 == "name:" { in_project = ($3 == "zephyr") }
    in_project && $1 == "revision:" { print $2; exit }' "$repo/west.yml")"

[[ "$(git -C "$workspace/zephyr" rev-parse HEAD)" == "$zephyr_pin" ]] ||
    { printf 'FAIL: the Zephyr checkout is not at the pin %s\n' "$zephyr_pin" >&2; exit 1; }
if [[ -f "$output/.crux-wolf-tls" ]] && [[ "$(cat "$output/.crux-wolf-tls")" == "$zephyr_pin $patch_sha256" ]]; then
    printf 'already prepared: %s\n' "$output"
    exit 0
fi
rm -rf "$output"
mkdir -p "$output"
# The documents, samples and tests are not needed to build, and they are half of the tree's size.
git -C "$workspace/zephyr" archive HEAD ':!doc' ':!samples' ':!tests' | tar -x -C "$output"
patch_file="$output/.$patch_name"
curl --fail --silent --show-error --location --output "$patch_file" \
    "https://raw.githubusercontent.com/wolfSSL/osp/$osp_revision/zephyr/4.4/$patch_name"
printf '%s  %s\n' "$patch_sha256" "$patch_file" | sha256sum --check --quiet
(cd "$output" && patch --strip=1 --no-backup-if-mismatch --quiet < "$patch_file")
[[ -z "$(find "$output" -name '*.rej' -print -quit)" ]] || { printf 'FAIL: a hunk was rejected\n' >&2; exit 1; }
printf '%s  %s\n' "$result_sha256" "$output/subsys/net/lib/sockets/sockets_tls.c" | sha256sum --check --quiet
rm -f "$patch_file"
printf '%s %s\n' "$zephyr_pin" "$patch_sha256" > "$output/.crux-wolf-tls"
printf 'prepared: %s, Zephyr %s with %s at wolfSSL/osp %s\n' "$output" "$zephyr_pin" "$patch_name" "$osp_revision"
