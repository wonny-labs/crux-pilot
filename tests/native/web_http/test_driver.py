#!/usr/bin/env python3
"""A client for the adapter on the real Zephyr server, on native_sim over plain HTTP on loopback.

Each case runs against the real server's resource matching, holders and timers, and on the host's TCP
stack through the native socket offload. So the behaviour is the server's, while the timings are not
device evidence: they come from an emulated container on a loaded machine, with shortened settings
(see prj.conf). Measurements are printed as one JSON object, and bounds here allow for that.

    test_driver.py <zephyr.exe> [--results file] [--only name,name]
"""
import argparse
import json
import pathlib
import socket
import struct
import subprocess
import sys
import threading
import time

HOST = "127.0.0.1"
PORT = 18184
INACTIVITY_S = 3.0
DEADLINE_S = 2.0
SEND_TIMEOUT_S = 0.5
RESPONSE_DEADLINE_S = 1.5
# Allowance for scheduling in an emulated container.
SLACK_S = 1.5

ADMIN = "crux_session=admin"
VIEWER = "crux_session=viewer"
PLAIN_404 = b"HTTP/1.1 404 Not Found\r\nContent-Length: 9\r\n\r\nNot Found"
PLAIN_405 = b"HTTP/1.1 405 Method Not Allowed\r\nContent-Length: 18\r\n\r\nMethod Not Allowed"
PLAIN_409 = b"HTTP/1.1 409 Conflict\r\n\r\n"
SECURITY_HEADERS = {
    "content-security-policy": "default-src 'self'; frame-ancestors 'none'",
    "x-content-type-options": "nosniff",
    "referrer-policy": "no-referrer",
    "cache-control": "no-store",
}
# 256 bits in base64url, the size Issue #186 specifies for each.
TOKEN = "A" * 43
BROWSER_COOKIE = f"crux_session=admin{'0' * 38}; XSRF-TOKEN={TOKEN}"

results = {}


class Closed(Exception):
    """The server closed the connection before a whole response."""


class Client:
    def __init__(self, rcvbuf=None, timeout=20.0):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        if rcvbuf is not None:
            self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, rcvbuf)
        self.sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.sock.settimeout(timeout)
        self.sock.connect((HOST, PORT))
        self.buffer = b""

    def close(self):
        self.sock.close()

    def reset(self):
        """Close with a TCP reset rather than a FIN."""
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("ii", 1, 0))
        self.sock.close()

    def send(self, data):
        self.sock.sendall(data)

    def fill(self):
        data = self.sock.recv(65536)
        if not data:
            raise Closed()
        self.buffer += data

    def read_raw_until_close(self):
        """Everything until the peer closes, however long that is within the timeout."""
        try:
            while True:
                self.fill()
        except (Closed, ConnectionResetError):
            pass
        data, self.buffer = self.buffer, b""
        return data

    def is_closed_within(self, seconds):
        self.sock.settimeout(seconds)
        try:
            while True:
                self.fill()
        except (Closed, ConnectionResetError):
            return True
        except TimeoutError:
            return False
        finally:
            self.sock.settimeout(20.0)

    def read_response(self):
        while b"\r\n\r\n" not in self.buffer:
            self.fill()
        head, self.buffer = self.buffer.split(b"\r\n\r\n", 1)
        lines = head.decode("latin-1").split("\r\n")
        status = int(lines[0].split()[1])
        headers = {}
        multi = {}
        for line in lines[1:]:
            name, value = line.split(":", 1)
            headers[name.lower()] = value.strip()
            multi.setdefault(name.lower(), []).append(value.strip())
        if headers.get("transfer-encoding") == "chunked":
            body = self.read_chunked()
        elif "content-length" in headers:
            body = self.read_exact(int(headers["content-length"]))
        else:
            body = b""
        return Response(status, headers, multi, body)

    def read_exact(self, length):
        while len(self.buffer) < length:
            self.fill()
        data, self.buffer = self.buffer[:length], self.buffer[length:]
        return data

    def read_chunked(self):
        body = b""
        while True:
            while b"\r\n" not in self.buffer:
                self.fill()
            size_line, self.buffer = self.buffer.split(b"\r\n", 1)
            size = int(size_line, 16)
            data = self.read_exact(size + 2)
            if size == 0:
                return body
            body += data[:-2]

    def request(self, method, path, headers=None, body=b""):
        self.send(encode(method, path, headers, body))
        return self.read_response()


