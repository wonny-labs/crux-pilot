#!/usr/bin/env python3
"""One native_sim image with HTTPS, the command line over SSH and SFTP, from outside (Issue #197).

OpenSSH `ssh` and `sftp` and `curl` are the clients, and they reach one running image: the services image of
tests/native/management_services with the wolfSSH provider beside the HTTPS listener, on the Wolf profile's real
PSA providers. The image runs as its own process on a disk file. This driver reuses the image and OpenSSH helpers
of tests/zephyr/ssh_provider/openssh_test.py and the constants of tests/native/management_services/driver.py.

What it shows, with the entropy test-only and the host key in test form, on native_sim and nothing else:

- the services' own cases, which the image prints, all pass with SSH running;
- an OpenSSH client logs in with an Ed25519 key and runs CLI commands, and password and wrong-key logins behave;
- OpenSSH sftp uploads and downloads files that a client of the file service API also sees;
- curl logs in over HTTPS and calls the management API of the same image;
- the three together: load on every listener at once, the session limits with both busy, a silent client on one
  listener against the other, and the account's session limit across channels;
- the listening sockets of the process are exactly the two declared ones, and the host key outlives a restart.

    driver.py <zephyr.exe> [--output dir]

Timings come from an emulated container and say nothing about a target.
"""
import argparse
import hashlib
import importlib.util
import json
import os
import pathlib
import re
import socket
import subprocess
import sys
import tempfile
import threading
import time

HERE = pathlib.Path(__file__).resolve().parent
REPOSITORY = HERE.parents[2]


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


provider = load("provider_openssh_test", REPOSITORY / "tests/zephyr/ssh_provider/openssh_test.py")
services = load("services_driver", REPOSITORY / "tests/native/management_services/driver.py")

SSH_PORT = 2222
WEB_PORT = services.PORT
ADMIN, ADMIN_PASSWORD = services.ADMIN
VIEWER, VIEWER_PASSWORD = services.VIEWER
GUEST, GUEST_PASSWORD = services.GUEST
CERTIFICATE = services.TEST_CERTIFICATE
ORIGIN = services.ORIGIN
EXPECTED_CASES = services.EXPECTED_CASES
AREA = "files"
SESSIONS_PER_ACCOUNT = 3

results = {"checks": {}, "measurements": {}}
failures = []


def check(condition, message):
    results["checks"][message] = bool(condition)
    if not condition:
        failures.append(message)
        print(f"FAIL: {message}", flush=True)
    else:
        print(f"ok:   {message}", flush=True)
    return bool(condition)


def measure(name, value):
    results["measurements"][name] = value
    print(f"data: {name} = {value}", flush=True)


