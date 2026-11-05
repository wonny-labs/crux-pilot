#!/usr/bin/env bash
# OpenSSH against SFTP on native_sim (Issue #194): the provider of #192, the real access adapter and file
# port, the real file service, and real FatFs over a RAM disk. These are native_sim results only. It never
# flashes, resets or runs anything on a board. No default workflow runs it, because it needs the optional
# stacks: west config manifest.group-filter -- +crux-secure-management && west update
set -euo pipefail

if [[ "$(uname -s)" != Linux ]]; then
    printf 'Zephyr builds are configured for Linux only; reopen this repository in the Dev Container.\n' >&2
    exit 2
fi

repo="$(git rev-parse --show-toplevel)"
if [[ ! -f "$repo/components/ssh/ports/zephyr/crux_ssh_wolf_provider.c" ]]; then
    printf 'This needs the wolfSSH provider of #192, which this revision does not hold.\n' >&2
    exit 2
fi
workspace="${CRUX_WORKSPACE:-$(dirname "$repo")}"
build="${CRUX_SSH_SFTP_NATIVE_BUILD:-$repo/build/zephyr/ssh-sftp-native}"
jobs="${CRUX_SSH_SFTP_JOBS:-3}"
port="${CRUX_SSH_SFTP_PORT:-2222}"
idle_port=$((port + 1))
idle_limit=4
modules="$workspace/modules"
module_list="$modules/crypto/wolfssl;$modules/crypto/wolfssh;$modules/fs/fatfs"
log="$build.server.log"

ZEPHYR_BASE="$workspace/zephyr" cmake -S "$repo/tests/ssh/sftp/native_sim" -B "$build" -G Ninja \
    -DBOARD=native_sim/native/64 "-DZEPHYR_MODULES=$module_list" \
    "-DPython3_EXECUTABLE=$(command -v python3)" > "$build.configure.log"
ninja -C "$build" -j "$jobs" > "$build.build.log"
image="$build/zephyr/zephyr.exe"

work="$(mktemp -d)"
server_pid=""
stop_server() {
    if [[ -n "$server_pid" ]]; then
        kill -9 "$server_pid" 2> /dev/null || true
        wait "$server_pid" 2> /dev/null || true
        server_pid=""
    fi
}
cleanup() {
    stop_server
    rm -rf "$work"
}
trap cleanup EXIT

ssh-keygen -q -t ed25519 -N '' -f "$work/user_key"
key_hex="$(python3 -c "import base64,sys; print(base64.b64decode(open(sys.argv[1]).read().split()[1]).hex())" "$work/user_key.pub")"
ssh -V 2>&1 | sed 's/^/Client: /'

start_image() { # port, idle limit, log
    "$image" -testargs "key=$key_hex" "idle=$2" "port=$1" > "$3" 2>&1 &
    server_pid=$!
}

cases=(python3 -I "$repo/tests/ssh/sftp/openssh/cases.py" --key "$work/user_key")

status=0
start_image "$port" 900 "$log"
"${cases[@]}" --port "$port" --server-log "$log" "$@" || status=$?
grep 'MEASURE' "$log" | tail -1 || true
stop_server

# An image of its own, because every other case would have to outrun a short idle limit.
if [[ $# -eq 0 ]]; then
    start_image "$idle_port" "$idle_limit" "$log.idle"
    "${cases[@]}" --port "$idle_port" --server-log "$log.idle" --idle-limit "$idle_limit" || status=$?
    stop_server
fi
printf 'native_sim results only. No board was operated.\n'
exit "$status"
