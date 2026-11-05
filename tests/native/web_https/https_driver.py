#!/usr/bin/env python3
"""The plain HTTP suite of tests/native/web_http over TLS, and the TLS cases of Issue #185, on native_sim.

The image is the adapter on the management composition, with the declared test certificate, over the
native socket offload. The suite's cases run unchanged with each client wrapped in TLS. The TLS cases
check the listener, the declared versions, cipher suites and groups on the wire, refused handshakes, an
abrupt close, a stalled handshake, the heap and the client limit, and the sends of a small response.
Timings are simulation figures on a loaded machine, and none of them is device evidence.

    https_driver.py <zephyr.exe> --profile mbed|wolf [--results file] [--only names] [--firefox binary]
"""
import argparse
import hashlib
import json
import os
import pathlib
import socket
import ssl
import statistics
import subprocess
import sys
import tempfile
import threading
import time

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "web_http"))
import firefox_capture  # noqa: E402
import test_driver as suite  # noqa: E402

CREDENTIALS = HERE.parent / "tls_offload_probe" / "credentials"
CA = CREDENTIALS / "test-server.crt"
OTHER_CA = CREDENTIALS / "other-ca.crt"
HOST = suite.HOST
PORT = 18443
# NET_SOCKETS_TLS_CONNECT_TIMEOUT in the reference configuration, which zephyr/Kconfig sets for CRUX_WEB.
CONNECT_TIMEOUT_S = 2.0
SLACK_S = suite.SLACK_S
VERSIONS = {"tls1": "TLSv1", "tls1_1": "TLSv1.1", "tls1_2": "TLSv1.2", "tls1_3": "TLSv1.3"}
TLS13_SUITES = ["TLS_AES_128_GCM_SHA256", "TLS_AES_256_GCM_SHA384", "TLS_CHACHA20_POLY1305_SHA256",
                "TLS_AES_128_CCM_SHA256", "TLS_AES_128_CCM_8_SHA256"]
GROUPS = ["P-256", "P-384", "P-521", "X25519", "X448", "ffdhe2048"]
# What each profile declares. The same versions and suite on both. The groups are what each library
# offers on its own: see docs/https-composition.md.
DECLARED = {
    "versions": ["TLSv1.3"],
    "suites": ["TLS_AES_128_GCM_SHA256"],
    "groups": {"mbed": ["P-256"], "wolf": ["P-256", "X25519"]},
}

results = {}
profile = None
process = None


def check(condition, message):
    if not condition:
        raise AssertionError(message)


def tls_context(ca=CA):
    context = ssl.create_default_context(cafile=str(ca))
    context.minimum_version = ssl.TLSVersion.TLSv1_2
    return context


def handshake(*options, ca=CA, timeout=15):
    """One handshake with OpenSSL's own client. What it negotiated, or why it failed."""
    command = ["openssl", "s_client", "-connect", f"{HOST}:{PORT}", "-servername", "localhost",
               "-CAfile", str(ca), "-verify_return_error", "-brief", *options]
    started = time.monotonic()
    run = subprocess.run(command, stdin=subprocess.DEVNULL, capture_output=True, text=True,
                         timeout=timeout, check=False)
    output = run.stdout + run.stderr
    found = {}
    for line in output.splitlines():
        for key, label in (("protocol", "Protocol version:"), ("suite", "Ciphersuite:"),
                           ("group", "Server Temp Key:"), ("group", "Negotiated TLS1.3 group:")):
            if line.startswith(label):
                found[key] = line[len(label):].strip()
    alert = next((line.strip() for line in output.splitlines() if "alert" in line), None)
    return {"ok": "suite" in found and run.returncode == 0, **found, "alert": alert,
            "seconds": round(time.monotonic() - started, 3)}


def cpu_seconds():
    with open(f"/proc/{process.pid}/stat", encoding="ascii") as stat:
        fields = stat.read().rsplit(")", 1)[1].split()
    return (int(fields[11]) + int(fields[12])) / os.sysconf("SC_CLK_TCK")


