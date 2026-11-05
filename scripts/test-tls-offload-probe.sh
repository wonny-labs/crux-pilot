#!/usr/bin/env bash
# A TLS server socket over the native socket offload on native_sim, on each provider profile (Issue #242).
# A client on the host does a handshake, moves data both ways and closes. The socket uses the declared test
# certificate. It proves the socket only: the HTTPS listener stays with Issue #185.
#
#   scripts/test-tls-offload-probe.sh [all|mbed|wolf]
#
# mbed needs the mbedtls and tf-psa-crypto projects, so a default workflow can run it. wolf needs the
# crux-secure-management group and the patched tree from scripts/prepare-wolf-tls-zephyr.sh, so a default
# workflow must not run it: see scripts/check-ssh-boundaries.py. It operates no hardware.
set -euo pipefail

scope="${1:-all}"
case "$scope" in all | mbed | wolf) ;; *)
    printf 'usage: %s [all|mbed|wolf]\n' "$0" >&2
    exit 2
    ;;
esac

repo="$(git rev-parse --show-toplevel)"
workspace="${CRUX_WORKSPACE:-$(dirname "$repo")}"
out="${CRUX_TLS_PROBE_OUTPUT:-$repo/build/tls-offload-probe}"
jobs="${CRUX_TLS_PROBE_JOBS:-4}"
source_dir="$repo/tests/native/tls_offload_probe"
board=native_sim/native/64
export ZEPHYR_TOOLCHAIN_VARIANT=host

fail() {
    printf 'FAIL: %s\n' "$*" >&2
    exit 1
}

# Every step passed, this configuration holds, and a limit the record states still holds.
check_result() { # profile, provider, whether a server sees a close with no close_notify through poll, TLS 1.2 port with a newer client
    python3 - "$out/$1/result.json" "$out/$1/probe-lines.txt" "$2" "$3" "$4" <<'PY'
import json, re, sys
result, lines, provider, sees_close, newer_client = sys.argv[1:]
steps = json.load(open(result))["steps"]
lines = open(lines).read()
failed = [name for name, step in steps.items() if step["result"] != "pass"]
if failed:
    sys.exit(f"failed steps: {failed}")
for version in ("1.2", "1.3"):
    if f"tls{version}_exchange" not in steps:
        sys.exit(f"TLS {version} did not run")
    if steps[f"tls{version}_exchange"]["version"] != f"TLSv{version}":
        sys.exit(f"the TLS {version} listener negotiated {steps[f'tls{version}_exchange']['version']}")
    if not steps[f"tls{version}_exchange"]["close_notify_from_server"]:
        sys.exit(f"TLS {version}: the close was not clean")
if not re.search(rf"PROBE psa provider={provider} init=0 random=0 sha256=0 known_answer=1", lines):
    sys.exit(f"{provider} did not run beside the TLS backend")
if steps["concurrent_connections"]["still_working"] != steps["concurrent_connections"]["connections_held"]:
    sys.exit("a held connection stopped working when the limit was reached")
seen = steps["raw_poll_after_abrupt_close"]["server_saw_the_close"]
if seen != (sees_close == "yes"):
    sys.exit(f"a close with no close_notify: the server saw it: {seen}. Review the limit in the record.")
if steps["tls1.2_port_newer_client"]["negotiated"] != newer_client:
    sys.exit(f"the TLS 1.2 listener gave a newer client {steps['tls1.2_port_newer_client']['negotiated']}. Review limit 7a.")
print(f"{provider}: every step passed, TLS 1.2 and 1.3, close seen through poll: {seen}")
PY
}

check_config() { # profile, build directory
    local config="$2/zephyr/.config"
    grep -qx 'CONFIG_NET_NATIVE_OFFLOADED_SOCKETS=y' "$config" || fail "$1: not over the offload"
    grep -qx 'CONFIG_NET_SOCKETS_OFFLOAD=y' "$config" || fail "$1: not over the offload"
    grep -qx 'CONFIG_FAKE_ENTROPY_NATIVE_SIM=y' "$config" ||
        fail "$1: the entropy source changed. Review the test-only declaration."
    grep -E '^CONFIG_(NET_NATIVE_OFFLOADED_SOCKETS|NET_SOCKETS_OFFLOAD|NET_SOCKETS_SOCKOPT_TLS|NET_SOCKETS_TLS_[A-Z_]*|TLS_MAX_CREDENTIALS_NUMBER|ZVFS_OPEN_MAX|ZVFS_POLL_MAX|MAIN_STACK_SIZE|FAKE_ENTROPY_NATIVE_SIM|PSA_CRYPTO_PROVIDER_[A-Z]*|MBEDTLS|MBEDTLS_HEAP_SIZE|MBEDTLS_SSL_MAX_CONTENT_LEN|MBEDTLS_SSL_PROTO_[A-Z0-9_]*|WOLFSSL|WOLFSSL_TLS_VERSION_[0-9_]*|WOLFPSA|WOLFPSA_THREAD_SAFE)=' \
        "$config" > "$out/$1/config.txt"
}

run_profile() { # profile, provider, ZEPHYR_BASE, whether poll shows a close with no close_notify, TLS 1.2 port with a newer client
    local profile="$1" build="$out/$1/build"
    mkdir -p "$out/$profile"
    (cd "$workspace" && ZEPHYR_BASE="$3" west build -p always -b "$board" -d "$build" -o="-j$jobs" "$source_dir" \
        -- "-DEXTRA_CONF_FILE=$profile.conf") > "$out/$profile/build.log" 2>&1 ||
        { tail -40 "$out/$profile/build.log" >&2; fail "$profile: the build failed"; }
    check_config "$profile" "$build"
    python3 "$source_dir/tls_probe_client.py" "$build/zephyr/zephyr.exe" --profile "$profile" --output "$out/$profile" ||
        fail "$profile: a step failed"
    check_result "$profile" "$2" "$4" "$5"
    rm -rf "$build"
}

printf 'Crux revision: %s\n' "$(git -C "$repo" rev-parse HEAD)"
if [[ "$scope" != wolf ]]; then
    run_profile mbed tf-psa-crypto "$workspace/zephyr" no TLSv1.3
fi
if [[ "$scope" != mbed ]]; then
    "$repo/scripts/prepare-wolf-tls-zephyr.sh" "$out/zephyr-wolf"
    run_profile wolf wolfpsa "$out/zephyr-wolf" yes TLSv1.2
fi
printf 'TLS offload probe (%s) finished.\n' "$scope"
