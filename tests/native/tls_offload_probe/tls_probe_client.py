#!/usr/bin/env python3
"""Drive the native_sim TLS offload probe from the host, and record what each step did.

Each step is a real client on the host's own TCP stack. The negative controls show that it is TLS at
all: a client that does not trust the certificate must fail, and plain text must get nothing back.

The result is a statement about this socket on native_sim. Connection limits, timing and memory come
from the host's stack and the simulator, so none of them is device evidence.
"""
import argparse
import json
import re
import socket
import ssl
import subprocess
import sys
import threading
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
CERTIFICATE = HERE / "credentials" / "test-server.crt"
HOST = "127.0.0.1"
# A certificate that did not sign the server's, which the client trusts instead, for the negative control.
OTHER_CA = HERE / "credentials" / "other-ca.crt"


class Probe:
    """Runs the image and collects its PROBE lines."""

    def __init__(self, executable, log_path):
        self.lines = []
        self.lock = threading.Lock()
        self.log = open(log_path, "w", encoding="utf-8")
        self.process = subprocess.Popen([executable], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                        text=True, errors="replace")
        self.reader = threading.Thread(target=self._read, daemon=True)
        self.reader.start()

    def _read(self):
        for line in self.process.stdout:
            self.log.write(line)
            self.log.flush()
            with self.lock:
                self.lines.append(line.rstrip())

    def wait_for(self, pattern, timeout):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            with self.lock:
                for line in self.lines:
                    if re.search(pattern, line):
                        return line
            if self.process.poll() is not None:
                return None
            time.sleep(0.05)
        return None

    def probe_lines(self):
        with self.lock:
            return [line for line in self.lines if line.startswith("PROBE ")]

    def stop(self):
        self.process.terminate()
        try:
            self.process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait()
        self.reader.join(timeout=5)
        self.log.close()


def client_context(version, trust=None):
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    context.load_verify_locations(cafile=str(CERTIFICATE if trust is None else trust))
    context.minimum_version = version
    context.maximum_version = version
    return context


def connect(port, version, timeout=30, trust=None):
    raw = socket.create_connection((HOST, port), timeout=timeout)
    try:
        return client_context(version, trust).wrap_socket(raw, server_hostname="localhost",
                                                          suppress_ragged_eofs=False)
    except BaseException:
        raw.close()
        raise


def exchange(tls, length):
    """Send a pattern of this length and read it back, keeping both directions moving."""
    data = bytes((i * 31 + 7) & 0xff for i in range(length))
    received = bytearray()
    sent = 0
    while len(received) < length:
        if sent < length:
            chunk = data[sent:sent + 1024]
            tls.sendall(chunk)
            sent += len(chunk)
        while len(received) < sent:
            piece = tls.recv(4096)
            if not piece:
                raise ConnectionError(f"closed after {len(received)} of {length} bytes")
            received.extend(piece)
    return bytes(received) == data


def step(results, name, function):
    started = time.monotonic()
    try:
        outcome = function()
        results[name] = {"result": "pass", "seconds": round(time.monotonic() - started, 3), **(outcome or {})}
    except Exception as error:  # A step's failure is its record, and the next step still runs.
        results[name] = {"result": "fail", "seconds": round(time.monotonic() - started, 3),
                         "error": f"{type(error).__name__}: {error}"}
    print(f"{name}: {json.dumps(results[name], sort_keys=True)}", flush=True)


def full_exchange(port, version):
    tls = connect(port, version)
    info = {"version": tls.version(), "cipher": tls.cipher()[0], "cipher_bits": tls.cipher()[2]}
    for length in (16, 4096, 65536):
        if not exchange(tls, length):
            raise ValueError(f"{length} bytes came back changed")
    # A clean close: the client's close_notify, then the server's, with no truncation.
    tls.settimeout(30)
    closed_clean = False
    try:
        tls.unwrap()
        closed_clean = True
    except (ssl.SSLError, OSError) as error:
        info["close_detail"] = f"{type(error).__name__}: {error}"
    info["close_notify_from_server"] = closed_clean
    tls.close()
    return info