def cpu_share(seconds):
    before = cpu_seconds()
    time.sleep(seconds)
    return round((cpu_seconds() - before) / seconds, 2)


def timed_request(path="/probe/stats", timeout=30.0):
    started = time.monotonic()
    client = suite.Client(timeout=timeout)
    try:
        response = client.request("GET", path)
    finally:
        client.close()
    return response, round(time.monotonic() - started, 3)


def tls_stats():
    return suite.one("GET", "/probe/tls/stats").json()


def raw_tcp():
    sock = socket.create_connection((HOST, PORT), timeout=20.0)
    return sock


def client_hello_start():
    """The first bytes of a real ClientHello, cut before it ends."""
    sink = ssl.MemoryBIO()
    tls = tls_context().wrap_bio(ssl.MemoryBIO(), sink, server_hostname="localhost")
    try:
        tls.do_handshake()
    except ssl.SSLWantReadError:
        pass
    data = sink.read()
    return data[:len(data) // 2]


def closed_by_server(sock, seconds):
    sock.settimeout(seconds)
    try:
        while sock.recv(4096):
            pass
        return True
    except (ConnectionResetError, BrokenPipeError):
        return True
    except TimeoutError:
        return False


# The listener.

def listening_ports(pid):
    inodes = set()
    for fd in pathlib.Path(f"/proc/{pid}/fd").iterdir():
        try:
            target = os.readlink(fd)
        except OSError:
            continue
        if target.startswith("socket:["):
            inodes.add(target[8:-1])
    ports = []
    for table in ("/proc/net/tcp", "/proc/net/tcp6"):
        if not os.path.exists(table):
            continue
        with open(table, encoding="ascii") as entries:
            for line in entries.readlines()[1:]:
                fields = line.split()
                if fields[3] == "0A" and fields[9] in inodes:
                    ports.append((table.rsplit("/", 1)[1], int(fields[1].rsplit(":", 1)[1], 16)))
    return sorted(ports)


def case_tls_listener_only():
    """No plain HTTP listener or redirect: one listening socket, and plain text on it gets no HTTP."""
    ports = listening_ports(process.pid)
    # One socket. The offload binds IPv6 with IPv4 mapped when the image has both, so tcp6 is allowed.
    check([port for _, port in ports] == [PORT], f"listening sockets of the image: {ports}")
    sock = raw_tcp()
    sock.sendall(b"GET / HTTP/1.1\r\nHost: localhost\r\n\r\n")
    reply = b""
    sock.settimeout(CONNECT_TIMEOUT_S + SLACK_S + 2.0)
    try:
        while chunk := sock.recv(4096):
            reply += chunk
    except (ConnectionResetError, TimeoutError):
        pass
    sock.close()
    check(b"HTTP/" not in reply and b"Location" not in reply, f"plain text got {reply[:80]!r}")
    results["listener"] = {"listening": [f"{table}:{port}" for table, port in ports],
                           "plain_request_reply": reply.hex()}


# Declared versions, suites and groups, on the wire.

def case_tls_declared_versions():
    seen = {}
    for option, name in VERSIONS.items():
        extra = ["-cipher", "DEFAULT:@SECLEVEL=0"] if option in ("tls1", "tls1_1") else []
        seen[name] = handshake(f"-{option}", *extra)
    accepted = sorted(name for name, result in seen.items() if result["ok"])
    results["versions"] = seen
    check(accepted == DECLARED["versions"], f"accepted versions {accepted}")
    check(all(seen[name].get("protocol") == name for name in accepted), f"{seen}")


def case_tls_declared_suites():
    seen = {name: handshake("-tls1_3", "-ciphersuites", name) for name in TLS13_SUITES}
    accepted = sorted(name for name, result in seen.items() if result["ok"])
    results["suites"] = seen
    check(accepted == DECLARED["suites"], f"accepted suites {accepted}")


def case_tls_declared_groups():
    seen = {name: handshake("-tls1_3", "-groups", name) for name in GROUPS}
    accepted = sorted(name for name, result in seen.items() if result["ok"])
    results["groups"] = seen
    check(accepted == sorted(DECLARED["groups"][profile]), f"accepted groups {accepted}")


def case_tls_certificate_is_the_declared_one():
    with socket.create_connection((HOST, PORT), timeout=20.0) as raw:
        with tls_context().wrap_socket(raw, server_hostname="localhost") as tls:
            presented = tls.getpeercert(binary_form=True)
            negotiated = {"version": tls.version(), "cipher": tls.cipher()[0]}
    declared = ssl.PEM_cert_to_DER_cert(CA.read_text(encoding="ascii"))
    check(presented == declared, "the image presented another certificate")
    refused = handshake(ca=OTHER_CA)
    check(not refused["ok"], f"a client that trusts another certificate connected: {refused}")
    results["certificate"] = {"presented_is_declared": True, "python_client": negotiated,
                              "other_ca": refused}


# Refused handshakes leave the server serving.

def expect_refused_then_served(name, *options):
    refused = handshake(*options)
    check(not refused["ok"], f"{name}: {refused}")
    response, seconds = timed_request()
    check(response.status == 200 and seconds < 1.0 + SLACK_S, f"{name}: next request {seconds}s")
    results.setdefault("refused", {})[name] = {**refused, "next_request_s": seconds}


def case_tls_refuses_bad_version():
    expect_refused_then_served("tls1.2_only_client", "-tls1_2")


def case_tls_refuses_bad_cipher():
    expect_refused_then_served("tls1.3_aes256_only", "-tls1_3", "-ciphersuites", "TLS_AES_256_GCM_SHA384")
    expect_refused_then_served("tls1.3_chacha_only", "-tls1_3", "-ciphersuites",
                               "TLS_CHACHA20_POLY1305_SHA256")


# An abrupt close: TCP ends with no close_notify.

def abrupt_after_request():
    client = suite.Client()
    check(client.request("GET", "/probe/stats").status == 200, "request")
    client.close()  # Python's ssl sends no close_notify on close().


def abrupt_mid_handshake():
    sock = raw_tcp()
    sock.sendall(client_hello_start())
    time.sleep(0.1)
    sock.close()


def abrupt_reset_mid_handshake():
    sock = raw_tcp()
    sock.sendall(client_hello_start())
    time.sleep(0.1)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, b"\x01\x00\x00\x00\x00\x00\x00\x00")
    sock.close()


