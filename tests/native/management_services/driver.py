#!/usr/bin/env python3
"""A real client for every management service on native_sim, over HTTPS (Issue #242).

The image composes Access, the credential store, security events, status, configuration, web sessions, the
network settings service, the TLS certificate module, the command line and the file service on real PSA
providers. It runs twice on one disk file, as separate processes:

- each boot prints one CASE line per case of the services that have no web route, which must all pass;
- the first boot generates the TLS certificate module's certificate, which OpenSSL checks, and the second
  loads the same one;
- the first boot then serves HTTPS, and this client runs permitted, unauthenticated and unpermitted
  requests through every route the components define, and checks that nothing else is served.

The entropy is test-only and the listener's certificate is the declared test certificate, whose key is public.
Timings come from an emulated container. Nothing here is device evidence.

    driver.py <zephyr.exe> --profile mbed|wolf [--output dir]
"""
import argparse
import gzip
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
PORT = 18242
ORIGIN = f"https://{NAME}:{PORT}"
HOST_HEADER = f"{NAME}:{PORT}"
REPOSITORY = pathlib.Path(__file__).resolve().parents[3]
TEST_CERTIFICATE = REPOSITORY / "tests/native/tls_offload_probe/credentials/test-server.crt"
ADMIN = ("admin", "composition test password")
VIEWER = ("viewer1", "a password changed at first login")
GUEST = ("guest1", "a password changed at first login")
EXPECTED_CASES = {
    "cli_permitted", "cli_unpermitted", "cli_unauthenticated", "cli_line_rejected_event", "cli_network_recover",
    "file_permitted", "file_unpermitted", "file_unauthenticated",
    "tls_certificate_permitted", "tls_certificate_unpermitted", "tls_certificate_unauthenticated",
    "network_permitted", "network_unpermitted", "network_unauthenticated", "network_boot",
    "config_update_event", "status_event_counts",
}

results = {"cases": {}, "web": {}}
failures = []


def check(condition, message):
    results["web"][message] = bool(condition)
    if not condition:
        failures.append(message)
        print(f"FAIL: {message}")
    return condition


class Response:
    def __init__(self, status, headers, body):
        self.status = status
        self.headers = headers
        self.body = body

    def values(self, name):
        return [value for key, value in self.headers if key == name]

    def json(self):
        try:
            return json.loads(self.body.decode()) if self.body else None
        except ValueError:
            return None


def tls_context():
    context = ssl.create_default_context(cafile=str(TEST_CERTIFICATE))
    context.minimum_version = ssl.TLSVersion.TLSv1_3
    return context


def exchange(raw):
    """One request on a new TLS connection, and one response."""
    with socket.create_connection((HOST, PORT), timeout=30) as plain:
        with tls_context().wrap_socket(plain, server_hostname=NAME) as sock:
            sock.sendall(raw)
            buffer = b""
            try:
                while True:
                    data = sock.recv(65536)
                    if not data:
                        break
                    buffer += data
                    if b"\r\n\r\n" in buffer:
                        head, rest = buffer.split(b"\r\n\r\n", 1)
                        lengths = re.findall(rb"(?im)^content-length:\s*(\d+)", head)
                        if lengths and len(rest) >= int(lengths[0]):
                            break
                        if re.search(rb"(?im)^transfer-encoding:\s*chunked", head) and rest.endswith(b"0\r\n\r\n"):
                            break
            except (ssl.SSLError, ConnectionError, socket.timeout):
                pass
    if b"\r\n\r\n" not in buffer:
        return Response(0, [], buffer)
    head, body = buffer.split(b"\r\n\r\n", 1)
    lines = head.decode("latin-1").split("\r\n")
    headers = []
    for line in lines[1:]:
        name, value = line.split(":", 1)
        headers.append((name.strip().lower(), value.strip()))
    if dict(headers).get("transfer-encoding") == "chunked":
        decoded = b""
        while body:
            size_line, body = body.split(b"\r\n", 1)
            size = int(size_line, 16)
            if size == 0:
                break
            decoded += body[:size]
            body = body[size + 2:]
        body = decoded
    if dict(headers).get("content-encoding") == "gzip":
        body = gzip.decompress(body)
    return Response(int(lines[0].split()[1]), headers, body)