class Response:
    def __init__(self, status, headers, multi, body):
        self.status = status
        self.headers = headers
        self.multi = multi
        self.body = body

    def json(self):
        return json.loads(self.body)


def encode(method, path, headers=None, body=b"", version="1.1"):
    lines = [f"{method} {path} HTTP/{version}", f"Host: {HOST}:{PORT}"]
    for name, value in (headers or {}).items():
        lines.append(f"{name}: {value}")
    if body and not any(name.lower() == "content-length" for name in headers or {}):
        lines.append(f"Content-Length: {len(body)}")
    return ("\r\n".join(lines) + "\r\n\r\n").encode("latin-1") + body


def one(method, path, headers=None, body=b""):
    client = Client()
    try:
        return client.request(method, path, headers, body)
    finally:
        client.close()


def raw_exchange(data, timeout=INACTIVITY_S + SLACK_S + 2.0):
    client = Client(timeout=timeout)
    try:
        client.send(data)
        return client.read_raw_until_close()
    finally:
        client.close()


def stats():
    return one("GET", "/probe/stats").json()


def wait_for_settled():
    """Let a closing client's slot return. Every held slot is gone within the inactivity timer."""
    time.sleep(0.2)


def check(condition, message):
    if not condition:
        raise AssertionError(message)


def expect_security_headers(response):
    for name, value in SECURITY_HEADERS.items():
        check(response.headers.get(name) == value, f"{name}: {response.headers.get(name)!r}")
    check("strict-transport-security" not in response.headers, "HSTS must not be sent")
    check("access-control-allow-origin" not in response.headers, "CORS must not be sent")


def expect_error(response, status, code):
    check(response.status == status, f"status {response.status}, wanted {status}")
    check(response.json() == {"code": code}, f"body {response.body!r}")
    expect_security_headers(response)


# Routing.

def case_routes_and_merged_methods():
    check(one("POST", "/api/v1/session", body=b"{}").json()["route"] == "login", "POST reaches login")
    response = one("DELETE", "/api/v1/session", {"Cookie": ADMIN, "X-XSRF-TOKEN": "t"})
    check(response.status == 200 and response.json()["route"] == "logout", "second method merged")
    response = one("GET", "/api/v1/status", {"Cookie": VIEWER})
    check(response.json()["route"] == "status", "fixed route")
    check(response.headers["transfer-encoding"] == "chunked", "the server always sends chunked")
    expect_security_headers(response)


def case_wildcard_not_captured_by_shorter_fixed_route():
    response = one("GET", "/api/v1/session/attempt/abc-1")
    check(response.status == 200, f"status {response.status}: /api/v1/session must not capture it")
    check(response.json() == {"route": "attempt", "id": "abc-1", "query": ""}, f"{response.body!r}")
    check(one("GET", "/api/v1/session/attempt/abc?x=/y").json()["query"] == "x=/y", "query")


def case_deeper_wildcard_first():
    response = one("GET", "/api/v1/files/logs/a.txt", {"Cookie": VIEWER})
    check(response.json()["route"] == "file_item", f"{response.body!r}")
    response = one("GET", "/api/v1/files/logs", {"Cookie": VIEWER})
    check(response.json()["route"] == "file_list", f"{response.body!r}")


def case_deeper_path_is_json_404():
    for path in ("/api/v1/status/x", "/api/v1/status/x/y", "/api/v1/session/attempt/a/b",
                 "/api/v1/session/me/x", "/api/v1/files/a/b/c", "/api/v1/status/"):
        expect_error(one("GET", path, {"Cookie": ADMIN}), 404, "not_found")
    # Before any access check: a deeper path with no session is also 404, not 401.
    expect_error(one("GET", "/api/v1/status/x"), 404, "not_found")


def case_path_forms():
    # The server removes dot segments first, so these reach the real route and its access class.
    check(one("GET", "/api/v1/status/../status").status == 401, "normalised, and access enforced")
    check(one("GET", "/api/v1/session/attempt/../../status").status == 401, "public route escaped")
    response = one("GET", "/api/v1/session/attempt/../../status", {"Cookie": ADMIN})
    check(response.json()["route"] == "status", f"{response.body!r}")
    # Nothing is decoded.
    for path in ("/api/v1/session/attempt/%2e%2e", "/api/v1/session/attempt/a%2Fb",
                 "/api/v1/status%2Fx", "/api/v1/session/attempt/a;b"):
        response = one("GET", path)
        check(response.status in (404, 405), f"{path}: {response.status}")
        check(response.status != 200, path)