def abrupt_mid_request():
    client = suite.Client()
    client.send(b"GET /probe/stats HTTP/1.1\r\nHo")
    time.sleep(0.1)
    client.close()


def case_tls_abrupt_close():
    seen = {}
    for name, action in (("after_request", abrupt_after_request), ("mid_handshake", abrupt_mid_handshake),
                         ("reset_mid_handshake", abrupt_reset_mid_handshake),
                         ("mid_request", abrupt_mid_request)):
        action()
        share = cpu_share(2.0)
        response, seconds = timed_request()
        check(response.status == 200 and seconds < 1.0 + SLACK_S, f"{name}: next request {seconds}s")
        check(share < 0.5, f"{name}: the image used {share} of a CPU after the close")
        seen[name] = {"cpu_share_2s": share, "next_request_s": seconds}
    # Four closed clients, then the three slots must all be free.
    for _ in range(4):
        abrupt_after_request()
    clients = [suite.Client() for _ in range(3)]
    started = time.monotonic()
    statuses = [client.request("GET", "/probe/stats").status for client in clients]
    for client in clients:
        client.close()
    check(statuses == [200] * 3, f"{statuses}")
    seen["three_slots_after_four_closes_s"] = round(time.monotonic() - started, 3)
    results["abrupt_close"] = seen


def close_with_notify(client):
    try:
        client.sock.tls.unwrap()
    except (OSError, ssl.SSLError):
        pass
    client.sock.tls.close()


