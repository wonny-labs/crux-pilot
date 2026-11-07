#!/usr/bin/env python3
"""A real client for login and sessions on native_sim, over plain HTTP on loopback.

The image runs the management composition of #242, so crux_access, its PBKDF2 verifier, its session table
and its store are the real ones, on real PSA ports, and the session module and the web adapter are the
product sources. The entropy is test-only, so nothing here shows a token's strength, and the timings come
from an emulated container on a loaded machine: none of this is device evidence.

    test_driver.py <zephyr.exe> [--results file]
"""
import argparse
import json
import pathlib
import re
import socket
import subprocess
import sys
import tempfile
import threading
import time

HOST = "127.0.0.1"
PORT = 18186
ORIGIN = f"http://{HOST}:{PORT}"
HOST_HEADER = f"{HOST}:{PORT}"
ADMIN = ("admin", "composition test password")
VIEWER = ("viewer", "session test viewer password")
CHANGED = "a changed viewer password"
TOKEN = re.compile(r"^[A-Za-z0-9_-]{43}$")
SECURITY_HEADERS = {
    "content-security-policy": "default-src 'self'; frame-ancestors 'none'",
    "x-content-type-options": "nosniff",
    "referrer-policy": "no-referrer",
    "cache-control": "no-store",
}
# Allowance for scheduling in an emulated container.
SLACK_S = 1.5

results = {}
failures = []


def check(condition, message):
    if not condition:
        failures.append(message)
        print(f"FAIL: {message}")
    return condition


class Response:
    def __init__(self, status, headers, body, closed):
        self.status = status
        self.headers = headers  # list of (lower-case name, value)
        self.body = body
        self.closed = closed

    def values(self, name):
        return [value for key, value in self.headers if key == name]

    def json(self):
        return json.loads(self.body.decode()) if self.body else None


def exchange(raw, wait_close=0.5):
    """Send raw request bytes on a new connection and read one response."""
    sock = socket.create_connection((HOST, PORT), timeout=30)
    sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    sock.sendall(raw)
    buffer = b""
    closed = False

    def fill():
        nonlocal buffer, closed
        data = sock.recv(65536)
        if not data:
            closed = True
            raise ConnectionError("closed")
        buffer += data

    try:
        while b"\r\n\r\n" not in buffer:
            fill()
        head, buffer = buffer.split(b"\r\n\r\n", 1)
        lines = head.decode("latin-1").split("\r\n")
        status = int(lines[0].split()[1])
        headers = []
        for line in lines[1:]:
            name, value = line.split(":", 1)
            headers.append((name.strip().lower(), value.strip()))
        names = dict(headers)
        body = b""
        if names.get("transfer-encoding") == "chunked":
            while True:
                while b"\r\n" not in buffer:
                    fill()
                size_line, buffer = buffer.split(b"\r\n", 1)
                size = int(size_line, 16)
                while len(buffer) < size + 2:
                    fill()
                body += buffer[:size]
                buffer = buffer[size + 2:]
                if size == 0:
                    break
        elif "content-length" in names:
            length = int(names["content-length"])
            while len(buffer) < length:
                fill()
            body = buffer[:length]
        if wait_close <= 0:
            return Response(status, headers, body, closed)
        # Does the server close after this response?
        sock.settimeout(wait_close)
        try:
            while True:
                fill()
        except (ConnectionError, ConnectionResetError):
            closed = True
        except (TimeoutError, socket.timeout):
            pass
        return Response(status, headers, body, closed)
    except (ConnectionError, ConnectionResetError):
        return Response(0, [], buffer, True)
    finally:
        sock.close()


def request(method, path, headers=(), body=None, wait_close=0.3):
    lines = [f"{method} {path} HTTP/1.1", f"Host: {HOST_HEADER}"]
    lines += [f"{name}: {value}" for name, value in headers]
    payload = b""
    if body is not None:
        payload = body if isinstance(body, bytes) else json.dumps(body).encode()
        lines.append(f"Content-Length: {len(payload)}")
    raw = ("\r\n".join(lines) + "\r\n\r\n").encode("latin-1") + payload
    return exchange(raw, wait_close)


def post_login(user, password, origin=ORIGIN):
    headers = [("Content-Type", "application/json")]
    if origin is not None:
        headers.append(("Origin", origin))
    return request("POST", "/api/v1/session", headers, {"user": user, "password": password})


def poll(attempt, header="1"):
    headers = [] if header is None else [("X-Crux-Request", header)]
    return request("GET", f"/api/v1/session/attempt/{attempt}", headers)