def case_project_route():
    check(one("GET", "/api/v1/app/pump").status == 401, "project permission route")
    check(one("GET", "/api/v1/app/pump", {"Cookie": VIEWER}).json() == {"speed": 42}, "project route")
    check(one("GET", "/api/v1/app/pump/7", {"Cookie": VIEWER}).status == 200, "project wildcard route")


# The server's own responses, declared in the design.

def case_server_plain_responses():
    check(raw_exchange(encode("GET", "/nothing", {"Connection": "close"})) == PLAIN_404, "plain 404")
    check(raw_exchange(encode("PUT", "/api/v1/status", {"Connection": "close"})) == PLAIN_405,
          "plain 405")
    check(raw_exchange(encode("HEAD", "/api/v1/status", {"Connection": "close"})) == PLAIN_405,
          "HEAD has no route, and must not reach the server's bare 200")
    check(raw_exchange(encode("OPTIONS", "/api/v1/status", {"Connection": "close"})) == PLAIN_405,
          "OPTIONS")
    # A deeper path under a route without this method, which the server answers from the parent's mask.
    check(raw_exchange(encode("GET", "/api/v1/session/x", {"Connection": "close"})) == PLAIN_405,
          "a deeper path where the parent has no such method")
    holder = Client()
    holder.send(encode("POST", "/api/v1/session", {"Content-Length": "10"}, b"ab"))
    time.sleep(0.2)
    check(raw_exchange(encode("POST", "/api/v1/session", {"Content-Length": "2"}, b"ok")) == PLAIN_409,
          "plain 409, and closed")
    holder.close()
    wait_for_settled()
    results["server_plain"] = {
        "404": PLAIN_404.decode(), "405": PLAIN_405.decode(), "409": PLAIN_409.decode()}


# Enforcement.

def case_enforcement():
    expect_error(one("GET", "/api/v1/status"), 401, "unauthorized")
    expect_error(one("GET", "/api/v1/status", {"Cookie": "crux_session=forged"}), 401, "unauthorized")
    check(one("GET", "/api/v1/network", {"Cookie": VIEWER}).status == 200, "permitted")
    expect_error(one("PUT", "/api/v1/network", {"Cookie": VIEWER, "X-XSRF-TOKEN": "t"}, b"{}"), 403,
                 "permission_denied")
    check(one("PUT", "/api/v1/network", {"Cookie": ADMIN, "X-XSRF-TOKEN": "t"}, b"{}").status == 200,
          "permitted write")
    expect_error(one("PUT", "/api/v1/network", {"Cookie": ADMIN}, b"{}"), 403, "csrf_rejected")


# Header capture.

def echo_lengths(headers, body=b"{}"):
    return one("POST", "/probe/echo", headers, body).json()


def browser_headers():
    return {
        "Cookie": BROWSER_COOKIE,
        "Origin": "https://192.168.100.200:8443",
        "X-XSRF-TOKEN": TOKEN,
        "X-Crux-Request": "1",
        "Content-Type": "application/json",
        "User-Agent": "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) "
                      "Chrome/131.0.0.0 Safari/537.36",
        "Accept": "application/json, text/plain, */*",
        "Accept-Language": "ko-KR,ko;q=0.9,en-US;q=0.8,en;q=0.7",
        "Accept-Encoding": "gzip, deflate, br, zstd",
        "Sec-Fetch-Site": "same-origin",
        "Sec-Fetch-Mode": "cors",
        "Sec-Fetch-Dest": "empty",
        "Referer": "https://192.168.100.200:8443/",
    }


def case_header_capture_browser_shape():
    lengths = echo_lengths(browser_headers())
    check(lengths == {"cookie": len(BROWSER_COOKIE), "origin": 28, "host": len(f"{HOST}:{PORT}"),
                      "xsrf": 43, "request": 1, "type": 16, "length": 1, "body": 2}, f"{lengths}")
    results["capture_browser_shape"] = lengths