def case_tls_request_after_a_close():
    """How long the next connection's request waits after the server has closed a client (limit W1 on Wolf)."""
    seen = {}
    for name, closer in (("client_close_notify", close_with_notify), ("client_abrupt", lambda c: c.close())):
        waits = []
        for _ in range(3):
            client = suite.Client()
            check(client.request("GET", "/probe/stats").status == 200, "first")
            closer(client)
            waits.append(timed_request()[1])
            time.sleep(1.5)
        seen[name] = waits
    fresh = []
    for _ in range(3):
        fresh.append(timed_request()[1])
        time.sleep(1.5)
    # A request 1.5 s after the last close, which W1 does not reach.
    seen["no_close_within_1.5s"] = fresh
    check(max(max(waits) for waits in seen.values()) < 1.0 + SLACK_S, f"{seen}")
    results["request_after_a_close_s"] = seen


# A stalled handshake holds the one server thread for NET_SOCKETS_TLS_CONNECT_TIMEOUT.

def stalled(first_bytes):
    staller = raw_tcp()
    if first_bytes:
        staller.sendall(first_bytes)
    time.sleep(0.2)
    response, waited = timed_request()
    check(response.status == 200, "the next client is served")
    closed = closed_by_server(staller, CONNECT_TIMEOUT_S + SLACK_S)
    staller.close()
    return waited, closed


def case_tls_stalled_handshake():
    seen = {}
    for name, first in (("silent", b""), ("partial_client_hello", client_hello_start())):
        waited, closed = stalled(first)
        check(CONNECT_TIMEOUT_S - 0.6 <= waited <= CONNECT_TIMEOUT_S + 0.2 + SLACK_S,
              f"{name}: the next client waited {waited}s")
        check(closed, f"{name}: the server must drop the staller")
        seen[name] = {"next_client_waited_s": waited}
    stallers = [raw_tcp(), raw_tcp()]
    time.sleep(0.2)
    response, waited = timed_request()
    for staller in stallers:
        staller.close()
    check(response.status == 200 and waited >= 2 * CONNECT_TIMEOUT_S - 0.6, f"two stallers: {waited}s")
    seen["two_silent"] = {"next_client_waited_s": waited}
    seen["connect_timeout_s"] = CONNECT_TIMEOUT_S
    results["stalled_handshake"] = seen


# Heap and the client limit.

def tls_heap(stats):
    """The TLS library's heap: Mbed TLS's buffer, or the C library's arena that wolfSSL uses."""
    if profile == "mbed":
        return stats["mbed_heap"], stats["mbed_heap_max"]
    # The arena is the heap that TLS use moves: its size reads 0 in the heap list at this pin.
    arena = max(stats["heaps"], key=lambda heap: heap["max"])
    return arena["used"], arena["max"]


def case_tls_heap_and_client_limit():
    time.sleep(suite.INACTIVITY_S + 0.5)  # Earlier clients are gone.
    suite.one("GET", "/probe/tls/reset-max")
    idle, _ = tls_heap(tls_stats())
    clients = []
    held = []
    for _ in range(3):
        client = suite.Client()
        check(client.request("GET", "/probe/stats").status == 200, "held client")
        clients.append(client)
        stats = clients[0].request("GET", "/probe/tls/stats").json()
        held.append(tls_heap(stats))
    # Each held client was just active, so none of them frees its slot before the inactivity timer.
    for client in clients:
        check(client.request("GET", "/probe/stats").status == 200, "held client, again")
    # A fourth client: the server stops accepting at its limit, so it waits in the backlog.
    started = time.monotonic()
    fourth = suite.Client(timeout=30.0)
    response = fourth.request("GET", "/probe/stats")
    waited = round(time.monotonic() - started, 2)
    fourth.close()
    for client in clients:
        client.close()
    check(response.status == 200, "the fourth client is served once a slot frees")
    check(waited >= suite.INACTIVITY_S - 1.0, f"the fourth client waited {waited}s")
    # Each figure includes the measuring connection, so one connection is the step between them.
    results["heap"] = {
        "profile": profile, "before_held_clients_bytes": idle,
        "with_clients": [{"clients": index + 1, "bytes": used, "largest_bytes": largest}
                         for index, (used, largest) in enumerate(held)],
        "per_connection_bytes": round((held[-1][0] - held[0][0]) / 2),
        "fourth_client_waited_s": waited,
    }