def wait_result(attempt, limit_s=60.0):
    started = time.monotonic()
    while True:
        reply = poll(attempt)
        if not (reply.status == 200 and reply.json() == {"state": "pending"}):
            return reply, time.monotonic() - started
        if time.monotonic() - started > limit_s:
            return reply, time.monotonic() - started
        time.sleep(0.05)


class Jar:
    def __init__(self, reply):
        self.session = self.xsrf = None
        for line in reply.values("set-cookie"):
            name, rest = line.split("=", 1)
            value = rest.split(";", 1)[0]
            if name == "crux_session":
                self.session = value
            elif name == "XSRF-TOKEN":
                self.xsrf = value

    def cookie(self):
        return f"crux_session={self.session}; XSRF-TOKEN={self.xsrf}"

    def headers(self, method, origin=ORIGIN, token=True):
        headers = [("Cookie", self.cookie())]
        if method != "GET":
            if origin is not None:
                headers.append(("Origin", origin))
            if token:
                headers.append(("X-XSRF-TOKEN", self.xsrf))
        return headers


def login(credentials):
    reply = post_login(*credentials)
    if reply.status != 202:
        return None, reply
    final, _ = wait_result(reply.json()["id"])
    return (Jar(final) if final.status == 200 and final.json() == {"state": "success"} else None), final


def error_is(reply, status, code):
    return reply.status == status and reply.json() == {"code": code}


def security_headers_ok(reply):
    names = dict(reply.headers)
    return (all(names.get(name) == value for name, value in SECURITY_HEADERS.items()) and
            "strict-transport-security" not in names and "access-control-allow-origin" not in names)


def case_two_request_login():
    reply = post_login(*ADMIN)
    check(reply.status == 202, f"login POST status {reply.status}")
    attempt = reply.json()["id"] if reply.status == 202 else ""
    check(bool(TOKEN.match(attempt)), "the attempt identifier is 43 base64url characters")
    check(reply.closed, "the server closes the connection after the login POST")
    check(not reply.values("set-cookie"), "the login POST sets no cookie")
    check(security_headers_ok(reply), "the login POST carries the security headers")
    final, waited = wait_result(attempt)
    check(final.status == 200 and final.json() == {"state": "success"}, f"admin login {final.body}")
    cookies = final.values("set-cookie")
    check(len(cookies) == 2, "two Set-Cookie lines")
    jar = Jar(final)
    check(bool(jar.session and TOKEN.match(jar.session)), "the session token's form")
    check(bool(jar.xsrf and TOKEN.match(jar.xsrf)), "the CSRF token's form, with no % and no quotes")
    check(jar.session != jar.xsrf and attempt not in (jar.session, jar.xsrf), "independent values")
    check(f"crux_session={jar.session}; Secure; HttpOnly; SameSite=Strict; Path=/" in cookies,
          "the session cookie's attributes")
    check(f"XSRF-TOKEN={jar.xsrf}; Secure; SameSite=Strict; Path=/" in cookies,
          "the CSRF cookie's attributes")
    check(security_headers_ok(final), "the poll carries the security headers")
    check(error_is(poll(attempt), 404, "not_found"), "a result is collected once")
    me = request("GET", "/api/v1/session/me", jar.headers("GET"))
    check(me.status == 200 and me.json() == {"user": "admin", "role": "administrator",
                                             "must_change_password": False}, f"me {me.body}")
    results["login_post_to_success_s"] = round(waited, 3)
    return jar


def case_poll_header():
    reply = post_login(*VIEWER)
    attempt = reply.json()["id"]
    time.sleep(0.5)
    for header in (None, "0", "11", "true"):
        refused = poll(attempt, header)
        check(error_is(refused, 403, "csrf_rejected") and not refused.values("set-cookie"),
              f"poll with X-Crux-Request {header!r}")
    final, _ = wait_result(attempt)
    check(final.status == 200 and final.json() == {"state": "success"} and
          len(final.values("set-cookie")) == 2, "the attempt survives refused polls")
    return Jar(final)


def case_missing_and_forged(jar):
    check(error_is(request("GET", "/api/v1/session/me"), 401, "unauthorized"), "no cookie")
    check(error_is(request("GET", "/api/v1/session/me", [("Cookie", "crux_session=" + "A" * 43)]),
                   401, "unauthorized"), "a forged token")
    check(error_is(request("GET", "/api/v1/session/me", [("Cookie", "crux_session=" + jar.session[:20])]),
                   401, "unauthorized"), "a prefix of the token")
    check(error_is(request("GET", "/api/v1/session/me", [("Cookie", "crux_session=" + jar.xsrf)]),
                   401, "unauthorized"), "the CSRF token as the session")


