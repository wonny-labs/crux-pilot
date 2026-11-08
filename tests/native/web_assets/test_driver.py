#!/usr/bin/env python3
"""A client for the packaged web assets on the real Zephyr server, on native_sim over plain HTTP.

Every asset of the built folder is fetched and compared with the hash list that the packager wrote,
so what the server sends is what the SBOM input says it holds. Timings come from an emulated container
on a loaded machine with shortened settings (see prj.conf), and are not device evidence.

    test_driver.py <zephyr.exe> --site <folder> --hash-list <json> [--results file] [--only a,b]
"""
import argparse
import gzip
import hashlib
import json
import pathlib
import socket
import struct
import subprocess
import sys
import threading
import time

HOST = "127.0.0.1"
PORT = 18189
SEND_TIMEOUT_S = 0.5
RESPONSE_DEADLINE_S = 1.5
SLACK_S = 1.5
# One callback's worst stall: the head, each header's three sends, and a chunk's four, each able to wait
# out the send timeout, plus the response deadline that bounds the pieces.
STALL_BOUND_S = RESPONSE_DEADLINE_S + (1 + 3 * 6 + 1 + 4) * SEND_TIMEOUT_S + SLACK_S
STRICT_POLICY = ("default-src 'self'; script-src 'self'; style-src 'self'; base-uri 'self'; "
                 "form-action 'self'; frame-ancestors 'none'")
CORE_POLICY = "default-src 'self'; frame-ancestors 'none'"

results = {}


class Closed(Exception):
    """The server closed the connection before a whole response."""


def connect(receive_buffer=None):
    sock = socket.socket()
    if receive_buffer is not None:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, receive_buffer)
    sock.settimeout(8)
    sock.connect((HOST, PORT))
    return sock


def abort(sock):
    """Close with a reset, as a browser tab that is closed mid-download does."""
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("ii", 1, 0))
    sock.close()


def stalled_connection():
    """A client with a tiny receive buffer, whose server socket also has a tiny send buffer, so that a
    large asset leaves the server part-way through its send until the client reads."""
    sock = connect(receive_buffer=2048)
    sock.sendall(b"GET /probe/send-buffer/8192 HTTP/1.1\r\nHost: localhost\r\n\r\n")
    status, _, _ = read_response(sock)
    assert status == 200, status
    return sock


def read_some(sock, at_least, limit_s=1.0):
    """What the client receives for up to limit_s, or until at_least bytes. Returns the bytes, and
    when limit_s is over 1, whether the server closed the connection."""
    received, closed = b"", False
    end = time.monotonic() + limit_s
    sock.settimeout(0.2)
    while time.monotonic() < end and len(received) < at_least:
        try:
            data = sock.recv(65536 if at_least > 65536 else 512)
        except socket.timeout:
            continue
        except ConnectionResetError:
            closed = True
            break
        if not data:
            closed = True
            break
        received += data
    return (received, closed) if at_least > 65536 else received


def read_until(sock, marker, buffered):
    while marker not in buffered:
        data = sock.recv(65536)
        if not data:
            raise Closed(buffered)
        buffered += data
    return buffered


def read_exact(sock, buffered, length):
    while len(buffered) < length:
        data = sock.recv(65536)
        if not data:
            raise Closed(buffered)
        buffered += data
    return buffered[:length], buffered[length:]


def read_response(sock):
    """Status, headers (lower-case names) and the decoded body of one response."""
    buffered = read_until(sock, b"\r\n\r\n", b"")
    head, buffered = buffered.split(b"\r\n\r\n", 1)
    lines = head.decode("latin-1").split("\r\n")
    status = int(lines[0].split()[1])
    headers = {}
    for line in lines[1:]:
        name, value = line.split(":", 1)
        headers.setdefault(name.strip().lower(), value.strip())
    body = b""
    if headers.get("transfer-encoding") == "chunked":
        while True:
            buffered = read_until(sock, b"\r\n", buffered)
            size_line, buffered = buffered.split(b"\r\n", 1)
            size = int(size_line.split(b";")[0], 16)
            chunk, buffered = read_exact(sock, buffered, size + 2)
            body += chunk[:size]
            if size == 0:
                break
    elif "content-length" in headers:
        body, buffered = read_exact(sock, buffered, int(headers["content-length"]))
    return status, headers, body