def case_tls_client_over_the_heap():
    """A full heap: the next accept fails, the client gets no HTTP, and the server goes on serving."""
    time.sleep(suite.INACTIVITY_S + 0.5)
    # One kept connection takes the heap, since a new one would need heap of its own.
    holder = suite.Client()
    taken = 0
    for chunk in [8192] * 48 + [1024] * 16 + [128] * 16:
        held = holder.request("GET", f"/probe/tls/hold/{chunk}").json()["held"]
        if held == taken and chunk == 128:
            break
        taken = held
    refused = {}
    started = time.monotonic()
    try:
        with socket.create_connection((HOST, PORT), timeout=20.0) as raw:
            with tls_context().wrap_socket(raw, server_hostname="localhost") as tls:
                tls.sendall(suite.encode("GET", "/probe/stats"))
                refused["reply"] = tls.recv(64).decode("latin-1")
    except (OSError, ssl.SSLError) as error:
        refused["error"] = f"{type(error).__name__}: {error}"
    refused["seconds"] = round(time.monotonic() - started, 3)
    try:
        holder.request("GET", "/probe/tls/hold/0")
        released = "over the kept connection"
    except (OSError, suite.Closed):
        released = "over a new connection"
        suite.one("GET", "/probe/tls/hold/0")
    holder.close()
    response, seconds = timed_request()
    results["over_heap"] = {"held_bytes": taken, "client_saw": refused, "released": released,
                            "next_request_s": seconds}
    check("reply" not in refused or "HTTP/1.1 200" not in refused["reply"], f"{refused}")
    check(response.status == 200, "served again after the heap is freed")


# The sends of a small response, and whether to pass the headers in one value.

def measure_raw(path, count=60):
    client = suite.Client()
    before = client.request("GET", "/probe/tls/stats").json()
    cost = client.request("GET", "/probe/tls/stats").json()
    stats_sends = cost["outer_sends"] - before["outer_sends"]
    stats_inner = cost["inner_sends"] - before["inner_sends"]
    latencies = []
    for _ in range(count):
        started = time.monotonic()
        response = client.request("GET", path)
        latencies.append((time.monotonic() - started) * 1000)
        check(response.status == 200 and response.body == b'{"ok":true}', f"{path}: {response.status}")
        check(response.headers.get("x-content-type-options") == "nosniff", f"{path}: {response.headers}")
    after = client.request("GET", "/probe/tls/stats").json()
    client.close()
    outer = (after["outer_sends"] - cost["outer_sends"] - stats_sends) / count
    inner = (after["inner_sends"] - cost["inner_sends"] - stats_inner) / count
    latencies.sort()
    return {"sends_per_response": round(outer, 2), "tcp_sends_per_response": round(inner, 2),
            "median_ms": round(statistics.median(latencies), 1),
            "p90_ms": round(latencies[int(len(latencies) * 0.9)], 1)}


def case_tls_sends_of_a_small_response():
    split = measure_raw("/probe/raw/split")
    joined = measure_raw("/probe/raw/joined")
    nodelay = measure_raw("/probe/raw/nodelay")
    check(split["sends_per_response"] > joined["sends_per_response"], f"{split} {joined}")
    results["small_response"] = {"split": split, "joined": joined, "split_with_nodelay": nodelay,
                                 "send_timeout_ms": int(suite.SEND_TIMEOUT_S * 1000)}