def request(method, path, headers=(), body=None):
    lines = [f"{method} {path} HTTP/1.1", f"Host: {HOST_HEADER}", "Connection: close"]
    lines += [f"{name}: {value}" for name, value in headers]
    payload = b""
    if body is not None:
        payload = body if isinstance(body, bytes) else json.dumps(body).encode()
        lines += ["Content-Type: application/json", f"Content-Length: {len(payload)}"]
    return exchange(("\r\n".join(lines) + "\r\n\r\n").encode("latin-1") + payload)


class Jar:
    def __init__(self, reply):
        self.session = self.xsrf = None
        for line in reply.values("set-cookie"):
            name, rest = line.split("=", 1)
            if name == "crux_session":
                self.session = rest.split(";", 1)[0]
            elif name == "XSRF-TOKEN":
                self.xsrf = rest.split(";", 1)[0]

    def headers(self, method, token=True):
        headers = [("Cookie", f"crux_session={self.session}; XSRF-TOKEN={self.xsrf}")]
        if method != "GET":
            headers.append(("Origin", ORIGIN))
            if token:
                headers.append(("X-XSRF-TOKEN", self.xsrf))
        return headers


def login(credentials):
    reply = request("POST", "/api/v1/session", [("Origin", ORIGIN)],
                    {"user": credentials[0], "password": credentials[1]})
    if reply.status != 202:
        return None
    attempt = reply.json()["id"]
    started = time.monotonic()
    while time.monotonic() - started < 60:
        final = request("GET", f"/api/v1/session/attempt/{attempt}", [("X-Crux-Request", "1")])
        if final.status == 200 and final.json() == {"state": "pending"}:
            time.sleep(0.1)
            continue
        if final.status == 200 and final.json() == {"state": "success"}:
            return Jar(final)
        return None
    return None


def error_is(reply, status, code):
    return reply.status == status and reply.json() == {"code": code}


def status_value(reply, name):
    for item in (reply.json() or {}).get("items", []):
        if item.get("name") == name:
            return item.get("value")
    return None


