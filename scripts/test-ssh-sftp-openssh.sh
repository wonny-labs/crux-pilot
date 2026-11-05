#!/usr/bin/env bash
# Runs real OpenSSH against the wolfSSH SFTP handlers on the real port and the real file service, on
# Linux (Issue #194). The server stands in for the provider of #192, so this is Host evidence about the
# port and says nothing about Zephyr. No default workflow runs it, because it needs wolfSSL and wolfSSH:
#   west config manifest.group-filter -- +crux-secure-management && west update
# or CRUX_SSH_WOLF_MODULES pointing at a directory with clean checkouts at the pinned revisions.
set -euo pipefail

if [[ "$(uname -s)" != Linux ]]; then
    printf 'This runs on Linux only; reopen this repository in the Dev Container.\n' >&2
    exit 2
fi

repo="$(git rev-parse --show-toplevel)"
workspace="${CRUX_WORKSPACE:-$(dirname "$repo")}"
modules="${CRUX_SSH_WOLF_MODULES:-$workspace/modules/crypto}"
build="${CRUX_SSH_SFTP_BUILD:-$repo/build/ssh-sftp-openssh}"
preset="${CRUX_SSH_SFTP_PRESET:-host-debug}"
jobs="${CRUX_SSH_SFTP_JOBS:-3}"
port="${CRUX_SSH_SFTP_PORT:-2222}"
idle_port=$((port + 1))
idle_limit=4

pinned_revision() { # project name in west.yml
    python3 - "$repo/west.yml" "$1" <<'PY'
import re
import sys

text = open(sys.argv[1]).read().split("projects:", 1)[1]
match = re.search(r"- name: " + re.escape(sys.argv[2]) + r"\n(?:\s+\S.*\n)*?\s+revision: ([0-9a-f]{40})", text)
print(match.group(1))
PY
}

for module in wolfssl wolfssh; do
    if [[ ! -e "$modules/$module/.git" ]]; then
        printf '%s is missing from %s. Enable the optional stacks, or set CRUX_SSH_WOLF_MODULES.\n' "$module" "$modules" >&2
        exit 2
    fi
    if [[ "$(git -C "$modules/$module" rev-parse HEAD)" != "$(pinned_revision "$module")" ]]; then
        printf '%s is not at the revision that west.yml pins.\n' "$module" >&2
        exit 1
    fi
    if [[ -n "$(git -C "$modules/$module" status --porcelain)" ]]; then
        printf '%s has changes, and this must run on unedited sources.\n' "$module" >&2
        exit 1
    fi
done

cmake --preset "$preset" -B "$build" "-DCRUX_SSH_SFTP_WOLF_MODULES=$modules" > "$build.configure.log"
ninja -C "$build" -j "$jobs" crux_ssh_sftp_proof_server > "$build.build.log"
server="$build/tests/ssh/sftp/openssh/crux_ssh_sftp_proof_server"

work="$(mktemp -d)"
server_pid=""
stop_server() {
    if [[ -n "$server_pid" ]]; then
        kill "$server_pid" 2> /dev/null || true
        wait "$server_pid" 2> /dev/null || true
        server_pid=""
    fi
}
cleanup() {
    stop_server
    rm -rf "$work"
}
trap cleanup EXIT

mkdir -p "$work/areas/data" "$work/areas/logs" "$work/areas/inbox" "$work/areas/web"
# A read-only area can get a file only from outside, and the service counts it at its start.
printf 'abc' > "$work/areas/logs/boot.log"
openssl ecparam -genkey -name prime256v1 -outform DER -noout -out "$work/host.der" 2> /dev/null
ssh-keygen -q -t ed25519 -N '' -f "$work/user_key"
ssh -V 2>&1 | sed 's/^/Client: /'
printf 'Server: wolfSSH %s, wolfSSL %s, on Linux, with the port and the file service.\n' \
    "$(git -C "$modules/wolfssh" rev-parse --short HEAD)" "$(git -C "$modules/wolfssl" rev-parse --short HEAD)"

start_server() { # port, idle limit
    "$server" "$1" "$work/host.der" "$work/areas" "$2" > "$work/server.log" 2>&1 &
    server_pid=$!
}

cases=(python3 -I "$repo/tests/ssh/sftp/openssh/cases.py" --key "$work/user_key" --directory "$work/areas"
    --server-log "$work/server.log")

status=0
start_server "$port" 900
"${cases[@]}" --port "$port" "$@" || status=$?
stop_server

# A server of its own, because every other case would have to outrun a short idle limit.
if [[ $# -eq 0 ]]; then
    start_server "$idle_port" "$idle_limit"
    "${cases[@]}" --port "$idle_port" --idle-limit "$idle_limit" || status=$?
    stop_server
fi

if [[ -n "${CRUX_SSH_SFTP_KEEP_LOG:-}" ]]; then
    cp "$work/server.log" "$CRUX_SSH_SFTP_KEEP_LOG"
fi
exit "$status"