def case_tls_slow_reader_small_responses():
    """The suite's silent reader with a full socket, which behaves otherwise over TLS.

    Plain text on the offload, each send can wait out its timeout and then goes through, so one small
    response can cost about twenty timeouts. Over TLS the socket beneath is non-blocking: the TLS layer
    waits once for room, up to the send timeout, and then returns -EAGAIN, which stops that response.
    So the plain case's bound, several timeouts, does not hold here, and this case holds the TLS one.
    """
    before = tls_stats()["real_eagain"]
    attempts = [suite.fill_a_silent_readers_socket() for _ in range(2)]
    eagain = tls_stats()["real_eagain"] - before
    summary = max(attempts, key=lambda attempt: attempt["max_ms"])
    check(all(attempt["unfinished"] == 0 for attempt in attempts), f"a ping never finished: {attempts}")
    check(summary["max_ms"] >= suite.SEND_TIMEOUT_S * 1000 * 0.8, f"a send waited: {attempts}")
    check(summary["max_ms"] <= (2 * suite.SEND_TIMEOUT_S + SLACK_S) * 1000, f"{attempts}")
    results["slow_reader_small_responses"] = {
        **summary, "attempts": len(attempts), "send_timeout_ms": int(suite.SEND_TIMEOUT_S * 1000),
        "sends_waited": round(summary["max_ms"] / (suite.SEND_TIMEOUT_S * 1000), 1),
        "sends_returned_eagain": eagain}
    check(eagain >= 1, "a send must have returned -EAGAIN")


def case_tls_slow_reader_with_progress():
    """The suite's reader that drains 20 KB/s, which behaves otherwise over TLS.

    Plain text, every send goes through after its wait, so the response deadline is what cuts the response.
    Over TLS a send that finds no room within the send timeout returns -EAGAIN, which stops the response
    first. This holds what must still be true, and records which of the two stopped it.
    """
    before = suite.stats()
    before_tls = tls_stats()
    reader = suite.Client(rcvbuf=4096, timeout=0.05)
    reader.send(suite.encode("GET", "/probe/static/8000000"))
    started = time.monotonic()
    total = 0
    with suite.Pinger() as pinger:
        while time.monotonic() - started < suite.RESPONSE_DEADLINE_S + suite.SEND_TIMEOUT_S * 2 + SLACK_S:
            try:
                data = reader.sock.recv(2048)
                if not data:
                    break
                total += len(data)
            except TimeoutError:
                pass
            except ConnectionResetError:
                break
            time.sleep(0.1)
    summary = pinger.summary()
    reader.reset()
    suite.wait_for_settled()
    after = suite.stats()
    after_tls = tls_stats()
    by_deadline = after["response_deadlines"] - before["response_deadlines"]
    by_eagain = after_tls["real_eagain"] - before_tls["real_eagain"]
    check(by_deadline + by_eagain >= 1, f"nothing stopped the response: {before} {after}")
    check(total < 8000000, "the response must be cut")
    check(summary["unfinished"] == 0, f"a ping never finished: {summary}")
    check(summary["max_ms"] <= suite.STALL_BOUND_S * 1000, f"{summary}")
    results["slow_reader_with_progress"] = {
        **summary, "bytes_read": total, "stopped_by_response_deadline": by_deadline,
        "stopped_by_send_eagain": by_eagain, "response_deadline_ms": int(suite.RESPONSE_DEADLINE_S * 1000)}


# The plain case that this replaces over TLS, with the reason above.
REPLACED = {"case_slow_reader_small_responses", "case_slow_reader_with_progress"}

TLS_CASES = [value for name, value in list(globals().items()) if name.startswith("case_tls_")]


# Separate images: a start with no certificate, and the offload as it is, without the FIN fix.

def start(executable, log, *extra):
    global process
    process = subprocess.Popen([str(executable), "--flash_in_ram", "--seed=185", *extra], stdout=log,
                               stderr=subprocess.STDOUT)
    deadline = time.monotonic() + 180.0
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise SystemExit(f"the image exited with {process.returncode}")
        if listening_ports(process.pid):
            time.sleep(0.5)
            return process
        time.sleep(0.5)
    process.kill()
    raise SystemExit("the image did not start listening")


def stop():
    process.kill()
    process.wait()