def case_header_capture_trickled():
    client = Client(timeout=60.0)
    data = encode("POST", "/probe/echo", browser_headers(), b"{}")
    for offset in range(0, len(data)):
        client.send(data[offset:offset + 1])
    lengths = client.read_response().json()
    client.close()
    check(lengths["cookie"] == len(BROWSER_COOKIE) and lengths["request"] == 1, f"{lengths}")
    results["capture_one_byte_per_send_bytes"] = len(data)


def case_header_capture_limits():
    # A parsed value holds 511 bytes, then the server drops it, and a dropped header is rejected.
    longest = "a=" + "b" * 509
    check(echo_lengths({"Cookie": longest})["cookie"] == 511, "the longest Cookie")
    expect_error(one("GET", "/probe/echo", {"Cookie": longest + "c"}), 400, "bad_request")
    expect_error(one("GET", "/api/v1/status", {"Cookie": ADMIN + "; pad=" + "c" * 600}), 400,
                 "bad_request")
    # The capture buffer overflows too, however the bytes are divided, rather than losing a header.
    client_headers = {"Cookie": longest, "Origin": "o" * 400, "X-XSRF-TOKEN": "x" * 400}
    expect_error(one("GET", "/probe/echo", client_headers), 400, "bad_request")
    # An uncaptured header of any size does not matter.
    check(echo_lengths({"Cookie": ADMIN, "X-Other": "v" * 2000})["cookie"] == len(ADMIN), "other")
    # Repeated: which one would a check read?
    expect_error(one("GET", "/probe/echo", {"Cookie": ADMIN, "cookie": VIEWER}), 400, "bad_request")
    results["capture_cookie_max_bytes"] = 511


# Bodies and form checks.

def case_body_form_checks():
    response = one("POST", "/api/v1/session", {"Content-Length": "513"}, b"x" * 513)
    expect_error(response, 413, "payload_too_large")
    check(response.headers.get("connection") == "close", "closed after an early answer")
    expect_error(one("POST", "/api/v1/session", {"Transfer-Encoding": "chunked"}), 400, "bad_request")
    expect_error(one("GET", "/api/v1/status", {"Cookie": ADMIN, "Content-Length": "4"}, b"abcd"), 400,
                 "bad_request")
    expect_error(one("POST", "/api/v1/session/deeper", {"Content-Length": "4"}), 404, "not_found")
    check(one("POST", "/api/v1/session", {"Content-Length": "512"}, b"x" * 512).json()["body"] == 512,
          "a body spanning reads")


def case_early_answer_and_keep_alive():
    client = Client()
    for _ in range(3):
        check(client.request("GET", "/api/v1/status", {"Cookie": ADMIN}).status == 200, "kept")
        check(client.request("POST", "/api/v1/session", body=b"{}").status == 200, "kept")
        check(client.request("GET", "/api/v1/status/x").status == 404, "kept after a JSON 404")
    client.close()


def case_protocol_edges():
    before = stats()
    # A protocol upgrade: the server sends its own 426 and would go on to run the resource.
    data = raw_exchange(encode("GET", "/api/v1/status", {"Cookie": ADMIN, "Connection": "Upgrade",
                                                         "Upgrade": "websocket"}))
    check(data.startswith(b"HTTP/1.1 426"), f"{data[:40]!r}")
    # HTTP/2 is compiled out, so its preface closes.
    data = raw_exchange(b"PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n")
    check(b"status" not in data, f"{data[:40]!r}")
    # Pipelined in one buffer: the server joins the URLs, so nothing routes, and neither handler runs.
    first = encode("GET", "/api/v1/status", {"Cookie": ADMIN})
    data = raw_exchange(first + first + encode("GET", "/nothing", {"Connection": "close"}))
    check(b'"route"' not in data, f"{data[:120]!r}")
    # A URL over the server's limit.
    data = raw_exchange(encode("GET", "/" + "a" * 300))
    check(data.startswith(b"HTTP/1.1 500"), f"{data[:40]!r}")
    # A malformed request line.
    data = raw_exchange(b"GET /api/v1/status HTP/1.1\r\nHost: padding-for-the-preface\r\n\r\n")
    check(data.startswith(b"HTTP/1.1 500") or data == b"", f"{data[:40]!r}")
    after = stats()
    check(after["handlers"] == before["handlers"] + 1, f"only stats ran: {before} {after}")
    # HTTP/1.0: no chunks, and the server closes.
    data = raw_exchange(encode("GET", "/api/v1/status", {"Cookie": ADMIN}, version="1.0"))
    check(b'{"route":"status","body":0}' in data, f"{data!r}")
    # The server waits for 24 bytes before it looks at a request, which a browser always sends.
    started = time.monotonic()
    data = raw_exchange(b"GET /x HTTP/1.1\r\n\r\n")
    results["short_request_closed_after_s"] = round(time.monotonic() - started, 2)
    check(data == b"", f"{data!r}")