def request(method, path, sock=None):
    own = sock is None
    sock = sock or connect()
    try:
        sock.sendall(f"{method} {path} HTTP/1.1\r\nHost: localhost\r\n\r\n".encode())
        return read_response(sock)
    finally:
        if own:
            sock.close()


def get(path, sock=None):
    return request("GET", path, sock)


def case(function):
    function.is_case = True
    return function


class Driver:
    def __init__(self, site, listing):
        self.site = pathlib.Path(site)
        self.listing = json.loads(pathlib.Path(listing).read_text(encoding="utf-8"))
        self.assets = {asset["path"]: asset for asset in self.listing["assets"]}

    def expect_asset(self, path, answer, asset):
        status, headers, body = answer
        assert status == 200, f"{path}: {status}"
        assert headers["content-type"] == asset["type"], (path, headers["content-type"])
        assert headers.get("content-encoding") == ("gzip" if asset["encoding"] == "gzip" else None), \
            (path, headers.get("content-encoding"))
        want = "no-cache" if asset["cache"] == "revalidate" else "max-age=31536000, immutable"
        assert headers["cache-control"] == want, (path, headers["cache-control"])
        assert headers["content-security-policy"] == STRICT_POLICY, headers["content-security-policy"]
        assert headers["x-content-type-options"] == "nosniff"
        assert headers["referrer-policy"] == "no-referrer"
        assert "set-cookie" not in headers
        assert hashlib.sha256(body).hexdigest() == asset["served_sha256"], f"{path}: not the listed bytes"
        original = (self.site / asset["file"]).read_bytes()
        decoded = gzip.decompress(body) if asset["encoding"] == "gzip" else body
        assert decoded == original, f"{path}: does not decode to the file"
        assert hashlib.sha256(original).hexdigest() == asset["sha256"]

    @case
    def every_asset_matches_the_hash_list(self):
        times = {}
        for path, asset in self.assets.items():
            started = time.monotonic()
            self.expect_asset(path, get(path), asset)
            times[path] = round(time.monotonic() - started, 3)
        assert len(self.assets) == self.listing["totals"]["files"]
        encodings = {path: asset["encoding"] for path, asset in self.assets.items()}
        assert encodings["/logo.png"] == "identity" and encodings["/index.html"] == "gzip"
        results["asset_seconds"] = times

    @case
    def root_is_index_and_a_query_is_ignored(self):
        index = self.assets["/index.html"]
        for path in ("/", "/?lang=en", "/index.html?v=2"):
            self.expect_asset(path, get(path), index)

    @case
    def one_connection_serves_every_asset_in_turn(self):
        with connect() as sock:
            for path, asset in self.assets.items():
                self.expect_asset(path, get(path, sock), asset)

    @case
    def unknown_paths_get_the_servers_plain_answer(self):
        for path in ("/settings", "/network", "/assets/missing.js", "/favicon.ico", "/index.htm"):
            status, headers, body = get(path)
            assert (status, body) == (404, b"Not Found"), (path, status, body)
            assert "content-security-policy" not in headers

    @case
    def a_deeper_path_under_an_asset_gets_the_json_not_found(self):
        for path in ("/index.html/x", "/assets/icon.svg/x", "//"):
            status, headers, body = get(path)
            assert status == 404 and json.loads(body) == {"code": "not_found"}, (path, status, body)
            assert headers["content-security-policy"] == CORE_POLICY

    @case
    def only_get_is_served(self):
        for method in ("POST", "PUT", "DELETE", "HEAD", "OPTIONS"):
            for path in ("/", "/index.html", "/assets/font-Zx98Wv76.woff2"):
                with connect() as sock:
                    sock.sendall(f"{method} {path} HTTP/1.1\r\nHost: localhost\r\nContent-Length: 0\r\n\r\n".encode())
                    status, _, body = read_response(sock)
                assert status == 405 and body == b"Method Not Allowed", (method, path, status)

    @case
    def three_clients_fetch_everything_at_once(self):
        failures, counts = [], []

        def worker():
            done = 0
            try:
                for _ in range(4):
                    for path, asset in self.assets.items():
                        answer = get(path)
                        if answer[0] == 409:
                            failures.append((path, "409 busy"))
                            continue
                        self.expect_asset(path, answer, asset)
                        done += 1
            except Exception as error:  # noqa: BLE001 - recorded, and fails the case below
                failures.append(repr(error))
            counts.append(done)

        threads = [threading.Thread(target=worker) for _ in range(3)]
        started = time.monotonic()
        for thread in threads:
            thread.start()
        for thread in threads:
            thread.join(60)
        assert not failures, failures
        assert counts == [4 * len(self.assets)] * 3, counts
        results["three_clients_seconds"] = round(time.monotonic() - started, 2)

    @case
    def a_client_that_disconnects_mid_asset_frees_the_resource(self):
        path = "/assets/font-Zx98Wv76.woff2"
        asset = self.assets[path]
        sock = stalled_connection()
        sock.sendall(f"GET {path} HTTP/1.1\r\nHost: localhost\r\n\r\n".encode())
        received = read_some(sock, 1500)
        assert 0 < len(received) < asset["served_bytes"], len(received)
        # The server is now blocked in a send of this response. Another client waits behind it.
        waits = {}

        def other():
            started = time.monotonic()
            waits["status"] = get("/style.css")[0]
            waits["seconds"] = time.monotonic() - started

        thread = threading.Thread(target=other)
        time.sleep(0.2)
        thread.start()
        time.sleep(0.4)
        assert thread.is_alive(), "the server was not part-way through the asset: the test's premise failed"
        reset_at = time.monotonic()
        abort(sock)
        thread.join(STALL_BOUND_S)
        assert not thread.is_alive() and waits["status"] == 200, waits
        after_reset = time.monotonic() - reset_at
        assert after_reset < STALL_BOUND_S, after_reset
        self.expect_asset(path, get(path), asset)
        self.expect_asset(path, get(path), asset)
        results["disconnect_mid_asset"] = {"received_before_reset": len(received),
                                           "other_client_wait_s": round(waits["seconds"], 3),
                                           "other_client_s_after_reset": round(after_reset, 3)}

    @case
    def a_reader_that_stops_cannot_hold_the_server_past_the_deadline(self):
        path = "/assets/font-Zx98Wv76.woff2"
        asset = self.assets[path]
        sock = stalled_connection()
        sock.sendall(f"GET {path} HTTP/1.1\r\nHost: localhost\r\n\r\n".encode())
        time.sleep(0.4)
        started = time.monotonic()
        status, _, _ = get("/index.html")
        wait = time.monotonic() - started
        assert status == 200, status
        assert 0.2 < wait < STALL_BOUND_S, wait
        received = read_some(sock, 10 ** 9, limit_s=4)
        closed = received[1]
        received = received[0]
        sock.close()
        complete = received.endswith(b"\r\n0\r\n\r\n")
        # The response is cut: by the response deadline, or by a send that could not finish.
        assert not complete, "a reader that stopped was served the whole asset"
        assert len(received) < asset["served_bytes"]
        self.expect_asset(path, get(path), asset)
        status, _, _ = get("/index.html")
        assert status == 200
        results["slow_reader"] = {"other_client_wait_s": round(wait, 3), "received": len(received),
                                  "closed_by_server": closed, "asset_served_bytes": asset["served_bytes"]}