def untrusted_is_refused(port, version):
    try:
        connect(port, version, trust=OTHER_CA).close()
    except ssl.SSLCertVerificationError as error:
        return {"refused": str(error.verify_message)}
    except ssl.SSLError as error:
        return {"refused": str(error)}
    raise AssertionError("a client that does not trust the certificate completed a handshake")


def plain_text_gets_nothing(port):
    with socket.create_connection((HOST, port), timeout=15) as raw:
        raw.settimeout(15)
        raw.sendall(b"GET / HTTP/1.0\r\n\r\n")
        answer = b""
        try:
            while True:
                piece = raw.recv(4096)
                if not piece:
                    break
                answer += piece
        except OSError as error:
            return {"answer_bytes": len(answer), "ended": f"{type(error).__name__}"}
    if b"GET" in answer or b"HTTP" in answer:
        raise AssertionError("plain text was answered in plain text")
    return {"answer_bytes": len(answer), "ended": "closed"}


def close_cleanly(tls):
    try:
        tls.unwrap()
    except (ssl.SSLError, OSError):
        pass
    tls.close()


def repeated(port, version, count, close=close_cleanly):
    times = []
    for _ in range(count):
        started = time.monotonic()
        tls = connect(port, version)
        times.append(time.monotonic() - started)
        if not exchange(tls, 64):
            raise ValueError("data changed")
        close(tls)
    return {"count": count, "handshake_seconds_min": round(min(times), 3),
            "handshake_seconds_max": round(max(times), 3)}


def concurrent_limit(port, version, ceiling):
    held = []
    failure = None
    try:
        for _ in range(ceiling):
            try:
                tls = connect(port, version, timeout=20)
                if not exchange(tls, 64):
                    failure = "data changed"
                    break
                held.append(tls)
            except (ssl.SSLError, OSError, ConnectionError) as error:
                failure = f"{type(error).__name__}: {error}"
                break
        # Everything held must still work once the limit has been hit.
        still_working = sum(1 for tls in held if exchange(tls, 64))
    finally:
        for tls in held:
            close_cleanly(tls)
    return {"connections_held": len(held), "tried": ceiling, "stopped_by": failure,
            "still_working": still_working}


def abrupt_closes(probe, port, version, count):
    """A TCP close with no close_notify, as a client that dies does. The server must see each."""
    before = len([line for line in probe.probe_lines() if line.startswith("PROBE close ")])
    outcome = repeated(port, version, count, close=lambda tls: tls.close())
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        seen = len([line for line in probe.probe_lines() if line.startswith("PROBE close ")]) - before
        if seen >= count:
            break
        time.sleep(0.1)
    if seen < count:
        raise AssertionError(f"the server saw {seen} of {count} abrupt closes")
    return outcome


def process_cpu_seconds(pid):
    fields = Path(f"/proc/{pid}/stat").read_text(encoding="ascii").rsplit(")", 1)[1].split()
    return (int(fields[11]) + int(fields[12])) / 100.0


def cpu_share(pid, seconds):
    before = process_cpu_seconds(pid)
    time.sleep(seconds)
    return round((process_cpu_seconds(pid) - before) / seconds, 2)


def raw_poll_after_abrupt_close(probe, port, version):
    """The same close, with the server's sweep turned off. It stays last: the image does not recover."""
    idle_share = cpu_share(probe.process.pid, 2)
    tls = connect(port, version)
    tls.sendall(b"@probe-no-sweep")
    tls.recv(64)
    before = len([line for line in probe.probe_lines() if line.startswith("PROBE close ")])
    tls.close()
    busy_share = cpu_share(probe.process.pid, 3)
    seen = len([line for line in probe.probe_lines() if line.startswith("PROBE close ")]) - before
    return {"cpu_share_idle": idle_share, "cpu_share_after_abrupt_close": busy_share,
            "server_saw_the_close": seen > 0}