# Release of a route.

def post_partial(client):
    client.send(encode("POST", "/api/v1/session", {"Content-Length": "100"}, b"ab"))


def wait_route_free(limit):
    """Poll the login route with a complete request until it stops answering 409."""
    started = time.monotonic()
    while time.monotonic() - started < limit:
        try:
            response = one("POST", "/api/v1/session", body=b"{}")
            if response.status == 200:
                return time.monotonic() - started
        except (Closed, ConnectionResetError):
            pass
        time.sleep(0.05)
    raise AssertionError("the route stayed held")


def case_disconnect_releases_route():
    before = stats()
    for leave in (Client.close, Client.reset):
        holder = Client()
        post_partial(holder)
        time.sleep(0.2)
        check(raw_exchange(encode("POST", "/api/v1/session", body=b"{}")) == PLAIN_409, "held")
        leave(holder)
        released = wait_route_free(1.0 + SLACK_S)
        results[f"released_after_{leave.__name__}_s"] = round(released, 3)
    after = stats()
    check(after["aborted"] == before["aborted"] + 2, f"{before} {after}")


def case_partial_body_closed_by_inactivity():
    holder = Client()
    started = time.monotonic()
    post_partial(holder)
    check(holder.is_closed_within(INACTIVITY_S + SLACK_S), "the silent client must be closed")
    held = time.monotonic() - started
    check(INACTIVITY_S - 0.5 <= held <= INACTIVITY_S + SLACK_S, f"held {held}")
    holder.close()
    wait_route_free(1.0)
    results["partial_body_silent_held_s"] = round(held, 2)


def case_trickled_body_closed_by_deadline():
    """The inactivity timer restarts on every byte. Only the whole-request deadline stops this."""
    before = stats()
    holder = Client()
    started = time.monotonic()
    holder.send(encode("POST", "/api/v1/session", {"Content-Length": "500"}))
    closed_at = None
    data = b""
    for _ in range(int((DEADLINE_S + SLACK_S + INACTIVITY_S) / 0.2)):
        try:
            holder.send(b"x")
        except (BrokenPipeError, ConnectionResetError):
            closed_at = time.monotonic() - started
            break
        holder.sock.settimeout(0.2)
        try:
            chunk = holder.sock.recv(4096)
            if not chunk:
                closed_at = time.monotonic() - started
                break
            data += chunk
        except TimeoutError:
            pass
    holder.close()
    check(closed_at is not None, "the deadline must close a trickling client")
    check(DEADLINE_S - 0.3 <= closed_at <= DEADLINE_S + SLACK_S, f"closed at {closed_at}")
    check(data == b"", f"the close sends nothing, and no plain 500: {data[:60]!r}")
    wait_route_free(1.0)
    after = stats()
    check(after["deadlines"] == before["deadlines"] + 1, f"{before} {after}")
    results["trickled_body_closed_s"] = round(closed_at, 2)


# Slots.

def case_trickled_headers_hold_slots():
    """Headers run no callback, so the adapter cannot see these. The result is a declared limit."""
    window = 4 * INACTIVITY_S
    interval = INACTIVITY_S / 3.0
    prefix = b"GET /api/v1/status HTTP/1.1\r\nHost: padding-so-the-preface-passes\r\nX-Slow: "
    holders = [Client() for _ in range(3)]
    for holder in holders:
        holder.send(prefix)
    before = time.monotonic()
    waiting = {}

    def blocked_client():
        client = Client(timeout=window * 3)
        started = time.monotonic()
        client.send(encode("GET", "/probe/stats"))
        client.read_response()
        waiting["served_after_s"] = time.monotonic() - started
        client.close()

    time.sleep(0.3)
    thread = threading.Thread(target=blocked_client)
    thread.start()
    alive = 0
    while time.monotonic() - before < window:
        time.sleep(interval)
        alive = 0
        for holder in holders:
            try:
                holder.send(b"a")
                alive += 1
            except OSError:
                pass
    check(alive == 3, f"{alive} of 3 trickling clients survived past the inactivity timer")
    check(thread.is_alive(), "a fourth client must still be waiting: the interface is blocked")
    held = time.monotonic() - before
    for holder in holders:
        holder.close()
    thread.join()
    results["trickled_headers"] = {
        "clients": 3,
        "held_s": round(held, 2),
        "inactivity_s": INACTIVITY_S,
        "bytes_each": int(window / interval),
        "fourth_client_served_after_s": round(waiting["served_after_s"], 2),
        "parser_header_limit_bytes": 80 * 1024,
        "worst_case_hold_at_10s_timer_days": round(80 * 1024 * 10 / 86400, 1),
    }