def web_cases():
    admin, viewer, guest = login(ADMIN), login(VIEWER), login(GUEST)
    check(admin and viewer and guest, "login: two requests, through the real Access core and PBKDF2")
    if not (admin and viewer and guest):
        return

    # The listener serves the declared test certificate, over TLS 1.3 only.
    with socket.create_connection((HOST, PORT), timeout=30) as plain:
        with tls_context().wrap_socket(plain, server_hostname=NAME) as sock:
            served = sock.getpeercert(binary_form=True)
            results["web"]["tls_version"] = sock.version()
            results["web"]["tls_cipher"] = sock.cipher()[0]
    declared = ssl.PEM_cert_to_DER_cert(TEST_CERTIFICATE.read_text())
    check(served == declared, "the listener serves the declared test certificate")
    check(results["web"]["tls_version"] == "TLSv1.3", "TLS 1.3")

    # Sessions.
    me = request("GET", "/api/v1/session/me", admin.headers("GET"))
    check(me.status == 200 and me.json().get("user") == "admin", "session/me permitted")
    check(error_is(request("GET", "/api/v1/session/me"), 401, "unauthorized"), "session/me unauthenticated")

    # Status: everything for the administrator, nothing for the guest.
    before = request("GET", "/api/v1/status", admin.headers("GET"))
    emitted_before = status_value(before, "security.events_emitted")
    check(before.status == 200 and isinstance(emitted_before, int), "status permitted, with the event counts")
    check(error_is(request("GET", "/api/v1/status"), 401, "unauthorized"), "status unauthenticated")
    hidden = request("GET", "/api/v1/status", guest.headers("GET"))
    check(hidden.status == 200 and hidden.json() == {"items": []}, "status unpermitted: every item hidden")

    # Configuration: an update records its event, and status counts it.
    name = "demo.display_name"
    update = request("PUT", f"/api/v1/config/{name}", admin.headers("PUT"), {"value": "set over https"})
    check(update.status == 200 and update.json().get("name") == name, f"config update permitted {update.body}")
    read = request("GET", f"/api/v1/config/{name}", admin.headers("GET"))
    check(read.status == 200 and read.json().get("value") == "set over https", "config read back")
    check(error_is(request("PUT", f"/api/v1/config/{name}", body={"value": "x"}), 401, "unauthorized"),
          "config update unauthenticated")
    refused = request("PUT", f"/api/v1/config/{name}", viewer.headers("PUT"), {"value": "x"})
    check(refused.status in (403, 404), f"config update unpermitted: {refused.status}")
    check(error_is(request("PUT", f"/api/v1/config/{name}", admin.headers("PUT", token=False), {"value": "x"}),
                   403, "csrf_rejected"), "config update without its CSRF token")
    listed = request("GET", "/api/v1/config", viewer.headers("GET"))
    check(listed.status == 200 and listed.json().get("items") == [], "config list unpermitted: nothing listed")
    time.sleep(1.5)
    after = request("GET", "/api/v1/status", admin.headers("GET"))
    emitted_after = status_value(after, "security.events_emitted")
    check(isinstance(emitted_after, int) and emitted_after > emitted_before,
          f"status counts the update's event: {emitted_before} then {emitted_after}")
    check(status_value(after, "security.events_dropped") == 0, "no event dropped")
    results["web"]["events_emitted"] = [emitted_before, emitted_after]

    # Save: no Configuration provider, decision C6. Queued, then reported as not supported.
    save = request("POST", "/api/v1/config/save", admin.headers("POST"))
    check(save.status == 202 and save.json() == {"state": "pending"}, "save queued")
    state = None
    for _ in range(50):
        state = (request("GET", "/api/v1/config", admin.headers("GET")).json() or {}).get("save")
        if state and state.get("state") != "pending":
            break
        time.sleep(0.2)
    check(state == {"state": "failed", "code": "not_supported"}, f"save reports not_supported: {state}")
    check(error_is(request("POST", "/api/v1/config/save", viewer.headers("POST")), 403, "permission_denied"),
          "save unpermitted")

    # Network settings: read with network.read, and a change cannot be saved here.
    network = request("GET", "/api/v1/network", viewer.headers("GET"))
    check(network.status == 200, f"network read permitted {network.body}")
    check(error_is(request("GET", "/api/v1/network", guest.headers("GET")), 403, "permission_denied"),
          "network read unpermitted")
    check(error_is(request("GET", "/api/v1/network"), 401, "unauthorized"), "network read unauthenticated")
    change = {"address": "192.0.2.20", "prefix_length": 24, "gateway": "192.0.2.1"}
    check(error_is(request("PUT", "/api/v1/network", admin.headers("PUT"), change), 501, "not_supported"),
          "network change: 501, no Configuration provider")
    check(error_is(request("PUT", "/api/v1/network", viewer.headers("PUT"), change), 403, "permission_denied"),
          "network change unpermitted")

    # The packaged folder, and nothing beyond the components' routes.
    page = request("GET", "/", [("Accept-Encoding", "gzip")])
    check(page.status == 200 and b"Issue #242" in page.body, "the packaged folder's index")
    for path in ("/probe/echo", "/api/v1/test/admin", "/api/v1/events", "/api/v1/files/files",
                 "/api/v1/tls/certificate"):
        check(request("GET", path, admin.headers("GET")).status == 404, f"no route at {path}")

    # Logout ends the session for every route.
    check(request("DELETE", "/api/v1/session", guest.headers("DELETE")).status in (200, 204), "logout")
    check(error_is(request("GET", "/api/v1/status", guest.headers("GET")), 401, "unauthorized"),
          "an ended session is unauthenticated")

    # No plain HTTP answer on the port.
    with socket.create_connection((HOST, PORT), timeout=10) as plain:
        plain.sendall(f"GET / HTTP/1.1\r\nHost: {HOST_HEADER}\r\n\r\n".encode())
        plain.settimeout(5)
        try:
            answer = plain.recv(1024)
        except (ConnectionError, socket.timeout):
            answer = b""
    check(not answer.startswith(b"HTTP/"), "no plain HTTP answer")


