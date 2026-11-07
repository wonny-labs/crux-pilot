#!/usr/bin/env python3
"""What a real client sees of a packaged example on native_sim, with no browser (Issue #220).

The image is tests/native/management_services/e2e with one example packaged in it. This client checks what only
a real server shows, and that the browser cases in web/e2e do not show as directly:

- TLS 1.3 only: OpenSSL's TLS 1.2 handshake is refused, and its TLS 1.3 handshake verifies the declared test
  certificate; a plain HTTP request on the port gets no HTTP answer;
- every asset in the packager's hash list: the bytes served are the listed bytes, with the content policy of the
  example's style, nosniff, no-referrer and the cache header of its class, and `/` serves the index;
- hash routes: the paths the interface routes in its fragment are not served, so there is no deep-link fallback;
- the API's own answers keep the core's policy and `no-store`;
- connection limits: three idle connections in every client slot, a stalled handshake, two of them, the real
  plain 409 for a route another client holds, the real 503 busy of a full login table, and the idle limit on
  the real session clock.

The entropy is test-only and the listener's certificate is the declared test certificate, whose key is public.
Timings come from an emulated container. Nothing here is device evidence.

    server_checks.py <zephyr.exe> --build <build dir> --style strict|inline --profile mbed|wolf --app <name>
                     [--output dir] [--idle-s 30]
"""
import argparse
import gzip
import hashlib
import json
import pathlib
import re
import socket
import ssl
import subprocess
import sys
import tempfile
import time

HOST = "127.0.0.1"
NAME = "localhost"
PORT = 18220
ORIGIN = f"https://{NAME}:{PORT}"
REPOSITORY = pathlib.Path(__file__).resolve().parents[4]
TEST_CERTIFICATE = REPOSITORY / "tests/native/tls_offload_probe/credentials/test-server.crt"
PASSWORD = "example-password-not-real"
# The two policies of docs/web-assets.md, and the core's own for its JSON answers.
ASSET_POLICY = {
    "strict": ("default-src 'self'; script-src 'self'; style-src 'self'; base-uri 'self'; form-action 'self'; "
               "frame-ancestors 'none'"),
    "inline": ("default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; base-uri 'self'; "
               "form-action 'self'; frame-ancestors 'none'"),
}
CORE_POLICY = "default-src 'self'; frame-ancestors 'none'"
CACHE = {"immutable": "max-age=31536000, immutable", "revalidate": "no-cache"}
# The paths that the examples route in their fragment. A server with a fallback would answer them with the index.
FRAGMENT_ROUTES = ("/config", "/home", "/network", "/signin", "/password", "/reboot", "/index.html/config")
# The image's bounds: TLS connect timeout, inactivity timer, and the slack of the emulated container, which
# the HTTPS precedent declares as 1.5 s, and 2.5 s on Wolf for W1 (#280).
HANDSHAKE_TIMEOUT_S = 2.0
INACTIVITY_S = 3.0
SLACK_S = {"mbed": 1.5, "wolf": 2.5}

results = {"checks": {}, "measured": {}}
failures = []


def check(condition, message):
    results["checks"][message] = bool(condition)
    if not condition:
        failures.append(message)
        print(f"FAIL: {message}")
    return condition


# The pure checks, which tests/build/test_e2e_server_checks.py holds to mutated answers.

def asset_errors(asset, status, headers, body, style):
    """What is wrong with one asset's answer, against its entry in the packager's hash list."""
    path = asset["path"]
    errors = []
    if status != 200:
        errors.append(f"{path}: status {status}")
    if headers.get("content-security-policy") != ASSET_POLICY[style]:
        errors.append(f"{path}: policy {headers.get('content-security-policy')!r}, not the {style} one")
    policy = headers.get("content-security-policy") or ""
    if "frame-ancestors 'none'" not in policy or "script-src 'self'" not in policy:
        errors.append(f"{path}: scripts must stay 'self' and frame-ancestors 'none'")
    if headers.get("x-content-type-options") != "nosniff":
        errors.append(f"{path}: no nosniff")
    if headers.get("referrer-policy") != "no-referrer":
        errors.append(f"{path}: referrer policy {headers.get('referrer-policy')!r}")
    if headers.get("cache-control") != CACHE[asset["cache"]]:
        errors.append(f"{path}: cache {headers.get('cache-control')!r}, and its class is {asset['cache']}")
    if (headers.get("content-encoding") == "gzip") != (asset["encoding"] == "gzip"):
        errors.append(f"{path}: encoding {headers.get('content-encoding')!r}, listed {asset['encoding']}")
    if hashlib.sha256(body).hexdigest() != asset["served_sha256"]:
        errors.append(f"{path}: the bytes served are not the listed bytes")
    return errors