def start(executable):
    process = subprocess.Popen([executable], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    deadline = time.monotonic() + 20
    for line in process.stdout:
        if "CRUX_WEB_READY" in line:
            threading.Thread(target=lambda: [None for _ in process.stdout], daemon=True).start()
            return process
        if "CRUX_WEB_START_FAILED" in line or time.monotonic() > deadline:
            break
    process.kill()
    raise SystemExit("the image did not start")


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("executable")
    parser.add_argument("--site", required=True)
    parser.add_argument("--hash-list", required=True)
    parser.add_argument("--results")
    parser.add_argument("--only")
    args = parser.parse_args()
    driver = Driver(args.site, args.hash_list)
    names = [name for name in dir(Driver) if getattr(getattr(Driver, name), "is_case", False)]
    if args.only:
        names = [name for name in names if name in args.only.split(",")]
    process = start(args.executable)
    failed = []
    try:
        for name in names:
            try:
                getattr(driver, name)()
                print(f"PASS {name}")
            except Exception as error:  # noqa: BLE001 - every case runs, and any failure fails the run
                failed.append(name)
                print(f"FAIL {name}: {error!r}")
    finally:
        process.kill()
    results["cases"] = {"run": len(names), "failed": failed}
    if args.results:
        pathlib.Path(args.results).write_text(json.dumps(results, indent=2, sort_keys=True), encoding="utf-8")
    print(json.dumps(results, indent=2, sort_keys=True))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