def boot(executable, disk, seed, log_path, serve):
    log = open(log_path, "w")
    process = subprocess.Popen([str(executable), f"--flash={disk}", f"--seed={seed}"], stdout=log,
                               stderr=subprocess.STDOUT)
    try:
        started = time.monotonic()
        text = ""
        while time.monotonic() - started < 180:
            text = pathlib.Path(log_path).read_text(errors="replace")
            if "CRUX_SERVICES_READY" in text or "START_FAILED" in text or process.poll() is not None:
                break
            time.sleep(0.5)
        if serve and "CRUX_SERVICES_READY" in text:
            time.sleep(0.5)
            web_cases()
        return pathlib.Path(log_path).read_text(errors="replace")
    finally:
        process.terminate()
        try:
            process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            process.kill()
        log.close()


def read_cases(text, label):
    seen = {}
    for name, verdict in re.findall(r"^CASE (\S+) (ok|FAIL)", text, re.MULTILINE):
        seen[name] = verdict == "ok"
    results["cases"][label] = seen
    missing = EXPECTED_CASES - set(seen)
    if missing:
        failures.append(f"{label}: cases missing: {sorted(missing)}")
    for name, passed in seen.items():
        if not passed:
            failures.append(f"{label}: CASE {name} failed")
    match = re.search(r"^CERTIFICATE generated=(\d) fingerprint=(\S+)", text, re.MULTILINE)
    der = re.search(r"^CERTIFICATE der=([0-9a-f]+)", text, re.MULTILINE)
    if not (match and der):
        failures.append(f"{label}: no certificate line")
        return None
    return match.group(1) == "1", match.group(2), bytes.fromhex(der.group(1))


def openssl_checks(der, fingerprint, directory):
    path = directory / "certificate.der"
    path.write_bytes(der)
    text = subprocess.run(["openssl", "x509", "-inform", "DER", "-in", str(path), "-noout", "-text"],
                          capture_output=True, text=True).stdout
    pem = directory / "certificate.pem"
    subprocess.run(["openssl", "x509", "-inform", "DER", "-in", str(path), "-out", str(pem)], check=False)
    verified = subprocess.run(["openssl", "verify", "-purpose", "sslserver", "-CAfile", str(pem), str(pem)],
                              capture_output=True, text=True)
    theirs = subprocess.run(["openssl", "x509", "-in", str(pem), "-noout", "-fingerprint", "-sha256"],
                            capture_output=True, text=True).stdout.strip().split("=", 1)[-1]
    found = {
        "parses": "Version: 3" in text,
        "ecdsa_with_sha256": "ecdsa-with-SHA256" in text,
        "p256": "NIST CURVE: P-256" in text,
        "alternative_names": "DNS:localhost, IP Address:127.0.0.1" in text,
        "no_expiry": "Not After : Dec 31 23:59:59 9999 GMT" in text,
        "self_signed_verifies": verified.returncode == 0,
        "fingerprint_matches": theirs == fingerprint,
    }
    results["certificate_openssl"] = found
    for name, passed in found.items():
        if not passed:
            failures.append(f"OpenSSL on the generated certificate: {name}")


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("executable", type=pathlib.Path)
    parser.add_argument("--profile", required=True, choices=("mbed", "wolf"))
    parser.add_argument("--output", type=pathlib.Path, default=pathlib.Path("."))
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    results["profile"] = args.profile
    with tempfile.TemporaryDirectory() as temporary:
        directory = pathlib.Path(temporary)
        disk = directory / "disk.bin"
        first = read_cases(boot(args.executable, disk, 2421, args.output / "services-first.log", True), "first")
        later = read_cases(boot(args.executable, disk, 2422, args.output / "services-later.log", False), "later")
        if first:
            check(first[0], "the first boot generated the TLS certificate module's certificate")
            openssl_checks(first[2], first[1], directory)
        if first and later:
            check(not later[0] and later[1] == first[1] and later[2] == first[2],
                  "the later boot loaded the same certificate from the store")
    results["failures"] = failures
    (args.output / "services-results.json").write_text(json.dumps(results, indent=1) + "\n")
    total = len(results["web"]) + sum(len(cases) for cases in results["cases"].values())
    print(f"services image, {args.profile} profile: {len(failures)} failures, {total} checks recorded, "
          f"results in {args.output / 'services-results.json'}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