def case_wrong_role(admin, viewer):
    check(error_is(request("GET", "/api/v1/test/admin", viewer.headers("GET")), 403,
                   "permission_denied"), "a viewer on an access.manage route")
    check(request("GET", "/api/v1/test/admin", admin.headers("GET")).status == 200,
          "the administrator on it")


def raw_post(path, header_lines):
    lines = [f"POST {path} HTTP/1.1", f"Host: {HOST_HEADER}", "Content-Length: 0"]
    return exchange(("\r\n".join(lines) + "\r\n").encode("latin-1") + b"".join(
        line + b"\r\n" for line in header_lines) + b"\r\n")


def case_forgery(jar):
    path = "/api/v1/test/session"
    check(request("POST", path, jar.headers("POST")).status == 200, "token and Origin accepted")
    check(error_is(request("POST", path, jar.headers("POST", token=False)), 403, "csrf_rejected"),
          "missing CSRF token")
    check(error_is(request("POST", path, [("Cookie", jar.cookie()), ("Origin", ORIGIN),
                                          ("X-XSRF-TOKEN", jar.xsrf[:42])]), 403, "csrf_rejected"),
          "a CSRF token one character short")
    check(error_is(request("POST", path, jar.headers("POST", origin=None)), 403, "origin_rejected"),
          "missing Origin")
    check(error_is(request("POST", path, jar.headers("POST", origin="http://evil.test")), 403,
                   "origin_rejected"), "wrong Origin")
    check(error_is(request("POST", path, jar.headers("POST", origin=ORIGIN + ".evil.test")), 403,
                   "origin_rejected"), "an Origin that extends the right one")
    check(error_is(post_login(*ADMIN, origin="http://evil.test"), 403, "origin_rejected"),
          "a login with the wrong Origin")
    check(error_is(post_login(*ADMIN, origin=None), 403, "origin_rejected"), "a login with no Origin")
    cookie = f"Cookie: {jar.cookie()}".encode()
    origin = f"Origin: {ORIGIN}".encode()
    token = f"X-XSRF-TOKEN: {jar.xsrf}".encode()
    # The server captures no empty value, so an empty header of a captured name is invisible.
    reply = raw_post(path, [cookie, origin, token, b"X-XSRF-TOKEN:"])
    results["empty_duplicate_token_beside_a_good_one"] = reply.status
    check(reply.status == 200, "an empty duplicate beside the right token changes nothing")
    reply = raw_post(path, [cookie, origin, b"X-XSRF-TOKEN:"])
    check(error_is(reply, 403, "csrf_rejected"), "an empty token alone is no token")
    reply = raw_post(path, [cookie, b"Origin:", token])
    check(error_is(reply, 403, "origin_rejected"), "an empty Origin alone is no Origin")
    # A NUL ends the captured value: the prefix before it must not pass for the whole token.
    reply = raw_post(path, [cookie, origin, f"X-XSRF-TOKEN: {jar.xsrf[:20]}".encode() + b"\x00" +
                            jar.xsrf[20:].encode()])
    results["token_cut_by_nul"] = reply.status
    check(reply.status != 200, f"a token cut by a NUL is refused, got {reply.status}")


def case_rejections_look_alike():
    replies = []
    for user, password in (("admin", "a wrong password here"), ("nobody", "a wrong password here"),
                           ("Not A Name", "a wrong password here")):
        reply = post_login(user, password)
        check(reply.status == 202, f"a rejected login is queued like any other: {user}")
        final, _ = wait_result(reply.json()["id"])
        replies.append((final.status, final.headers, final.body))
    check(replies[0] == replies[1] == replies[2], "wrong password, unknown name and bad name look alike")
    check(replies[0][2] == b'{"state":"rejected"}', f"rejected body {replies[0][2]}")


def case_password_change(viewer):
    other, _ = login(VIEWER)
    check(other is not None, "a second viewer session")
    reply = request("POST", "/api/v1/session/me/password", viewer.headers("POST") +
                    [("Content-Type", "application/json")],
                    {"current_password": VIEWER[1], "new_password": CHANGED})
    check(reply.status == 202 and reply.closed, "a password change is queued and closes")
    final, waited = wait_result(reply.json()["id"])
    check(final.status == 200 and final.json() == {"state": "success"} and not final.values("set-cookie"),
          f"the password change {final.body}")
    check(request("GET", "/api/v1/session/me", viewer.headers("GET")).status == 200,
          "the changing session continues")
    if other is not None:
        check(error_is(request("GET", "/api/v1/session/me", other.headers("GET")), 401, "unauthorized"),
              "the account's other session ended")
    results["password_change_s"] = round(waited, 3)