def fallback_errors(path, status, body, index):
    """A path the interface routes in its fragment must not be served, least of all with the index."""
    errors = []
    if status != 404:
        errors.append(f"{path}: status {status}, so the server answers a fragment route")
    if body and index and (body == index or b"<script" in body):
        errors.append(f"{path}: served the index, a deep-link fallback")
    return errors


def tls_errors(tls12, tls13):
    """tls12 and tls13 are what OpenSSL saw: whether a session was established, and with which protocol."""
    errors = []
    if tls12.get("established"):
        errors.append(f"TLS 1.2 was accepted: {tls12.get('protocol')}")
    if not tls13.get("established") or tls13.get("protocol") != "TLSv1.3":
        errors.append(f"TLS 1.3 was not established: {tls13}")
    if not tls13.get("verified"):
        errors.append("the TLS 1.3 handshake did not verify the declared test certificate")
    return errors


def plain_errors(answer):
    """The bytes a plain HTTP request got. Any HTTP answer is a plain listener."""
    return ["a plain HTTP request got an HTTP answer"] if answer.startswith(b"HTTP/") else []


def core_errors(status, headers):
    errors = []
    if headers.get("content-security-policy") != CORE_POLICY:
        errors.append(f"API answer {status}: policy {headers.get('content-security-policy')!r}")
    if headers.get("cache-control") != "no-store":
        errors.append(f"API answer {status}: cache {headers.get('cache-control')!r}")
    if headers.get("x-content-type-options") != "nosniff":
        errors.append(f"API answer {status}: no nosniff")
    return errors


# The client.

def tls_context():
    context = ssl.create_default_context(cafile=str(TEST_CERTIFICATE))
    context.minimum_version = ssl.TLSVersion.TLSv1_3
    return context


def connect():
    plain = socket.create_connection((HOST, PORT), timeout=30)
    return tls_context().wrap_socket(plain, server_hostname=NAME)


class Response:
    def __init__(self, status, headers, body, raw):
        self.status, self.headers, self.body, self.raw = status, headers, body, raw

    def json(self):
        try:
            return json.loads(self.body.decode())
        except ValueError:
            return None


def parse(buffer):
    if b"\r\n\r\n" not in buffer:
        return None
    head, body = buffer.split(b"\r\n\r\n", 1)
    lines = head.decode("latin-1").split("\r\n")
    headers, cookies = {}, []
    for line in lines[1:]:
        name, value = line.split(":", 1)
        if name.strip().lower() == "set-cookie":
            cookies.append(value.strip())
        else:
            headers[name.strip().lower()] = value.strip()
    if headers.get("transfer-encoding") == "chunked":
        decoded = b""
        while True:
            if b"\r\n" not in body:
                return None
            size_line, rest = body.split(b"\r\n", 1)
            size = int(size_line, 16)
            if size == 0:
                break
            if len(rest) < size + 2:
                return None
            decoded += rest[:size]
            body = rest[size + 2:]
        body = decoded
    elif "content-length" in headers and len(body) < int(headers["content-length"]):
        return None
    headers["set-cookie"] = cookies
    return Response(int(lines[0].split()[1]), headers, body, buffer)


def read_response(sock):
    buffer = b""
    try:
        while True:
            data = sock.recv(65536)
            if not data:
                break
            buffer += data
            if parse(buffer):
                break
    except (ssl.SSLError, ConnectionError, socket.timeout):
        pass
    return parse(buffer) or Response(0, {"set-cookie": []}, buffer, buffer)