def case_client_slots():
    idle = [Client() for _ in range(3)]
    for client in idle:
        check(client.request("GET", "/api/v1/status", {"Cookie": ADMIN}).status == 200, "served")
    started = time.monotonic()
    fourth = Client()
    fourth.send(encode("GET", "/api/v1/status", {"Cookie": ADMIN}))
    check(fourth.read_response().status == 200, "the fourth is served once a slot ends")
    waited = time.monotonic() - started
    fourth.close()
    check(waited >= INACTIVITY_S - 1.0, f"three idle keep-alive connections took all slots: {waited}")
    for client in idle:
        client.close()
    wait_for_settled()
    results["client_slots"] = {"max_clients": 3, "fourth_waited_s": round(waited, 2),
                               "note": "idle keep-alive connections hold a slot until the timer"}


# Stalls of the single server thread.

def ping_latencies(stop, out):
    client = Client(timeout=30.0)
    try:
        while not stop.is_set():
            started = time.monotonic()
            client.send(encode("GET", "/api/v1/status", {"Cookie": ADMIN}))
            client.read_response()
            out.append(time.monotonic() - started)
            time.sleep(0.01)
    except (OSError, Closed):
        pass
    finally:
        client.close()


class Pinger:
    def __init__(self):
        self.stop = threading.Event()
        self.latencies = []
        self.thread = threading.Thread(target=ping_latencies, args=(self.stop, self.latencies))

    def __enter__(self):
        self.thread.start()
        time.sleep(0.2)
        return self

    def __exit__(self, *_):
        self.stop.set()
        self.thread.join()

    def summary(self):
        ordered = sorted(self.latencies)
        return {
            "samples": len(ordered),
            "median_ms": round(ordered[len(ordered) // 2] * 1000, 1),
            "max_ms": round(ordered[-1] * 1000, 1),
        }


def case_baseline_latency():
    with Pinger() as pinger:
        time.sleep(1.5)
    results["baseline_latency"] = pinger.summary()


def case_slow_handler_stalls_others():
    before = stats()
    with Pinger() as pinger:
        check(one("GET", "/probe/slow/500").status == 200, "slow handler")
        time.sleep(0.1)
    summary = pinger.summary()
    check(summary["max_ms"] >= 450, f"a handler stalls every client: {summary}")
    after = stats()
    check(after["overruns"] == before["overruns"] + 1, f"{before} {after}")
    results["handler_500ms_stall"] = {**summary, "handler_max_ms": after["handler_max_ms"]}


def case_large_static_body_in_pieces():
    client = Client()
    response = client.request("GET", "/probe/static/20000")
    check(len(response.body) == 20000, f"{len(response.body)}")
    check(response.body[:4] == b"abcd" and response.body[26:28] == b"ab", "content")
    check(client.request("GET", "/probe/static/0").status == 200, "empty, and the connection is kept")
    client.close()


def case_slow_reader_no_progress():
    """A reader that reads nothing: each send waits for its timeout, then the server gives up."""
    reader = Client(rcvbuf=4096)
    with Pinger() as pinger:
        reader.send(encode("GET", "/probe/static/8000000"))
        time.sleep(SEND_TIMEOUT_S * 3 + 0.5)
    summary = pinger.summary()
    reader.reset()
    wait_for_settled()
    check(summary["max_ms"] <= (RESPONSE_DEADLINE_S + SEND_TIMEOUT_S * 2 + SLACK_S) * 1000, f"{summary}")
    results["slow_reader_no_progress"] = {**summary, "send_timeout_ms": int(SEND_TIMEOUT_S * 1000)}


def case_slow_reader_held_slot_closes():
    """After the send times out the slot is held until the inactivity timer, and no longer."""
    reader = Client(rcvbuf=4096)
    reader.send(encode("GET", "/probe/static/8000000"))
    started = time.monotonic()
    time.sleep(SEND_TIMEOUT_S + 0.2)
    closed = reader.is_closed_within(INACTIVITY_S + RESPONSE_DEADLINE_S + SLACK_S + 4.0)
    # The receive above drains what is queued, so the send may progress before it stops.
    reader.reset()
    check(closed, "a stalled reader must lose its slot")
    results["slow_reader_slot_released_s"] = round(time.monotonic() - started, 2)
    wait_for_settled()


def case_slow_reader_with_progress():
    """A reader that drains a little before each send times out. The response deadline bounds it."""
    before = stats()
    reader = Client(rcvbuf=4096, timeout=0.05)
    reader.send(encode("GET", "/probe/static/8000000"))
    started = time.monotonic()
    total = 0
    with Pinger() as pinger:
        while time.monotonic() - started < RESPONSE_DEADLINE_S + SEND_TIMEOUT_S * 2 + SLACK_S:
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
    wait_for_settled()
    after = stats()
    # The client sees the close only after draining what the host still holds for it.
    check(after["response_deadlines"] == before["response_deadlines"] + 1, f"{before} {after}")
    check(total < 8000000, "the response must be cut")
    check(summary["max_ms"] <= (RESPONSE_DEADLINE_S + SEND_TIMEOUT_S * 2 + SLACK_S) * 1000, f"{summary}")
    results["slow_reader_with_progress"] = {
        **summary, "bytes_read": total, "response_deadline_ms": int(RESPONSE_DEADLINE_S * 1000)}


def case_upload_flow_control():
    """The flow control probe: back-pressure from a bounded worker costs every client a stall."""
    size = 32 * 1024
    with Pinger() as pinger:
        started = time.monotonic()
        response = one("POST", "/probe/upload", {"Content-Length": str(size)}, b"u" * size)
        elapsed = time.monotonic() - started
    check(response.status == 200, f"{response.status}")
    probe = response.json()
    check(probe["received"] == size and not probe["overflow"], f"{probe}")
    results["upload_probe"] = {
        "bytes": size, "elapsed_s": round(elapsed, 2), **probe, "other_client": pinger.summary(),
        "queue_bytes": 8 * 256, "chunk_ms": 5, "stall_every_chunks": 16, "stall_ms": 200}


CASES = [value for name, value in list(globals().items()) if name.startswith("case_")]


def start_server(executable, log):
    process = subprocess.Popen([str(executable)], stdout=log, stderr=subprocess.STDOUT)
    deadline = time.monotonic() + 60.0
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise SystemExit(f"the image exited with {process.returncode}")
        try:
            socket.create_connection((HOST, PORT), timeout=1.0).close()
            time.sleep(0.2)
            return process
        except OSError:
            time.sleep(0.2)
    process.kill()
    raise SystemExit("the image did not start listening")


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("executable", type=pathlib.Path)
    parser.add_argument("--results", type=pathlib.Path)
    parser.add_argument("--only", default="")
    args = parser.parse_args()
    selected = [case for case in CASES
                if not args.only or case.__name__[5:] in args.only.split(",")]
    failures = 0
    log_path = args.results.with_suffix(".log") if args.results else pathlib.Path("/tmp/web_http.log")
    with open(log_path, "w", encoding="utf-8") as log:
        process = start_server(args.executable, log)
        try:
            for case in selected:
                started = time.monotonic()
                try:
                    case()
                    print(f"PASS {case.__name__[5:]} ({time.monotonic() - started:.1f}s)", flush=True)
                except Exception as error:  # A failed case should not hide the others.
                    failures += 1
                    print(f"FAIL {case.__name__[5:]}: {type(error).__name__}: {error}", flush=True)
                # Slots held by an earlier case end within the inactivity timer.
                time.sleep(INACTIVITY_S + 0.5 if failures else 0.1)
            if process.poll() is not None:
                failures += 1
                print(f"FAIL the image exited with {process.returncode}", flush=True)
        finally:
            process.kill()
            process.wait()
    print(json.dumps(results, indent=2, sort_keys=True))
    if args.results:
        args.results.write_text(json.dumps(results, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"{len(selected) - failures} of {len(selected)} cases passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