def quantiles(samples):
    ordered = sorted(samples)
    if not ordered:
        return {}
    return {"n": len(ordered), "min": round(ordered[0], 3), "median": round(ordered[len(ordered) // 2], 3),
            "max": round(ordered[-1], 3)}


# ---------------------------------------------------------------------------------------------- curl

class Curl:
    """curl against the HTTPS listener, with the declared test certificate as the only trust anchor."""

    def __init__(self, directory):
        self.jar = pathlib.Path(directory) / f"jar-{id(self)}.txt"

    def run(self, method, path, headers=(), body=None, jar=True, timeout=60, compressed=False):
        command = ["curl", "--silent", "--show-error", "--tlsv1.3", "--cacert", str(CERTIFICATE),
                   "--resolve", f"localhost:{WEB_PORT}:127.0.0.1", "--max-time", str(timeout),
                   "-X", method, "-w", "\n%{http_code}", f"https://localhost:{WEB_PORT}{path}"]
        if jar:
            command += ["-b", str(self.jar), "-c", str(self.jar)]
        if compressed:
            # The packaged folder is served gzipped, as a browser asks for it.
            command.append("--compressed")
        for name, value in headers:
            command += ["-H", f"{name}: {value}"]
        if body is not None:
            command += ["-H", "Content-Type: application/json", "--data-binary", json.dumps(body)]
        started = time.monotonic()
        done = subprocess.run(command, capture_output=True, text=True, timeout=timeout + 10)
        elapsed = time.monotonic() - started
        text = done.stdout
        status = 0
        if "\n" in text:
            text, code = text.rsplit("\n", 1)
            status = int(code) if code.isdigit() else 0
        try:
            decoded = json.loads(text) if text else None
        except ValueError:
            decoded = None
        return {"status": status, "body": text, "json": decoded, "seconds": elapsed,
                "returncode": done.returncode, "stderr": done.stderr}

    def xsrf(self):
        for line in self.jar.read_text().splitlines():
            fields = line.split("\t")
            if len(fields) >= 7 and fields[5] == "XSRF-TOKEN":
                return fields[6]
        return None

    def login(self, user, password):
        """The two requests of the web login, as a browser makes them. True once the session is held."""
        self.jar.write_text("")
        reply = self.run("POST", "/api/v1/session", [("Origin", ORIGIN)], {"user": user, "password": password})
        if reply["status"] != 202 or not reply["json"]:
            return False
        started = time.monotonic()
        while time.monotonic() - started < 90:
            final = self.run("GET", f"/api/v1/session/attempt/{reply['json']['id']}", [("X-Crux-Request", "1")])
            if final["status"] == 200 and final["json"] == {"state": "pending"}:
                time.sleep(0.1)
                continue
            return final["status"] == 200 and final["json"] == {"state": "success"}
        return False

    def write_headers(self):
        return [("Origin", ORIGIN), ("X-XSRF-TOKEN", self.xsrf() or "")]


# ----------------------------------------------------------------------------------------- OpenSSH

class Clients:
    """The OpenSSH tools of tests/zephyr/ssh_provider/openssh_test.py, with sftp added."""

    def __init__(self, directory):
        self.directory = pathlib.Path(directory)
        self.ssh = provider.Client(self.directory)

    def key_run(self, command, user=ADMIN, key="ed25519", stdin=None, timeout=120, extra=()):
        started = time.monotonic()
        done = self.ssh.run(command, self.ssh.key_options(key) + list(extra), user=user, stdin=stdin,
                            timeout=timeout)
        return done, time.monotonic() - started

    def password_run(self, command, user, password, timeout=180):
        return self.ssh.run(command, self.ssh.password_options(), answers=(password,), user=user,
                            timeout=timeout)

    def sftp(self, batch, user=ADMIN, key="ed25519", timeout=240):
        batch_file = self.directory / f"batch-{threading.get_ident()}-{time.monotonic_ns()}.txt"
        batch_file.write_text("\n".join(batch) + "\n")
        command = ["sftp", "-F", "/dev/null", "-P", str(SSH_PORT), "-b", str(batch_file),
                   "-o", "StrictHostKeyChecking=no", "-o", "UserKnownHostsFile=/dev/null",
                   "-o", "ConnectTimeout=20", "-o", "PreferredAuthentications=publickey",
                   "-o", "IdentitiesOnly=yes", "-o", "IdentityAgent=none", "-o", "BatchMode=yes",
                   "-i", str(self.ssh.key(key)), f"{user}@127.0.0.1"]
        started = time.monotonic()
        done = subprocess.run(command, capture_output=True, text=True, timeout=timeout)
        return done, time.monotonic() - started


# --------------------------------------------------------------------------------- the process

def listening_ports(pid):
    """TCP ports in LISTEN state that this process holds, read from /proc, and its open socket count."""
    inodes = set()
    sockets = 0
    for entry in pathlib.Path(f"/proc/{pid}/fd").iterdir():
        try:
            target = os.readlink(entry)
        except OSError:
            continue
        match = re.match(r"socket:\[(\d+)\]", target)
        if match:
            inodes.add(match.group(1))
            sockets += 1
    ports = set()
    for table in ("/proc/net/tcp", "/proc/net/tcp6"):
        try:
            lines = pathlib.Path(table).read_text().splitlines()[1:]
        except OSError:
            continue
        for line in lines:
            fields = line.split()
            if fields[3] == "0A" and fields[9] in inodes:
                ports.add(int(fields[1].rsplit(":", 1)[1], 16))
    return ports, sockets


def thread_count(pid):
    return len(list(pathlib.Path(f"/proc/{pid}/task").iterdir()))


def sha256(data):
    return hashlib.sha256(data).hexdigest()


# ------------------------------------------------------------------------------------------ cases

class Run:
    def __init__(self, binary, directory, disk, seed, key_blob_hex):
        self.directory = pathlib.Path(directory)
        self.image = provider.Image(binary, f"key={key_blob_hex}", options=(f"--flash={disk}", f"--seed={seed}"),
                                    ready=False)
        self.image.wait_for(r"^CRUX_SERVICES_READY", 240)
        self.image.wait_for(r"^SSH_READY", 5)
        self.pid = self.image.process.pid

    def close(self):
        self.image.__exit__()

    def lines(self):
        return self.image.snapshot()

    def fingerprint(self):
        for line in self.lines():
            match = re.match(r"^SSH-HOST-KEY (SHA256:\S+)$", line)
            if match:
                return match.group(1)
        return None


def case_boot(run, clients):
    text = "\n".join(run.lines())
    seen = {name: verdict == "ok" for name, verdict in re.findall(r"^CASE (\S+) (ok|FAIL)", text, re.MULTILINE)}
    check(EXPECTED_CASES <= set(seen), f"every case of the services image ran with SSH present: {len(seen)} cases")
    for name in sorted(EXPECTED_CASES & set(seen)):
        check(seen[name], f"services case {name}, with the SSH listener and threads present")
    check("CRUX_SERVICES_CASES failures=0" in text, "the image reports no failed case")
    check(run.fingerprint() is not None, "the image prints the host key's fingerprint, and never the key")
    scan = subprocess.run(["ssh-keyscan", "-p", str(SSH_PORT), "-t", "ed25519", "127.0.0.1"],
                          capture_output=True, text=True, timeout=60).stdout
    shown = subprocess.run(["ssh-keygen", "-lf", "-"], input=scan, capture_output=True, text=True).stdout
    check(run.fingerprint() is not None and run.fingerprint() in shown,
          "ssh-keyscan and ssh-keygen report the fingerprint that the image printed")


def case_ssh_cli(run, clients):
    done, seconds = clients.key_run("help")
    check(done.returncode == 0 and b"account list" in done.stdout,
          f"ssh, Ed25519 key, admin: help exits 0 and lists commands ({seconds:.1f} s)")
    done, _ = clients.key_run("account list")
    names = done.stdout.decode(errors="replace")
    check(done.returncode == 0 and all(user in names for user in (ADMIN, VIEWER, GUEST)),
          f"ssh admin: account list shows the accounts of the image's own cases: {names.split()}")
    done, _ = clients.key_run("account list", user=VIEWER)
    check(done.returncode == 127 and GUEST not in done.stdout.decode(errors="replace"),
          "ssh viewer: account list is hidden by the role, exit 127")
    done, _ = clients.key_run("no-such-command")
    check(done.returncode == 127, "ssh: an unknown command exits 127")
    done, _ = clients.key_run("network recover")
    check(done.returncode == 127, "ssh: network recover is serial only, so hidden over SSH, exit 127")
    done, _ = clients.key_run("logout")
    check(done.returncode == 0, "ssh: logout ends the session with status 0")
    done, _ = clients.key_run("help", key="other")
    check(done.returncode == 255, "ssh: a key that no account holds is refused")
    done, _ = clients.key_run("help", key="ecdsa")
    check(done.returncode == 255, "ssh: an ECDSA key that no account holds is refused")
    done, _ = clients.key_run("help", key="rsa")
    check(done.returncode == 255, "ssh: an RSA key is refused")
    shell, _ = clients.key_run(None, stdin=b"help\nlogout\n", extra=["-T"])
    check(shell.returncode == 0 and b"account list" in shell.stdout, "ssh: an interactive shell runs help and logout")
    started = time.monotonic()
    done = clients.password_run("help", ADMIN, ADMIN_PASSWORD)
    check(done.returncode == 0 and b"account list" in done.stdout,
          f"ssh: password login of the first account works too ({time.monotonic() - started:.1f} s)")
    done = clients.password_run("help", ADMIN, "not the password")
    check(done.returncode == 255, "ssh: a wrong password is refused")


def case_sftp(run, clients):
    work = clients.directory / "sftp"
    work.mkdir(exist_ok=True)
    small = os.urandom(3000)
    large = os.urandom(60000)
    (work / "small.bin").write_bytes(small)
    (work / "large.bin").write_bytes(large)
    done, seconds = clients.sftp(["ls /", f"put {work}/small.bin /{AREA}/small.bin",
                                  f"put {work}/large.bin /{AREA}/large.bin", f"ls -l /{AREA}"])
    check(done.returncode == 0 and AREA in done.stdout, f"sftp, Ed25519 key, admin: the area is the root, upload ok ({seconds:.1f} s)")
    check("small.bin" in done.stdout and "large.bin" in done.stdout, "sftp: both uploads are listed")
    done, seconds = clients.sftp([f"get /{AREA}/small.bin {work}/small.down", f"get /{AREA}/large.bin {work}/large.down"])
    check(done.returncode == 0 and (work / "small.down").read_bytes() == small and
          (work / "large.down").read_bytes() == large,
          f"sftp: 3,000 and 60,000 bytes come back identical, SHA-256 {sha256(large)[:12]} ({seconds:.1f} s)")
    # The image's own case wrote note.txt through the file service API, with no SFTP involved.
    done, _ = clients.sftp([f"get /{AREA}/note.txt {work}/note.down"])
    note = (work / "note.down").read_text() if (work / "note.down").exists() else ""
    check(done.returncode == 0 and note == "a file written through the file service",
          "sftp reads the file that the file service API wrote: one file area behind both")
    done, _ = clients.sftp([f"put {work}/small.bin /{AREA}/viewer.bin"], user=VIEWER)
    check(done.returncode != 0, "sftp viewer: a write is refused, the role has file.read only")
    done, _ = clients.sftp([f"get /{AREA}/small.bin {work}/viewer.down"], user=VIEWER)
    check(done.returncode == 0 and (work / "viewer.down").read_bytes() == small, "sftp viewer: a read works")
    done, _ = clients.sftp([f"put {work}/small.bin /{AREA}/over.bin"], key="other")
    check(done.returncode != 0, "sftp: a key that no account holds is refused")
    done, _ = clients.sftp([f"rm /{AREA}/viewer.bin"])
    check(done.returncode != 0, "sftp: removing a file that was never written fails")
    done, _ = clients.sftp([f"rm /{AREA}/large.bin", f"rm /{AREA}/small.bin"])
    check(done.returncode == 0, "sftp: remove works for the administrator")


def events_emitted(curl):
    """security.events_emitted of the status route, as the web reads it, or None."""
    for item in (curl.run("GET", "/api/v1/status")["json"] or {}).get("items", []):
        if item.get("name") == "security.events_emitted":
            try:
                return int(item.get("value"))
            except (TypeError, ValueError):
                return None
    return None


def case_https(run, clients):
    curl = Curl(clients.directory)
    check(curl.login(ADMIN, ADMIN_PASSWORD), "curl: the two-request HTTPS login of the first account works")
    me = curl.run("GET", "/api/v1/session/me")
    check(me["status"] == 200 and (me["json"] or {}).get("user") == ADMIN, "curl: GET /api/v1/session/me names the user")
    status = curl.run("GET", "/api/v1/status")
    names = [item.get("name") for item in (status["json"] or {}).get("items", [])]
    check(status["status"] == 200 and "security.events_emitted" in names,
          f"curl: GET /api/v1/status returns {len(names)} items")
    update = curl.run("PUT", "/api/v1/config/demo.display_name", curl.write_headers(), {"value": "set by curl"})
    read = curl.run("GET", "/api/v1/config/demo.display_name")
    check(update["status"] == 200 and (read["json"] or {}).get("value") == "set by curl",
          "curl: a configuration update over HTTPS reads back")
    check(curl.run("PUT", "/api/v1/config/demo.display_name", [("Origin", ORIGIN)], {"value": "x"})["status"] == 403,
          "curl: the same update without its CSRF token is refused, 403")
    anonymous = Curl(clients.directory)
    check(anonymous.run("GET", "/api/v1/status", jar=False)["status"] == 401, "curl: no cookie, 401")
    page = anonymous.run("GET", "/", jar=False, compressed=True)
    script = anonymous.run("GET", "/app.js", jar=False, compressed=True)
    check(page["status"] == 200 and "Crux native_sim test image" in page["body"] and script["status"] == 200 and
          "management API" in script["body"], "curl: the packaged page and its script that a browser opens are served")
    wrong = Curl(clients.directory)
    check(not wrong.login(ADMIN, "not the password"), "curl: a wrong password does not log in")
    # Events that SSH raises reach the same store that the web's status counts. The count is read before and after
    # SSH logins, one that succeeds and one that fails. The count is read live, and the wait lets the drain thread
    # record the events.
    before = events_emitted(curl)
    done, _ = clients.key_run("help")
    refused = clients.password_run("help", ADMIN, "not the password")
    check(done.returncode == 0 and refused.returncode == 255, "curl: the SSH logins that raise the events ran")
    time.sleep(3)
    after = events_emitted(curl)
    check(isinstance(before, int) and isinstance(after, int) and after > before,
          f"curl: status counts the security events that SSH logins raised: {before} then {after}")
    check(curl.run("DELETE", "/api/v1/session", curl.write_headers())["status"] in (200, 204), "curl: logout")
    check(curl.run("GET", "/api/v1/status")["status"] == 401, "curl: an ended session is 401")


def case_ports(run, clients):
    ports, sockets = listening_ports(run.pid)
    measure("listening_ports", sorted(ports))
    check(ports == {SSH_PORT, WEB_PORT}, f"the process listens on exactly the two declared ports: {sorted(ports)}")
    measure("sockets_idle", sockets)


def case_sessions_across_channels(run, clients):
    """One account holds three sessions, whichever channel opened them, and the oldest then ends."""
    shells = []
    curl = Curl(clients.directory)
    try:
        for _ in range(SESSIONS_PER_ACCOUNT):
            process = clients.ssh.popen(None, clients.ssh.key_options("ed25519") + ["-T"], user=ADMIN)
            process.stdin.write(b"help\n")
            process.stdin.flush()
            shells.append(process)
            time.sleep(1)
        check(all(process.poll() is None for process in shells),
              f"{SESSIONS_PER_ACCOUNT} SSH shells of one account stay open")
        check(curl.login(ADMIN, ADMIN_PASSWORD), "a web login of the same account works with the shells open")
        started = time.monotonic()
        while shells[0].poll() is None and time.monotonic() - started < 15:
            time.sleep(0.2)
        ended = shells[0].poll() is not None
        check(ended, f"the account's oldest session, an SSH shell, ended when the web login made a fourth "
                     f"({time.monotonic() - started:.1f} s after it)")
        check(all(process.poll() is None for process in shells[1:]), "and the two newer SSH shells stayed open")
        check(curl.run("GET", "/api/v1/session/me")["status"] == 200, "and the web session is the live one")
    finally:
        for process in shells:
            if process.poll() is None:
                process.kill()
            process.wait()
        # A web session that nobody ends holds one of the shared slots until its idle limit, and a later case
        # counts the slots, so this one is ended here.
        curl.run("DELETE", "/api/v1/session", curl.write_headers())


def case_slots_with_both_busy(run, clients):
    """Every SSH slot taken, then the core's session slots, which HTTPS and SSH share."""
    held = []
    web = []
    try:
        users = [(ADMIN, "ed25519"), (ADMIN, "ed25519"), (VIEWER, "ed25519"), (VIEWER, "ed25519")]
        for user, key in users:
            process = clients.ssh.popen(None, clients.ssh.key_options(key) + ["-T"], user=user)
            process.stdin.write(b"help\n")
            process.stdin.flush()
            held.append(process)
            time.sleep(1)
        check(all(process.poll() is None for process in held), "four SSH shells hold every slot, two accounts, two each")
        ports, sockets = listening_ports(run.pid)
        measure("sockets_four_ssh_sessions", sockets)
        measure("threads_four_ssh_sessions", thread_count(run.pid))
        done, _ = clients.key_run("help", user=GUEST)
        check(done.returncode == 255 and b"too many connections" in done.stderr.lower(),
              "a fifth SSH client is refused with 'too many connections', not left to wait")
        # The core has seven network session slots, one of its eight being reserved for serial. Four hold SSH
        # shells, so three web logins fit, on three accounts so that no account passes its own cap of three.
        started = time.monotonic()
        for user, password in ((GUEST, GUEST_PASSWORD), (VIEWER, VIEWER_PASSWORD), (ADMIN, ADMIN_PASSWORD)):
            curl = Curl(clients.directory)
            logged_in = curl.login(user, password)
            check(logged_in and curl.run("GET", "/api/v1/session/me")["status"] == 200,
                  f"HTTPS login and call of {user} work with four SSH shells open")
            web.append(curl)
        measure("https_three_logins_with_ssh_full_s", round(time.monotonic() - started, 2))
        ports, sockets = listening_ports(run.pid)
        measure("sockets_four_ssh_and_three_https_sessions", sockets)
        # The eighth session has no slot, on either channel. A web attempt reports it as busy.
        refused = Curl(clients.directory)
        check(not refused.login(GUEST, GUEST_PASSWORD),
              "an eighth session is refused: all seven network slots are held, by both channels")
        reply = refused.run("POST", "/api/v1/session", [("Origin", ORIGIN)], {"user": GUEST, "password": GUEST_PASSWORD})
        final = None
        if reply["status"] == 202:
            for _ in range(100):
                final = refused.run("GET", f"/api/v1/session/attempt/{reply['json']['id']}", [("X-Crux-Request", "1")])
                if final["json"] != {"state": "pending"}:
                    break
                time.sleep(0.1)
        measure("eighth_session_web_answer", [final["status"], final["body"]] if final else reply["status"])
        check(final is not None and final["status"] == 503 and final["json"] == {"code": "busy"},
              "and the answer is 503 busy, which is what a user of the web sees")
        # One SSH shell leaves, and the slot is the web's again.
        held[0].kill()
        held[0].wait()
        time.sleep(2)
        again = Curl(clients.directory)
        check(again.login(GUEST, GUEST_PASSWORD), "one SSH shell leaves, and its slot is the web's: the same login works")
        web.append(again)
    finally:
        for process in held:
            if process.poll() is None:
                process.kill()
            process.wait()
        for curl in web:
            curl.run("DELETE", "/api/v1/session", curl.write_headers())
    time.sleep(1)
    done, _ = clients.key_run("help")
    check(done.returncode == 0, "SSH logs in again once the slots are free")


def silent_connections(port, count):
    connections = []
    for _ in range(count):
        connection = socket.create_connection(("127.0.0.1", port), timeout=10)
        connections.append(connection)
    return connections


def case_silent_clients(run, clients):
    """A client that sends nothing on one listener, against a user of the other."""
    connections = silent_connections(SSH_PORT, 4)
    try:
        time.sleep(1)
        curl = Curl(clients.directory)
        started = time.monotonic()
        ok = curl.login(VIEWER, VIEWER_PASSWORD) and curl.run("GET", "/api/v1/status")["status"] == 200
        elapsed = time.monotonic() - started
        measure("https_login_and_call_with_four_silent_ssh_clients_s", round(elapsed, 2))
        check(ok, "HTTPS works while four silent clients hold every SSH slot")
        done, _ = clients.key_run("help")
        check(done.returncode == 255, "and a real SSH user is refused meanwhile: the recorded slot lockout, decision 20")
    finally:
        for connection in connections:
            connection.close()
    time.sleep(1)
    done, _ = clients.key_run("help")
    check(done.returncode == 0, "SSH works again once the silent clients leave")
    # The other way round: silent TLS clients, each of which stalls the server thread in its handshake.
    connections = silent_connections(WEB_PORT, 3)
    try:
        time.sleep(0.5)
        done, seconds = clients.key_run("account list")
        measure("ssh_command_with_three_silent_https_clients_s", round(seconds, 2))
        check(done.returncode == 0, f"an SSH command works while three silent clients sit on the HTTPS port ({seconds:.1f} s)")
        started = time.monotonic()
        status = Curl(clients.directory).run("GET", "/api/v1/status", jar=False, timeout=60)
        measure("https_request_with_three_silent_tls_clients_s", round(time.monotonic() - started, 2))
        check(status["status"] == 401, "an HTTPS request is still answered meanwhile, after the handshake limit")
    finally:
        for connection in connections:
            connection.close()
    time.sleep(3)


def case_load_on_every_listener(run, clients):
    """SFTP transfers, CLI commands and HTTPS calls, all at once, against the same image."""
    work = clients.directory / "load"
    work.mkdir(exist_ok=True)
    payload = os.urandom(48000)
    (work / "payload.bin").write_bytes(payload)
    curl = Curl(clients.directory)
    # Viewer for HTTPS, administrator for SSH and SFTP, so that no account passes its three sessions.
    check(curl.login(VIEWER, VIEWER_PASSWORD), "load: an HTTPS session for the run")
    # Idle baselines first.
    https_idle = [curl.run("GET", "/api/v1/status")["seconds"] for _ in range(5)]
    ssh_idle = [clients.key_run("account list")[1] for _ in range(3)]
    sftp_idle = []
    for index in range(2):
        done, seconds = clients.sftp([f"put {work}/payload.bin /{AREA}/idle{index}.bin",
                                      f"get /{AREA}/idle{index}.bin {work}/idle{index}.down",
                                      f"rm /{AREA}/idle{index}.bin"])
        sftp_idle.append(seconds if done.returncode == 0 else float("nan"))
    measure("idle_https_status_s", quantiles(https_idle))
    measure("idle_ssh_command_s", quantiles(ssh_idle))
    measure("idle_sftp_put_get_rm_48kB_s", quantiles(sftp_idle))
    before_ports, before_sockets = listening_ports(run.pid)
    outcomes = {"https": [], "ssh": [], "sftp": []}
    latencies = {"https": [], "ssh": [], "sftp": []}
    peak = {"sockets": before_sockets, "threads": thread_count(run.pid)}
    stop = threading.Event()

    def https_worker():
        mine = Curl(clients.directory)
        if not mine.login(VIEWER, VIEWER_PASSWORD):
            outcomes["https"].append(False)
            return
        for _ in range(12):
            reply = mine.run("GET", "/api/v1/status")
            outcomes["https"].append(reply["status"] == 200)
            latencies["https"].append(reply["seconds"])

    def ssh_worker():
        for _ in range(8):
            done, seconds = clients.key_run("account list")
            outcomes["ssh"].append(done.returncode == 0 and VIEWER.encode() in done.stdout)
            latencies["ssh"].append(seconds)

    def sftp_worker():
        for index in range(4):
            done, seconds = clients.sftp([f"put {work}/payload.bin /{AREA}/load{index}.bin",
                                          f"get /{AREA}/load{index}.bin {work}/load{index}.down",
                                          f"rm /{AREA}/load{index}.bin"])
            same = (work / f"load{index}.down").exists() and (work / f"load{index}.down").read_bytes() == payload
            outcomes["sftp"].append(done.returncode == 0 and same)
            latencies["sftp"].append(seconds)

    def sampler():
        while not stop.is_set():
            _, sockets = listening_ports(run.pid)
            peak["sockets"] = max(peak["sockets"], sockets)
            peak["threads"] = max(peak["threads"], thread_count(run.pid))
            time.sleep(0.2)

    threads = [threading.Thread(target=function) for function in (https_worker, ssh_worker, sftp_worker)]
    watcher = threading.Thread(target=sampler)
    watcher.start()
    for thread in threads:
        thread.start()
    for thread in threads:
        thread.join(timeout=900)
    stop.set()
    watcher.join()
    for kind in ("https", "ssh", "sftp"):
        check(outcomes[kind] and all(outcomes[kind]),
              f"load: every {kind} operation succeeded while the other two ran: {sum(outcomes[kind])} of {len(outcomes[kind])}")
        measure(f"loaded_{kind}_s", quantiles(latencies[kind]))
    measure("peak_sockets_under_load", peak["sockets"])
    measure("peak_threads_under_load", peak["threads"])
    after_ports, after_sockets = listening_ports(run.pid)
    check(after_ports == {SSH_PORT, WEB_PORT}, "the same two ports listen after the load")
    time.sleep(8)
    _, settled = listening_ports(run.pid)
    check(settled <= before_sockets, f"sockets return to the idle count after the load: {before_sockets}, then {settled}")
    check(run.image.process.poll() is None, "the image is still running after the load")


def last_measure(run):
    found = None
    for line in run.lines():
        match = re.match(r"^SSH-MEASURE heap_peak=(\d+) heap_now=(\d+) worker_stack_used=(\d+) listener_stack_used=(\d+)", line)
        if match:
            found = [int(value) for value in match.groups()]
    return found


def case_resource_use(run, clients):
    # The deepest key exchange of #192's measurements, so that the stack figure covers it too.
    done, _ = clients.key_run("help", extra=["-o", "KexAlgorithms=ecdh-sha2-nistp256"])
    check(done.returncode == 0, "ssh: a login with the ecdh-sha2-nistp256 key exchange works too")
    time.sleep(3)
    found = last_measure(run)
    check(found is not None, "the image reported its heap and the stacks of the SSH threads")
    if found:
        measure("heap_peak_bytes", found[0])
        measure("heap_now_bytes_after_load", found[1])
        measure("ssh_worker_stack_used_bytes", found[2])
        measure("ssh_listener_stack_used_bytes", found[3])
    ports, sockets = listening_ports(run.pid)
    measure("sockets_at_rest_after_load", sockets)
    measure("threads_at_rest_after_load", thread_count(run.pid))


def run_cases(binary, directory, output):
    clients = Clients(directory)
    key = clients.ssh.blob_hex("ed25519")
    disk = pathlib.Path(directory) / "disk.bin"
    # Each case needs a clean slate of sessions, and the image keeps its disk, so one image serves them all.
    run = Run(binary, directory, disk, 1971, key)
    first_fingerprint = run.fingerprint()
    try:
        for case in (case_boot, case_ssh_cli, case_sftp, case_https, case_ports, case_sessions_across_channels,
                     case_slots_with_both_busy, case_silent_clients, case_load_on_every_listener,
                     case_resource_use):
            print(f"--- {case.__name__}", flush=True)
            try:
                case(run, clients)
            except Exception as error:  # a case that raises is a failure, and the rest still run
                check(False, f"{case.__name__} raised {type(error).__name__}: {error}")
        (output / "integrated-first.log").write_text("\n".join(run.lines()) + "\n")
    finally:
        run.close()
    # The host key and the accounts' keys live on the disk, so a second process finds the same ones.
    print("--- case_restart", flush=True)
    second = Run(binary, directory, disk, 1972, key)
    try:
        check(second.fingerprint() == first_fingerprint, f"a restart on the same disk keeps the host key: {first_fingerprint}")
        done, _ = clients.key_run("help")
        check(done.returncode == 0, "and the stored public key still logs in after the restart")
        check(Curl(clients.directory).login(ADMIN, ADMIN_PASSWORD), "and HTTPS login works after the restart")
        (output / "integrated-second.log").write_text("\n".join(second.lines()) + "\n")
    finally:
        second.close()


def revision():
    def git(*arguments):
        done = subprocess.run(["git", "-C", str(REPOSITORY), *arguments], capture_output=True, text=True)
        return done.stdout.strip() if done.returncode == 0 else None
    status = git("status", "--porcelain")
    return {"revision": git("rev-parse", "HEAD") or "unknown", "dirty": None if status is None else bool(status)}


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("executable", type=pathlib.Path)
    parser.add_argument("--output", type=pathlib.Path, default=pathlib.Path("."))
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    results.update(revision())
    with tempfile.TemporaryDirectory() as temporary:
        run_cases(args.executable.resolve(), temporary, args.output)
    results["failures"] = failures
    (args.output / "integrated-results.json").write_text(json.dumps(results, indent=1) + "\n")
    print(f"integrated image: {len(failures)} failures, {len(results['checks'])} checks, "
          f"results in {args.output / 'integrated-results.json'}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