def request(method, path, headers=(), body=None, keep=False):
    lines = [f"{method} {path} HTTP/1.1", f"Host: {NAME}:{PORT}"] + ([] if keep else ["Connection: close"])
    lines += [f"{name}: {value}" for name, value in headers]
    payload = b""
    if body is not None:
        payload = json.dumps(body).encode()
        lines += ["Content-Type: application/json", f"Content-Length: {len(payload)}"]
    with connect() as sock:
        sock.sendall(("\r\n".join(lines) + "\r\n\r\n").encode("latin-1") + payload)
        return read_response(sock)


def cookie(lines, name):
    for line in lines:
        if line.startswith(name + "="):
            return line.split(";", 1)[0].split("=", 1)[1]
    return None


def login(user, password=PASSWORD, collect=True):
    queued = request("POST", "/api/v1/session", [("Origin", ORIGIN)], {"user": user, "password": password})
    if queued.status != 202:
        return queued
    if not collect:
        return queued
    attempt = queued.json()["id"]
    for _ in range(300):
        poll = request("GET", f"/api/v1/session/attempt/{attempt}", [("X-Crux-Request", "1")])
        if poll.status == 200 and poll.json() == {"state": "pending"}:
            time.sleep(0.1)
            continue
        return poll
    return poll


def session_headers(poll, method="GET"):
    session, xsrf = cookie(poll.headers["set-cookie"], "crux_session"), cookie(poll.headers["set-cookie"], "XSRF-TOKEN")
    headers = [("Cookie", f"crux_session={session}; XSRF-TOKEN={xsrf}")]
    if method != "GET":
        headers += [("Origin", ORIGIN), ("X-XSRF-TOKEN", xsrf)]
    return headers


def openssl(version):
    run = subprocess.run(["openssl", "s_client", "-connect", f"{HOST}:{PORT}", "-servername", NAME,
                          "-CAfile", str(TEST_CERTIFICATE), "-verify_return_error", version, "-brief"],
                         input=b"", capture_output=True, timeout=60)
    text = (run.stdout + run.stderr).decode(errors="replace")
    protocol = re.search(r"Protocol version: (\S+)", text)
    return {"established": run.returncode == 0 and protocol is not None,
            "protocol": protocol.group(1) if protocol else None,
            "verified": "Verification: OK" in text, "output": text[-600:]}


def timed_get(path="/favicon.svg"):
    started = time.monotonic()
    reply = request("GET", path)
    return reply, time.monotonic() - started


def tls_and_listener():
    tls12, tls13 = openssl("-tls1_2"), openssl("-tls1_3")
    results["measured"]["openssl_tls1_2"] = tls12
    results["measured"]["openssl_tls1_3"] = tls13
    for error in tls_errors(tls12, tls13) or ["none"]:
        check(error == "none", f"TLS: {error}" if error != "none" else "TLS 1.3 only, with the declared certificate")
    with socket.create_connection((HOST, PORT), timeout=10) as plain:
        plain.sendall(f"GET / HTTP/1.1\r\nHost: {NAME}:{PORT}\r\n\r\n".encode())
        plain.settimeout(5)
        try:
            answer = plain.recv(1024)
        except (ConnectionError, socket.timeout):
            answer = b""
    check(not plain_errors(answer), "no plain HTTP answer on the port")