def stalled_client(port, version):
    """A client that connects and says nothing, while another tries a handshake."""
    with socket.create_connection((HOST, port), timeout=60):
        time.sleep(0.5)
        started = time.monotonic()
        tls = connect(port, version, timeout=60)
        waited = time.monotonic() - started
        ok = exchange(tls, 64)
        close_cleanly(tls)
    return {"second_client_waited_seconds": round(waited, 3), "second_client_ok": ok}


def host_listeners():
    """The offload's listeners are ordinary sockets in the host's TCP table."""
    found = []
    for line in Path("/proc/net/tcp").read_text(encoding="ascii").splitlines()[1:]:
        fields = line.split()
        if fields[3] == "0A":
            found.append(int(fields[1].split(":")[1], 16))
    return sorted(found)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable")
    parser.add_argument("--profile", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--tls12-port", type=int, default=14212)
    parser.add_argument("--tls13-port", type=int, default=14213)
    parser.add_argument("--concurrent-ceiling", type=int, default=9)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)

    probe = Probe(args.executable, args.output / "probe.log")
    results = {}
    try:
        if probe.wait_for(r"^PROBE ready", 120) is None:
            print("the probe never became ready:", *probe.probe_lines(), sep="\n", file=sys.stderr)
            return 1
        lines = "\n".join(probe.probe_lines())
        listeners = {"1.2": (args.tls12_port, ssl.TLSVersion.TLSv1_2),
                     "1.3": (args.tls13_port, ssl.TLSVersion.TLSv1_3)}
        opened = {}
        for name, (port, _) in listeners.items():
            match = re.search(rf"PROBE listener name=tls{re.escape(name)} port=\d+ result=(-?\d+)", lines)
            opened[name] = match is not None and int(match.group(1)) >= 0
            results[f"listener_tls{name}"] = {"result": "pass" if opened[name] else "fail",
                                              "line": match.group(0) if match else None}
        results["host_tcp_table"] = {"result": "pass" if any(
            listeners[name][0] in host_listeners() for name in opened if opened[name]) else "fail",
            "listening_ports": host_listeners()}
        for name, (port, version) in listeners.items():
            if not opened[name]:
                continue
            step(results, f"tls{name}_exchange", lambda: full_exchange(port, version))
            step(results, f"tls{name}_untrusted_refused", lambda: untrusted_is_refused(port, version))
            step(results, f"tls{name}_plain_text", lambda: plain_text_gets_nothing(port))
        name = "1.3" if opened["1.3"] else "1.2"
        port, version = listeners[name]
        step(results, "repeated_connections", lambda: repeated(port, version, 20))
        step(results, "abrupt_closes", lambda: abrupt_closes(probe, port, version, 6))
        step(results, "concurrent_connections", lambda: concurrent_limit(port, version, args.concurrent_ceiling))
        time.sleep(1)
        step(results, "after_limit_recovers", lambda: repeated(port, version, 2))
        step(results, "stalled_client", lambda: stalled_client(port, version))
        step(results, "after_stall_recovers", lambda: repeated(port, version, 2))
        results["eventfd_in_poll_set"] = {
            "result": "pass" if probe.wait_for(r"eventfd wakes the poll set", 5) else "fail"}
        step(results, "raw_poll_after_abrupt_close", lambda: raw_poll_after_abrupt_close(probe, port, version))
        results["image_alive_at_end"] = {"result": "pass" if probe.process.poll() is None else "fail",
                                         "return_code": probe.process.poll()}
    finally:
        probe.stop()
    (args.output / "probe-lines.txt").write_text("\n".join(probe.probe_lines()) + "\n", encoding="utf-8")
    summary = {"profile": args.profile, "client_openssl": ssl.OPENSSL_VERSION, "steps": results}
    (args.output / "result.json").write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n",
                                             encoding="utf-8")
    failed = [name for name, entry in results.items() if entry["result"] != "pass"]
    print(f"{args.profile}: {len(results) - len(failed)} of {len(results)} steps passed. Failed: {failed}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