def case_logout(jar):
    reply = request("DELETE", "/api/v1/session", jar.headers("DELETE"))
    check(reply.status == 200 and reply.body == b"", "logout")
    check(reply.values("set-cookie") == [
        "crux_session=; Secure; HttpOnly; SameSite=Strict; Path=/; Max-Age=0",
        "XSRF-TOKEN=; Secure; SameSite=Strict; Path=/; Max-Age=0"], "logout clears both cookies")
    check(error_is(request("GET", "/api/v1/session/me", jar.headers("GET")), 401, "unauthorized"),
          "the session ended")


def stats():
    return request("GET", "/probe/session-stats").json()


def case_failed_logins_beside_a_client(jar):
    """Clients sending failed logins while another client polls its own user."""
    def victim_round(count, latencies, statuses):
        for _ in range(count):
            started = time.monotonic()
            reply = request("GET", "/api/v1/session/me", jar.headers("GET"), wait_close=0)
            latencies.append(time.monotonic() - started)
            statuses.append(reply.status)
            time.sleep(0.05)

    baseline, baseline_status = [], []
    victim_round(20, baseline, baseline_status)
    stop = threading.Event()
    outcomes = []
    verified = []

    def attacker():
        while not stop.is_set():
            started = time.monotonic()
            reply = post_login("nobody", "a wrong password here")
            if reply.status == 202:
                final, _ = wait_result(reply.json()["id"], limit_s=20)
                outcomes.append(final.status)
                if final.status == 200:
                    # Rejected after a real verification: a whole attempt, seen from outside.
                    verified.append(time.monotonic() - started)
            else:
                outcomes.append(reply.status)

    threads = [threading.Thread(target=attacker) for _ in range(2)]
    for thread in threads:
        thread.start()
    loaded, loaded_status = [], []
    victim_round(60, loaded, loaded_status)
    stop.set()
    for thread in threads:
        thread.join(timeout=60)
    counters = stats()
    # The worker's own counter reads 0 here, because the simulated clock does not count computing, so
    # the bound takes the longest whole attempt seen from outside. A native_sim thread that computes is
    # never preempted, so one verification can stall the server thread for all of its length.
    attempt_s = max(verified) if verified else 0.0
    bound = max(baseline) + attempt_s + SLACK_S
    results["failed_logins"] = {
        "baseline_median_s": round(sorted(baseline)[len(baseline) // 2], 3),
        "baseline_max_s": round(max(baseline), 3),
        "loaded_median_s": round(sorted(loaded)[len(loaded) // 2], 3),
        "loaded_max_s": round(max(loaded), 3),
        "attacker_outcomes": {str(status): outcomes.count(status) for status in sorted(set(outcomes))},
        "attempt_max_s": round(attempt_s, 3),
        "worker_max_ms": counters["worker_max_ms"],
        "verified": counters["verified"],
        "busy": counters["busy"],
        "bound_s": round(bound, 3),
    }
    check(all(status == 200 for status in baseline_status + loaded_status),
          f"the other client was always answered: {set(loaded_status)}")
    check(bool(verified), "some failed logins ran a real verification")
    check(max(loaded) <= bound, f"the other client's longest wait {max(loaded):.3f} s exceeds {bound:.3f} s")


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("executable")
    parser.add_argument("--results", type=pathlib.Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory() as directory:
        disk = pathlib.Path(directory) / "disk.bin"
        process = subprocess.Popen([args.executable, f"--flash={disk}", "--seed=186"],
                                   stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                                   errors="replace")
        output = []
        ready = False
        deadline = time.monotonic() + 300
        while time.monotonic() < deadline:
            line = process.stdout.readline()
            if not line:
                break
            output.append(line)
            if "CRUX_SESSION_START_FAILED" in line:
                break
            if "CRUX_SESSION_READY" in line:
                ready = True
                break
        threading.Thread(target=lambda: [output.append(line) for line in process.stdout], daemon=True).start()
        try:
            if not check(ready, "the image starts:\n" + "".join(output[-20:])):
                return 1
            admin = case_two_request_login()
            viewer = case_poll_header()
            case_missing_and_forged(admin)
            case_wrong_role(admin, viewer)
            case_forgery(admin)
            case_password_change(viewer)
            case_logout(viewer)
            case_rejections_look_alike()
            case_failed_logins_beside_a_client(admin)
            results["stats"] = stats()
        finally:
            process.terminate()
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill()
    results["failures"] = failures
    text = json.dumps(results, indent=2, sort_keys=True)
    print(text)
    if args.results:
        args.results.write_text(text + "\n", encoding="utf-8")
    print("native_sim, plain HTTP on loopback, test-only entropy: not device evidence")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