def assets(build, style):
    listed = json.loads((build / "crux_web_assets/app/crux-web-assets.json").read_text())
    check(listed["style"] == style, f"the hash list records the {style} style")
    served = []
    index = None
    for asset in listed["assets"]:
        reply = request("GET", asset["path"])
        errors = asset_errors(asset, reply.status, reply.headers, reply.body, style)
        for error in errors:
            failures.append(error)
            print(f"FAIL: {error}")
        served.append({"path": asset["path"], "cache": reply.headers.get("cache-control"),
                       "encoding": reply.headers.get("content-encoding"), "bytes": len(reply.body),
                       "errors": errors})
        if asset["path"] == "/index.html":
            index = gzip.decompress(reply.body) if asset["encoding"] == "gzip" else reply.body
    check(all(not entry["errors"] for entry in served), f"every listed asset, {len(served)}, as listed, with its headers")
    results["measured"]["assets"] = served
    root = request("GET", "/")
    body = gzip.decompress(root.body) if root.headers.get("content-encoding") == "gzip" else root.body
    check(root.status == 200 and body == index, "/ serves the index")
    for path in FRAGMENT_ROUTES:
        reply = request("GET", path)
        errors = fallback_errors(path, reply.status, reply.body, index)
        check(not errors, f"no fallback at {path}: {reply.status}")
    api = request("GET", "/api/v1/status")
    check(api.status == 401 and not core_errors(api.status, api.headers), "the API's 401 keeps the core's headers")


def limits(profile, idle_s):
    slack = SLACK_S[profile]
    _, alone = timed_get()
    results["measured"]["request_alone_s"] = round(alone, 3)

    # Three idle keep-alive connections take every client slot, and the server stops polling the listener.
    held = []
    for _ in range(3):
        sock = connect()
        sock.sendall(f"GET /favicon.svg HTTP/1.1\r\nHost: {NAME}:{PORT}\r\n\r\n".encode())
        held.append((sock, read_response(sock).status))
    reply, waited = timed_get()
    for sock, _ in held:
        sock.close()
    results["measured"]["fourth_client_behind_three_idle_s"] = round(waited, 3)
    check([status for _, status in held] == [200, 200, 200] and reply.status == 200 and
          waited <= INACTIVITY_S + slack, f"a fourth client is served within one inactivity period: {waited:.2f} s")

    # A stalled handshake holds the server thread for at most the TLS connect timeout.
    for stallers in (1, 2):
        silent = [socket.create_connection((HOST, PORT), timeout=10) for _ in range(stallers)]
        time.sleep(0.2)
        reply, waited = timed_get()
        for sock in silent:
            sock.close()
        results["measured"][f"request_behind_{stallers}_stalled_handshakes_s"] = round(waited, 3)
        check(reply.status == 200 and waited <= stallers * HANDSHAKE_TIMEOUT_S + slack,
              f"{stallers} stalled handshake(s): served in {waited:.2f} s")
    time.sleep(INACTIVITY_S)

    # The real plain 409: a client holds a parameter's route with part of a body, and another client asks.
    writer = login("writer")
    check(writer.status == 200 and writer.json() == {"state": "success"}, "writer signs in")
    operator = login("operator")
    holder = connect()
    body = b'{"value":7}'
    head = [f"PUT /api/v1/config/report_interval HTTP/1.1", f"Host: {NAME}:{PORT}",
            "Content-Type: application/json", f"Content-Length: {len(body)}"]
    head += [f"{name}: {value}" for name, value in session_headers(writer, "PUT")]
    holder.sendall(("\r\n".join(head) + "\r\n\r\n").encode() + body[:4])
    time.sleep(0.3)
    blocked = request("PUT", "/api/v1/config/report_interval", session_headers(operator, "PUT"), {"value": 9})
    results["measured"]["held_route"] = {"status": blocked.status, "raw": blocked.raw[:120].decode("latin-1")}
    check(blocked.status == 409 and blocked.body == b"", "a held route answers the server's plain 409, with no body")
    holder.close()
    time.sleep(0.2)
    freed = request("PUT", "/api/v1/config/report_interval", session_headers(operator, "PUT"), {"value": 9})
    check(freed.status == 200, f"the route is free once the holder leaves: {freed.status}")

    # The real 503 busy: four results that nobody collects hold every attempt slot.
    queued = []
    for _ in range(4):
        queued.append(login("viewer", collect=False).status)
        time.sleep(0.8)
    refused = login("viewer", collect=False)
    results["measured"]["full_login_table"] = {"queued": queued, "status": refused.status,
                                               "retry_after": refused.headers.get("retry-after"),
                                               "body": refused.body.decode(errors="replace")}
    check(queued == [202] * 4 and refused.status == 503 and refused.json() == {"code": "busy"} and
          refused.headers.get("retry-after"), "a full login table answers 503 busy, with Retry-After")
    started = time.monotonic()
    while login("viewer", collect=False).status == 503 and time.monotonic() - started < 40:
        time.sleep(1)
    results["measured"]["login_table_free_after_s"] = round(time.monotonic() - started, 1)
    time.sleep(22)

    # The real session clock: GET does not refresh the idle timer.
    viewer = login("viewer")
    started = time.monotonic()
    statuses = []
    while time.monotonic() - started < idle_s + 3:
        statuses.append(request("GET", "/api/v1/status", session_headers(viewer)).status)
        time.sleep(5)
    after = request("GET", "/api/v1/status", session_headers(viewer))
    results["measured"]["idle_expiry"] = {"idle_s": idle_s, "polled": statuses, "after": after.status}
    check(statuses[0] == 200 and after.status == 401, f"a session polled with GET ends at the idle limit, {idle_s} s")