def no_certificate(executable, log):
    """No certificate under the service's tag: the TLS layer refuses the tag, so nothing listens."""
    global process
    process = subprocess.Popen([str(executable), "--flash_in_ram", "--seed=185", "--no-tls-credential"],
                               stdout=log, stderr=subprocess.STDOUT)
    try:
        deadline = time.monotonic() + 180.0
        while "CRUX_WEB_READY" not in pathlib.Path(log.name).read_text(errors="replace"):
            check(process.poll() is None and time.monotonic() < deadline, "the image did not start")
            time.sleep(0.5)
        time.sleep(3.0)
        ports = listening_ports(process.pid)
        try:
            socket.create_connection((HOST, PORT), timeout=5.0).close()
            connect = "accepted"
        except ConnectionRefusedError:
            connect = "refused"
        check(process.poll() is None, "the image stays up")
        check(ports == [] and connect == "refused", f"listening {ports}, connect {connect}")
        results["no_certificate"] = {"listening": ports, "connect": connect}
    finally:
        stop()


def offload_fin_control(executable, log):
    """Without the FIN fix: an abrupt close, then the CPU and whether the next client is served."""
    start(executable, log, "--no-rdhup")
    try:
        idle = cpu_share(2.0)
        abrupt_after_request()
        share = cpu_share(3.0)
        try:
            response, seconds = timed_request(timeout=8.0)
            served = response.status == 200
        except (OSError, ssl.SSLError, suite.Closed):
            served, seconds = False, None
        results["offload_fin_control"] = {"idle_cpu_share": idle, "cpu_share_after_close": share,
                                          "next_client_served": served, "next_request_s": seconds}
    finally:
        stop()