def port_free():
    """Something else on the port would answer in the image's place."""
    try:
        socket.create_connection((HOST, PORT), timeout=2).close()
    except OSError:
        return True
    return False


def boot(executable, disk, seed, log_path):
    if not port_free():
        raise SystemExit(f"something already listens on port {PORT}: stop it first")
    log = open(log_path, "w")
    process = subprocess.Popen([str(executable), f"--flash={disk}", f"--seed={seed}"], stdout=log,
                               stderr=subprocess.STDOUT)
    started = time.monotonic()
    while time.monotonic() - started < 240:
        text = pathlib.Path(log_path).read_text(errors="replace")
        if "CRUX_E2E_READY" in text or "CRUX_E2E_START_FAILED" in text or process.poll() is not None:
            break
        time.sleep(0.2)
    results["measured"].setdefault("boot_s", []).append(round(time.monotonic() - started, 2))
    return process, log, "CRUX_E2E_READY" in pathlib.Path(log_path).read_text(errors="replace")


def stop(process, log):
    process.terminate()
    try:
        process.wait(timeout=10)
    except subprocess.TimeoutExpired:
        process.kill()
    log.close()


def revision():
    """The commit and whether the tree had changes, so that a retained result names what produced it."""
    def git(*arguments):
        result = subprocess.run(["git", "-C", str(REPOSITORY), *arguments], capture_output=True, text=True)
        return result.stdout.strip() if result.returncode == 0 else None
    head = git("rev-parse", "HEAD")
    status = git("status", "--porcelain")
    return {"revision": head or "unknown", "dirty": None if status is None else bool(status)}


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("executable", type=pathlib.Path)
    parser.add_argument("--build", type=pathlib.Path, required=True)
    parser.add_argument("--style", required=True, choices=sorted(ASSET_POLICY))
    parser.add_argument("--profile", required=True, choices=sorted(SLACK_S))
    parser.add_argument("--app", required=True)
    parser.add_argument("--idle-s", type=int, default=30)
    parser.add_argument("--output", type=pathlib.Path, default=pathlib.Path("."))
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    results.update({"app": args.app, "profile": args.profile, "style": args.style, **revision()})
    with tempfile.TemporaryDirectory() as temporary:
        disk = pathlib.Path(temporary) / "disk.bin"
        process, log, ready = boot(args.executable, disk, 2201, args.output / f"server-{args.app}-first.log")
        stop(process, log)
        check(ready, "the first boot creates the accounts and serves")
        process, log, ready = boot(args.executable, disk, 2202, args.output / f"server-{args.app}.log")
        try:
            if check(ready, "a later boot finds the accounts and serves"):
                time.sleep(0.5)
                tls_and_listener()
                assets(args.build, args.style)
                limits(args.profile, args.idle_s)
        finally:
            stop(process, log)
    results["failures"] = failures
    path = args.output / f"server-{args.app}-results.json"
    path.write_text(json.dumps(results, indent=1) + "\n")
    print(f"server checks, {args.app} on the {args.profile} profile: {len(failures)} failures, "
          f"{len(results['checks'])} checks, results in {path}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