def firefox(binary):
    """A real browser over TLS: the handshake as it measures it, and the header capture's lengths."""
    base = f"https://localhost:{PORT}"
    digest = hashlib.sha256(ssl.PEM_cert_to_DER_cert(CA.read_text(encoding="ascii"))).hexdigest().upper()
    fingerprint = ":".join(digest[index:index + 2] for index in range(0, len(digest), 2))
    with tempfile.TemporaryDirectory() as profile_dir:
        # What a user's "accept the risk" leaves in the profile. Marionette alone reports acceptInsecureCerts
        # false, and the certificate, CA:TRUE and self-signed, cannot be trusted as a server's otherwise.
        pathlib.Path(profile_dir, "cert_override.txt").write_text(
            "# PSM Certificate Override Settings file\n# This is a generated file!  Do not edit.\n"
            f"localhost:{PORT}:\tOID.2.16.840.1.101.3.4.2.1\t{fingerprint}\t\n", encoding="ascii")
        browser = subprocess.Popen([binary, "--headless", "--marionette", "--no-remote", "--profile",
                                    profile_dir], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            for _ in range(240):
                try:
                    client = firefox_capture.Marionette()
                    break
                except OSError:
                    time.sleep(1)
            else:
                raise SystemExit("Marionette did not start")
            client.call("WebDriver:NewSession", {"capabilities": {"alwaysMatch": {"acceptInsecureCerts": True}}})
            client.call("WebDriver:Navigate", {"url": f"{base}/probe/cookies"})
            timing = client.call("WebDriver:ExecuteScript", {"script": (
                "const n = performance.getEntriesByType('navigation')[0];"
                "return {secure: n.secureConnectionStart, connect: n.connectStart, connectEnd: n.connectEnd,"
                " request: n.requestStart, end: n.responseEnd, protocol: n.nextHopProtocol};"), "args": []})["value"]
            client.call("WebDriver:Navigate", {"url": f"{base}/probe/page"})
            text = ""
            for _ in range(120):
                text = client.call("WebDriver:ExecuteScript", {"script": firefox_capture.READ, "args": []})["value"]
                if text:
                    break
                time.sleep(0.5)
            capture = json.loads(text)
            version = subprocess.run([binary, "--version"], capture_output=True, text=True, check=False).stdout
        finally:
            browser.kill()
            browser.wait()
    handshake_ms = round(timing["connectEnd"] - timing["secure"], 1) if timing["secure"] else None
    results["firefox"] = {"browser": version.strip(), "tls_handshake_ms": handshake_ms, "timing": timing,
                          "capture": capture}
    check(capture.get("postStatus") == 200, f"{capture}")
    check(capture["post"]["cookie"] >= 100 and capture["post"]["request"] == 1, f"{capture}")


# A case that can fail on a profile for a limit recorded in docs/https-composition.md. It still runs and its
# result is recorded. W1 depends on where a request falls in a period of about 1 s, so the case can pass too.
KNOWN_LIMITS = {
    "wolf": {
        "deadline_then_silent_closes_before_inactivity":
            "W1, issue #280: the server reads a new connection's first request up to about 1 s late, so the first "
            "callback, and the deadline that runs from it, can start late",
    },
}


def run_cases(cases):
    failures = 0
    for case in cases:
        started = time.monotonic()
        name = case.__name__[5:]
        known = KNOWN_LIMITS.get(profile, {}).get(name)
        try:
            case()
            print(f"PASS {name} ({time.monotonic() - started:.1f}s){' despite a known limit' if known else ''}",
                  flush=True)
        except Exception as error:  # A failed case should not hide the others.
            if known:
                results.setdefault("known_limits", {})[name] = {"limit": known, "error": str(error)}
                print(f"KNOWN {name}: {known}: {type(error).__name__}: {error}", flush=True)
            else:
                failures += 1
                print(f"FAIL {name}: {type(error).__name__}: {error}", flush=True)
        time.sleep(suite.INACTIVITY_S + 0.5 if failures else 0.1)
    return failures


def main():
    global profile
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("executable", type=pathlib.Path)
    parser.add_argument("--profile", choices=("mbed", "wolf"), required=True)
    parser.add_argument("--results", type=pathlib.Path)
    parser.add_argument("--only", default="", help="case names, without case_; 'tls' for the TLS cases")
    parser.add_argument("--firefox", help="a Firefox binary, for the real browser check")
    args = parser.parse_args()
    profile = args.profile
    suite.PORT = PORT
    suite.TLS = tls_context()
    wanted = set(filter(None, args.only.split(",")))
    cases = [case for case in [c for c in suite.CASES if c.__name__ not in REPLACED] + TLS_CASES
             if not wanted or case.__name__[5:] in wanted or ("tls" in wanted and case in TLS_CASES)]
    log_path = args.results.with_suffix(".log") if args.results else pathlib.Path("/tmp/web_https.log")
    failures = 0
    selected = len(cases)
    with open(log_path, "w", encoding="utf-8") as log:
        start(args.executable, log)
        try:
            failures += run_cases(cases)
            if args.firefox:
                def case_firefox():
                    firefox(args.firefox)
                selected += 1
                failures += run_cases([case_firefox])
            if process.poll() is not None:
                failures += 1
                print(f"FAIL the image exited with {process.returncode}", flush=True)
        finally:
            stop()
    if not wanted or "separate" in wanted:
        for name, action in (("no_certificate", no_certificate), ("offload_fin_control", offload_fin_control)):
            selected += 1
            started = time.monotonic()
            with open(log_path.with_name(f"{log_path.stem}-{name}.log"), "w", encoding="utf-8") as own_log:
                try:
                    action(args.executable, own_log)
                    print(f"PASS {name} ({time.monotonic() - started:.1f}s)", flush=True)
                except (Exception, SystemExit) as error:
                    failures += 1
                    print(f"FAIL {name}: {type(error).__name__}: {error}", flush=True)
    combined = {"profile": profile, "suite": suite.results, "tls": results}
    print(json.dumps(combined, indent=2, sort_keys=True))
    if args.results:
        args.results.write_text(json.dumps(combined, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    known = len(results.get("known_limits", {}))
    print(f"{selected - failures - known} of {selected} cases passed on the {profile} profile, over TLS, "
          f"{known} failed on a known limit")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
