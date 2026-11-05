#!/usr/bin/env python3
"""OpenSSH against a Crux SFTP server: the acceptance cases, and the request sequences of WinSCP.

It runs the real `sftp` client, and a small SFTP version 3 client of its own over `ssh -s`, so that a
case can see the exact status on the wire. The transport is always real OpenSSH. It needs nothing of the
server but a port, a key and the file areas that proof_server.c declares, and --directory, which lets it
look at the medium. A server on native_sim can run the same cases once it has a provider.

The WinSCP cases replay requests that were read from its source. They are not WinSCP.
"""
import argparse
import hashlib
import os
import shutil
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import time

SSH_FXP_INIT, SSH_FXP_VERSION, SSH_FXP_OPEN, SSH_FXP_CLOSE, SSH_FXP_READ, SSH_FXP_WRITE = 1, 2, 3, 4, 5, 6
SSH_FXP_LSTAT, SSH_FXP_FSTAT, SSH_FXP_SETSTAT, SSH_FXP_FSETSTAT = 7, 8, 9, 10
SSH_FXP_OPENDIR, SSH_FXP_READDIR, SSH_FXP_REMOVE, SSH_FXP_MKDIR, SSH_FXP_RMDIR = 11, 12, 13, 14, 15
SSH_FXP_REALPATH, SSH_FXP_STAT, SSH_FXP_RENAME, SSH_FXP_READLINK, SSH_FXP_SYMLINK = 16, 17, 18, 19, 20
SSH_FXP_STATUS, SSH_FXP_HANDLE, SSH_FXP_DATA, SSH_FXP_NAME, SSH_FXP_ATTRS = 101, 102, 103, 104, 105

FX_OK, FX_EOF, FX_NO_SUCH_FILE, FX_PERMISSION_DENIED, FX_FAILURE, FX_OP_UNSUPPORTED = 0, 1, 2, 3, 4, 8

FXF_READ, FXF_WRITE, FXF_APPEND, FXF_CREAT, FXF_TRUNC, FXF_EXCL = 1, 2, 4, 8, 16, 32

ATTR_SIZE, ATTR_UIDGID, ATTR_PERMISSIONS, ATTR_ACMODTIME = 1, 2, 4, 8

BLOCK = 32768
FILE_LIMIT = 1024 * 1024
RESUME_THRESHOLD = 100 * 1024  # WinSCP's default, source/core/CopyParam.cpp


class ConnectionEnded(Exception):
    pass


class Status:
    def __init__(self, code, message):
        self.code = code
        self.message = message

    def __repr__(self):
        return f"Status({self.code}, {self.message!r})"


class Attrs:
    def __init__(self, flags=0, size=None, permissions=None, times=None):
        self.flags = flags
        self.size = size
        self.permissions = permissions
        self.times = times


class Wire:
    """One SFTP session through `ssh -s`, with a record of what each request got."""

    def __init__(self, config, user):
        config.wires.append(self)
        self.process = subprocess.Popen(
            config.ssh_command(user) + ["-s", "sftp"],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
        self.request_id = 0
        self.version = None
        self._send_raw(struct.pack(">BI", SSH_FXP_INIT, 3))
        kind, body = self._read_packet()
        if kind != SSH_FXP_VERSION:
            raise ConnectionEnded(f"expected VERSION, got {kind}")
        self.version = struct.unpack(">I", body[:4])[0]
        self.extensions = body[4:]

    def _send_raw(self, payload):
        try:
            self.process.stdin.write(struct.pack(">I", len(payload)) + payload)
            self.process.stdin.flush()
        except (BrokenPipeError, OSError) as error:
            raise ConnectionEnded(str(error)) from error

    def _read_exact(self, count):
        data = b""
        while len(data) < count:
            piece = self.process.stdout.read(count - len(data))
            if not piece:
                raise ConnectionEnded("end of stream")
            data += piece
        return data

    def _read_packet(self):
        length = struct.unpack(">I", self._read_exact(4))[0]
        data = self._read_exact(length)
        return data[0], data[1:]

    def send(self, kind, body):
        self.request_id += 1
        self._send_raw(struct.pack(">BI", kind, self.request_id) + body)
        return self.request_id

    def receive(self, request_id):
        kind, body = self._read_packet()
        answered = struct.unpack(">I", body[:4])[0]
        if answered != request_id:
            raise AssertionError(f"answer to {answered}, expected {request_id}")
        body = body[4:]
        if kind == SSH_FXP_STATUS:
            code = struct.unpack(">I", body[:4])[0]
            size = struct.unpack(">I", body[4:8])[0]
            return Status(code, body[8:8 + size].decode())
        return kind, body

    def call(self, kind, body):
        return self.receive(self.send(kind, body))

    # ---- Requests ----

    @staticmethod
    def text(value):
        data = value if isinstance(value, bytes) else value.encode()
        return struct.pack(">I", len(data)) + data

    @staticmethod
    def attrs(flags=0, size=None, permissions=None, times=None):
        out = struct.pack(">I", flags)
        if flags & ATTR_SIZE:
            out += struct.pack(">Q", size)
        if flags & ATTR_PERMISSIONS:
            out += struct.pack(">I", permissions)
        if flags & ATTR_ACMODTIME:
            out += struct.pack(">II", times[0], times[1])
        return out

    @staticmethod
    def parse_attrs(body):
        flags = struct.unpack(">I", body[:4])[0]
        at = 4
        found = Attrs(flags)
        if flags & ATTR_SIZE:
            found.size = struct.unpack(">Q", body[at:at + 8])[0]
            at += 8
        if flags & ATTR_UIDGID:
            at += 8
        if flags & ATTR_PERMISSIONS:
            found.permissions = struct.unpack(">I", body[at:at + 4])[0]
            at += 4
        if flags & ATTR_ACMODTIME:
            found.times = struct.unpack(">II", body[at:at + 8])
            at += 8
        return found, at

    def stat(self, path, kind=SSH_FXP_LSTAT):
        answer = self.call(kind, self.text(path))
        return answer if isinstance(answer, Status) else self.parse_attrs(answer[1])[0]

    def realpath(self, path):
        answer = self.call(SSH_FXP_REALPATH, self.text(path))
        if isinstance(answer, Status):
            return answer
        return answer[1][8:8 + struct.unpack(">I", answer[1][4:8])[0]].decode()

    def open(self, path, flags, size=None):
        attributes = self.attrs(ATTR_SIZE, size=size) if size is not None else self.attrs()
        answer = self.call(SSH_FXP_OPEN, self.text(path) + struct.pack(">I", flags) + attributes)
        return answer if isinstance(answer, Status) else answer[1][4:]

    def close(self, handle):
        return self.call(SSH_FXP_CLOSE, self.text(handle))

    def write(self, handle, offset, data):
        return self.call(SSH_FXP_WRITE, self.text(handle) + struct.pack(">Q", offset) + self.text(data))

    def read(self, handle, offset, size):
        answer = self.call(SSH_FXP_READ, self.text(handle) + struct.pack(">QI", offset, size))
        return answer if isinstance(answer, Status) else answer[1][4:]

    def opendir(self, path):
        answer = self.call(SSH_FXP_OPENDIR, self.text(path))
        return answer if isinstance(answer, Status) else answer[1][4:]

    def readdir(self, handle):
        answer = self.call(SSH_FXP_READDIR, self.text(handle))
        if isinstance(answer, Status):
            return answer
        body = answer[1]
        count = struct.unpack(">I", body[:4])[0]
        at = 4
        entries = []
        for _ in range(count):
            size = struct.unpack(">I", body[at:at + 4])[0]
            name = body[at + 4:at + 4 + size].decode()
            at += 4 + size
            size = struct.unpack(">I", body[at:at + 4])[0]
            at += 4 + size
            found, used = self.parse_attrs(body[at:])
            at += used
            entries.append((name, found))
        return entries

    def list(self, path):
        handle = self.opendir(path)
        if isinstance(handle, Status):
            return handle
        names = []
        while True:
            entries = self.readdir(handle)
            if isinstance(entries, Status):
                if entries.code != FX_EOF:
                    raise AssertionError(f"listing {path}: {entries}")
                break
            names.extend(entries)
        expect_ok(self.close(handle))
        return names

    def remove(self, path):
        return self.call(SSH_FXP_REMOVE, self.text(path))

    def rename(self, old, new):
        return self.call(SSH_FXP_RENAME, self.text(old) + self.text(new))

    def setstat(self, path, flags, permissions=None, times=None, size=None):
        return self.call(SSH_FXP_SETSTAT,
                         self.text(path) + self.attrs(flags, size, permissions, times))

    def fsetstat(self, handle, flags, permissions=None, times=None, size=None):
        return self.call(SSH_FXP_FSETSTAT,
                         self.text(handle) + self.attrs(flags, size, permissions, times))

    def finish(self):
        try:
            self.process.stdin.close()
        except OSError:
            pass
        self.process.terminate()
        self.process.wait(timeout=10)

    def kill(self):
        self.process.send_signal(signal.SIGKILL)
        self.process.wait(timeout=10)

    def ended(self, within=5.0):
        """Whether the server ends the session in this time, with no request from here."""
        deadline = time.monotonic() + within
        while time.monotonic() < deadline:
            if self.process.poll() is not None:
                return True
            time.sleep(0.1)
        return False


class Config:
    def __init__(self, arguments):
        self.host = arguments.host
        self.port = arguments.port
        self.key = arguments.key
        self.directory = arguments.directory
        self.server_log = arguments.server_log
        self.wires = []

    def end_wires(self):
        """A case that failed must not keep its sessions, which are few, from the next one."""
        for wire in self.wires:
            if wire.process.poll() is None:
                wire.kill()
        self.wires = []
        time.sleep(0.3)

    def options(self):
        return ["-i", self.key, "-oBatchMode=yes", "-oStrictHostKeyChecking=no",
                "-oUserKnownHostsFile=/dev/null", "-oLogLevel=ERROR"]

    def ssh_command(self, user):
        return ["ssh", "-p", str(self.port)] + self.options() + [f"{user}@{self.host}"]

    def sftp(self, user, commands, extra=()):
        with tempfile.NamedTemporaryFile("w", suffix=".batch", delete=False) as batch:
            batch.write("\n".join(commands) + "\n")
        try:
            return subprocess.run(
                ["sftp", "-b", batch.name, "-P", str(self.port)] + self.options() + list(extra)
                + [f"{user}@{self.host}"],
                capture_output=True, text=True, timeout=120, check=False)
        finally:
            os.unlink(batch.name)

    def disk(self, *parts):
        return os.path.join(self.directory, *parts)

    def events(self):
        with open(self.server_log, encoding="utf-8") as log:
            return [line.strip() for line in log if line.startswith("event ")]


def expect_status(answer, code, message=None):
    if not isinstance(answer, Status) or answer.code != code:
        raise AssertionError(f"expected status {code}, got {answer}")
    if message is not None and answer.message != message:
        raise AssertionError(f"expected message {message!r}, got {answer}")


def expect_ok(answer):
    expect_status(answer, FX_OK)


def expect_true(value, what):
    if not value:
        raise AssertionError(what)


def expect_handle(answer):
    if isinstance(answer, Status):
        raise AssertionError(f"expected a handle, got {answer}")
    return answer


def expect_attrs(answer):
    if isinstance(answer, Status):
        raise AssertionError(f"expected attributes, got {answer}")
    return answer


def pattern(size, seed=0):
    """Bytes that a misplaced block cannot hide in."""
    block = hashlib.sha256(str(seed).encode()).digest()
    return (block * (size // len(block) + 1))[:size]


def digest(path):
    with open(path, "rb") as file:
        return hashlib.sha256(file.read()).hexdigest()


def seed(config, path, content):
    """A file put there through the service, because the service counts each area in RAM from its
    start, so that a file that a case made behind its back would throw out every later limit."""
    wire = Wire(config, "root")
    handle = expect_handle(wire.open(path, FXF_WRITE | FXF_CREAT | FXF_TRUNC))
    if content:
        upload(wire, handle, content)
    expect_ok(wire.close(handle))
    wire.finish()


def drop(config, *paths):
    """Removes files through the service, for the same reason."""
    wire = Wire(config, "root")
    for path in paths:
        expect_ok(wire.remove(path))
    wire.finish()


def upload(wire, handle, data, start=0):
    """Pipelined and in order, as both clients send, with each answer read after the last write."""
    offset = start
    pending = []
    while offset < len(data):
        piece = data[offset:offset + BLOCK]
        pending.append(wire.send(SSH_FXP_WRITE, wire.text(handle) + struct.pack(">Q", offset)
                                 + wire.text(piece)))
        offset += len(piece)
        if len(pending) >= 8:
            expect_ok(wire.receive(pending.pop(0)))
    for request in pending:
        expect_ok(wire.receive(request))


def download(wire, handle, chunk=BLOCK):
    data = b""
    while True:
        piece = wire.read(handle, len(data), chunk)
        if isinstance(piece, Status):
            expect_status(piece, FX_EOF)
            return data
        data += piece


CASES = []


def case(function):
    CASES.append(function)
    return function


# ---- With the OpenSSH sftp client ----


@case
def sftp_lists_the_areas_a_session_may_see(config, work):
    result = config.sftp("root", ["pwd", "ls /"])
    expect_true(result.returncode == 0, result.stderr)
    expect_true("Remote working directory: /" in result.stdout, result.stdout)
    for name in ("data", "logs", "web"):
        expect_true(name in result.stdout, f"{name} missing: {result.stdout}")
    expect_true("inbox" not in result.stdout, result.stdout)
    result = config.sftp("stan", ["ls /"])
    expect_true(result.returncode == 0, result.stderr)
    for name in ("data", "logs", "inbox", "web"):
        expect_true(name not in result.stdout.replace("sftp> ls /", ""), result.stdout)


@case
def sftp_round_trip_small_and_above_a_packet(config, work):
    for size in (0, 1, BLOCK - 1, BLOCK, BLOCK + 1, 200 * 1024, FILE_LIMIT):
        source = os.path.join(work, f"up{size}")
        back = os.path.join(work, f"back{size}")
        with open(source, "wb") as file:
            file.write(pattern(size, size))
        name = f"round{size}"
        result = config.sftp("root", [f"put {source} /data/{name}", f"get /data/{name} {back}"])
        expect_true(result.returncode == 0, f"{size}: {result.stderr}")
        expect_true(digest(source) == digest(back), f"{size}: different after the round trip")
        expect_true(digest(source) == digest(config.disk("data", name)), f"{size}: different on disk")
        drop(config, f"/data/{name}")


@case
def sftp_traversal_reaches_nothing(config, work):
    secret = os.path.join(config.directory, "outside")
    with open(secret, "w", encoding="utf-8") as file:
        file.write("outside the areas")
    local = os.path.join(work, "stolen")
    for path in ("/../outside", "/data/../../outside", "../outside", "/data/../outside",
                 "/data/../../../../../../etc/hostname", "/etc/hostname", "/data/..%2foutside"):
        result = config.sftp("root", [f"get {path} {local}"])
        expect_true(result.returncode != 0, f"get {path} worked")
        expect_true(not os.path.exists(local), f"get {path} wrote {local}")
    source = os.path.join(work, "payload")
    with open(source, "w", encoding="utf-8") as file:
        file.write("payload")
    for path in ("/../planted", "/data/../../planted", "../planted", "/planted", "/data/../planted"):
        result = config.sftp("root", [f"put {source} {path}"])
        expect_true(result.returncode != 0, f"put {path} worked")
    expect_true(not os.path.exists(config.disk("planted")), "a file landed outside the areas")
    # cd stops at the virtual root, however far up it asks.
    result = config.sftp("root", ["cd /data/../..", "pwd"])
    expect_true("Remote working directory: /\n" in result.stdout, result.stdout)
    os.unlink(secret)


@case
def sftp_permission_refusals(config, work):
    seed(config, "/data/" + "doc", b"abc")
    source = os.path.join(work, "payload")
    with open(source, "w", encoding="utf-8") as file:
        file.write("payload")
    local = os.path.join(work, "copy")
    # A viewer reads, and cannot change anything.
    expect_true(config.sftp("vera", [f"get /data/doc {local}"]).returncode == 0, "viewer cannot read")
    for command in (f"put {source} /data/doc", f"put {source} /data/new", "rm /data/doc",
                    "rename /data/doc /data/other"):
        expect_true(config.sftp("vera", [command]).returncode != 0, f"viewer could: {command}")
    with open(config.disk("data", "doc"), "rb") as file:
        expect_true(file.read() == b"abc", "the viewer changed the file")
    expect_true(not os.path.exists(config.disk("data", "new")), "the viewer made a file")
    expect_true(not os.path.exists(config.disk("data", "other")), "the viewer renamed a file")
    # A read-only area refuses an administrator too.
    expect_true(config.sftp("root", [f"put {source} /logs/x"]).returncode != 0, "wrote to logs")
    expect_true(not os.path.exists(config.disk("logs", "x")), "a file landed in logs")
    # An area that offers managed uploads only has no write mode for SFTP.
    expect_true(config.sftp("root", [f"put {source} /web/x"]).returncode != 0, "wrote to web")
    expect_true(not os.path.exists(config.disk("web", "x")), "a file landed in web")
    # A session that cannot see the area is told what a missing one is told.
    expect_true(config.sftp("stan", [f"get /data/doc {local}2"]).returncode != 0, "stranger read")
    expect_true(config.sftp("stan", ["ls /data"]).returncode != 0, "stranger listed")
    # Write without read: it can drop a file, and it cannot read it back.
    expect_true(config.sftp("dora", [f"put {source} /inbox/drop"]).returncode == 0, "cannot drop")
    expect_true(os.path.exists(config.disk("inbox", "drop")), "the drop is missing")
    expect_true(config.sftp("dora", [f"get /inbox/drop {local}3"]).returncode != 0, "dropper read")
    expect_true(config.sftp("dora", ["rm /inbox/drop"]).returncode == 0, "the dropper cannot remove")
    drop(config, "/data/doc")


@case
def sftp_capacity_refusals(config, work):
    over = os.path.join(work, "over")
    with open(over, "wb") as file:
        file.write(pattern(FILE_LIMIT + 1))
    result = config.sftp("root", [f"put {over} /data/too-big"])
    expect_true(result.returncode != 0, "a file above the limit was written")
    size = os.path.getsize(config.disk("data", "too-big"))
    expect_true(size <= FILE_LIMIT, f"the limit let {size} bytes through")
    # What was written stays, as a direct write does, and it can be removed.
    expect_true(config.sftp("root", ["rm /data/too-big"]).returncode == 0, "cannot clean up")
    # The area holds 3 MiB. Three whole files fit, and then there is no room.
    full = os.path.join(work, "full")
    with open(full, "wb") as file:
        file.write(pattern(FILE_LIMIT))
    for name in ("a", "b", "c"):
        expect_true(config.sftp("root", [f"put {full} /data/{name}"]).returncode == 0, name)
    expect_true(config.sftp("root", [f"put {over} /data/d"]).returncode != 0, "the area overflowed")
    for name in ("a", "b", "c", "d"):
        if os.path.exists(config.disk("data", name)):
            expect_true(config.sftp("root", [f"rm /data/{name}"]).returncode == 0, f"rm {name}")


@case
def sftp_rename_and_remove_follow_the_service(config, work):
    seed(config, "/data/" + "doc", b"abc")
    seed(config, "/data/" + "other", b"z")
    result = config.sftp("root", ["rename /data/doc /data/other"])
    expect_true(result.returncode != 0, "rename over a file worked")
    with open(config.disk("data", "other"), "rb") as file:
        expect_true(file.read() == b"z", "the rename replaced the file")
    expect_true(config.sftp("root", ["rename /data/doc /logs/moved"]).returncode != 0, "across areas")
    expect_true(not os.path.exists(config.disk("data", "moved")), "the rename lost the area")
    expect_true(not os.path.exists(config.disk("logs", "moved")), "the rename crossed the areas")
    expect_true(config.sftp("root", ["rm /data/other", "rename /data/doc /data/other"]).returncode == 0,
                "remove and rename")
    expect_true(os.path.exists(config.disk("data", "other")), "the new name is missing")
    expect_true(not os.path.exists(config.disk("data", "doc")), "the old name is still there")
    drop(config, "/data/other")


@case
def sftp_directories_cannot_be_made(config, work):
    result = config.sftp("root", ["mkdir /data/sub"])
    expect_true(result.returncode != 0, "mkdir worked")
    expect_true("Permission denied" in result.stderr + result.stdout, result.stderr)
    expect_true(not os.path.exists(config.disk("data", "sub")), "a directory appeared")
    expect_true(config.sftp("root", ["mkdir /newarea"]).returncode != 0, "a top level directory")
    expect_true(config.sftp("root", ["rmdir /data"]).returncode != 0, "rmdir worked")


@case
def sftp_disconnect_keeps_a_partial_upload_and_resume_finishes_it(config, work):
    source = os.path.join(work, "large")
    data = pattern(600 * 1024, 7)
    with open(source, "wb") as file:
        file.write(data)
    with tempfile.NamedTemporaryFile("w", suffix=".batch", delete=False) as batch:
        batch.write(f"put {source} /data/large.filepart\n")
    client = subprocess.Popen(
        ["sftp", "-b", batch.name, "-l", "800", "-P", str(config.port)] + config.options()
        + [f"root@{config.host}"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    target = config.disk("data", "large.filepart")
    deadline = time.monotonic() + 30
    while time.monotonic() < deadline:
        if os.path.exists(target) and os.path.getsize(target) > 0:
            break
        time.sleep(0.05)
    client.send_signal(signal.SIGKILL)
    client.wait(timeout=10)
    os.unlink(batch.name)
    time.sleep(1.5)
    partial = os.path.getsize(target)
    expect_true(0 < partial < len(data), f"partial size {partial}")
    with open(target, "rb") as file:
        expect_true(file.read() == data[:partial], "the partial file is not a prefix")
    # The name is free again, and `reput` continues from what is there.
    result = config.sftp("root", [f"reput {source} /data/large.filepart",
                                  "rename /data/large.filepart /data/large"])
    expect_true(result.returncode == 0, result.stderr)
    expect_true(digest(source) == digest(config.disk("data", "large")), "resume changed the content")
    drop(config, "/data/large")


# ---- The status on the wire ---


@case
def wire_a_refused_open_is_a_general_failure(config, work):
    """The answer to the question from the contract, from the wire."""
    seed(config, "/data/" + "doc", b"abc")
    root = Wire(config, "root")
    viewer = Wire(config, "vera")
    stranger = Wire(config, "stan")
    open_error = "Open File Error"
    # The session may not write here, or may not see the area, or the area has no such thing.
    expect_status(viewer.open("/data/doc", FXF_WRITE), FX_FAILURE, open_error)
    expect_status(stranger.open("/data/doc", FXF_READ), FX_FAILURE, open_error)
    expect_status(root.open("/data/missing", FXF_READ), FX_FAILURE, open_error)
    expect_status(root.open("/logs/x", FXF_WRITE | FXF_CREAT | FXF_TRUNC), FX_FAILURE, open_error)
    expect_status(root.open("/missing/x", FXF_READ), FX_FAILURE, open_error)
    expect_status(root.open("/data/has space", FXF_WRITE | FXF_CREAT), FX_FAILURE, open_error)
    expect_status(root.open("/data", FXF_READ), FX_FAILURE, "Not A File")
    # A failed STAT says the same whichever of these it was, and so does REMOVE.
    for wire, path in ((root, "/data/missing"), (stranger, "/data/doc"), (root, "/missing")):
        expect_status(wire.stat(path, SSH_FXP_STAT), FX_FAILURE, "STAT error")
        expect_status(wire.stat(path), FX_FAILURE, "LSTAT error")
    expect_status(viewer.remove("/data/doc"), FX_FAILURE, "Remove File Error")
    expect_status(viewer.rename("/data/doc", "/data/x"), FX_FAILURE, "Rename File Error")
    # A hidden area and a missing one give one answer to OPENDIR too, which is NO_SUCH_FILE.
    expect_status(stranger.opendir("/data"), FX_NO_SUCH_FILE)
    expect_status(root.opendir("/missing"), FX_NO_SUCH_FILE)
    # Only MKDIR and RMDIR can carry the permission status, and they always do.
    expect_status(root.call(SSH_FXP_MKDIR, root.text("/data/sub") + root.attrs()),
                  FX_PERMISSION_DENIED, "Permission denied")
    expect_status(root.call(SSH_FXP_RMDIR, root.text("/data")),
                  FX_PERMISSION_DENIED, "Permission denied")
    # Requests with no handler, and a size or owner, in the status that wolfSSH has for each.
    expect_status(root.call(SSH_FXP_SYMLINK, root.text("/data/l") + root.text("/data/doc")), FX_FAILURE)
    expect_status(root.call(SSH_FXP_READLINK, root.text("/data/doc")), FX_FAILURE)
    expect_status(root.setstat("/data/doc", ATTR_SIZE, size=1), FX_OP_UNSUPPORTED)
    # The session carries on after each of them.
    handle = expect_handle(root.open("/data/doc", FXF_READ))
    expect_true(root.read(handle, 0, 3) == b"abc", "the session did not carry on")
    expect_ok(root.close(handle))
    for wire in (root, viewer, stranger):
        wire.finish()
    drop(config, "/data/doc")


@case
def wire_a_limit_refuses_the_write_with_nothing_written(config, work):
    root = Wire(config, "root")
    handle = expect_handle(root.open("/data/edge", FXF_WRITE | FXF_CREAT | FXF_TRUNC))
    upload(root, handle, pattern(FILE_LIMIT - 10))
    expect_status(root.write(handle, FILE_LIMIT - 10, b"x" * 11), FX_FAILURE, "Write File Error")
    expect_ok(root.write(handle, FILE_LIMIT - 10, b"x" * 10))
    # A sparse write has no File behind it.
    expect_status(root.write(handle, FILE_LIMIT + 100, b"x"), FX_FAILURE, "Write File Error")
    expect_ok(root.close(handle))
    expect_true(os.path.getsize(config.disk("data", "edge")) == FILE_LIMIT, "the size is wrong")
    expect_ok(root.remove("/data/edge"))
    root.finish()


@case
def wire_reads_past_the_end_are_the_end(config, work):
    seed(config, "/data/" + "doc", b"abcdef")
    root = Wire(config, "root")
    handle = expect_handle(root.open("/data/doc", FXF_READ))
    expect_true(root.read(handle, 4, 100) == b"ef", "short read")
    expect_status(root.read(handle, 6, 100), FX_EOF)
    expect_status(root.read(handle, 1000, 100), FX_EOF)
    expect_status(root.read(handle, 1 << 40, 100), FX_EOF)
    expect_true(root.read(handle, 0, 2) == b"ab", "reading on after the end")
    expect_ok(root.close(handle))
    root.finish()
    drop(config, "/data/doc")


@case
def wire_open_flags(config, work):
    seed(config, "/data/" + "doc", b"abcdef")
    root = Wire(config, "root")
    # READ with CREAT is not a mode of the service, and a READ of a new name must not make it.
    expect_status(root.open("/data/none", FXF_READ | FXF_CREAT), FX_FAILURE)
    expect_status(root.open("/data/doc", FXF_READ | FXF_TRUNC), FX_FAILURE)
    expect_status(root.open("/data/doc", FXF_WRITE | FXF_TRUNC), FX_FAILURE)
    expect_status(root.open("/data/doc", FXF_WRITE | FXF_CREAT | FXF_EXCL), FX_FAILURE)
    expect_true(os.path.getsize(config.disk("data", "doc")) == 6, "a refused open changed the file")
    expect_true(not os.path.exists(config.disk("data", "none")), "a refused open made a file")
    # APPEND goes to the end, whatever the client gives as an offset.
    handle = expect_handle(root.open("/data/doc", FXF_WRITE | FXF_APPEND))
    expect_ok(root.write(handle, 0, b"gh"))
    expect_ok(root.close(handle))
    with open(config.disk("data", "doc"), "rb") as file:
        expect_true(file.read() == b"abcdefgh", "append did not go to the end")
    # READ and WRITE together.
    handle = expect_handle(root.open("/data/doc", FXF_READ | FXF_WRITE))
    expect_ok(root.write(handle, 0, b"XY"))
    expect_true(root.read(handle, 0, 3) == b"XYc", "read after write")
    fstat = expect_attrs(root.call(SSH_FXP_FSTAT, root.text(handle)))
    expect_true(Wire.parse_attrs(fstat[1])[0].size == 8, "FSTAT size")
    expect_ok(root.close(handle))
    root.finish()
    drop(config, "/data/doc")


@case
def wire_attributes_come_from_the_session(config, work):
    seed(config, "/data/" + "doc", b"abc")
    root = Wire(config, "root")
    viewer = Wire(config, "vera")
    dropper = Wire(config, "dora")
    attrs = expect_attrs(root.stat("/"))
    expect_true(attrs.permissions == 0o040555 and attrs.times is None, oct(attrs.permissions))
    expect_true(expect_attrs(root.stat("/data")).permissions == 0o040755, "area, writer")
    expect_true(expect_attrs(viewer.stat("/data")).permissions == 0o040555, "area, viewer")
    expect_true(expect_attrs(root.stat("/logs")).permissions == 0o040555, "read-only area")
    expect_true(expect_attrs(dropper.stat("/inbox")).permissions == 0o040300, "area, write only")
    attrs = expect_attrs(root.stat("/data/doc"))
    expect_true(attrs.permissions == 0o100644 and attrs.size == 3, oct(attrs.permissions))
    expect_true(attrs.times is None, "F6 leaves out any time")
    expect_true(expect_attrs(viewer.stat("/data/doc")).permissions == 0o100444, "file, viewer")
    # Setting them is accepted, and changes nothing that can be seen.
    expect_ok(root.setstat("/data/doc", ATTR_PERMISSIONS, permissions=0o600))
    expect_ok(root.setstat("/data/doc", ATTR_ACMODTIME, times=(1, 2)))
    attrs = expect_attrs(root.stat("/data/doc"))
    expect_true(attrs.permissions == 0o100644 and attrs.times is None, "setstat had an effect")
    handle = expect_handle(root.open("/data/doc", FXF_READ))
    expect_ok(root.fsetstat(handle, ATTR_PERMISSIONS | ATTR_ACMODTIME, permissions=0o600, times=(1, 2)))
    expect_ok(root.close(handle))
    for wire in (root, viewer, dropper):
        wire.finish()
    drop(config, "/data/doc")


@case
def wire_handles_are_bounded_for_each_session(config, work):
    for name in ("a", "b", "c"):
        seed(config, f"/data/{name}", b"1")
    greedy = Wire(config, "root")
    other = Wire(config, "vera")
    first = expect_handle(greedy.open("/data/a", FXF_READ))
    second = expect_handle(greedy.open("/data/b", FXF_READ))
    expect_status(greedy.open("/data/c", FXF_READ), FX_FAILURE, "Open File Error")
    # Refused before anything happened to the name.
    expect_status(greedy.open("/data/never", FXF_WRITE | FXF_CREAT | FXF_TRUNC), FX_FAILURE)
    expect_true(not os.path.exists(config.disk("data", "never")), "a refused open made a file")
    theirs = expect_handle(other.open("/data/c", FXF_READ))
    expect_ok(other.close(theirs))
    expect_ok(greedy.close(first))
    third = expect_handle(greedy.open("/data/c", FXF_READ))
    expect_ok(greedy.close(second))
    expect_ok(greedy.close(third))
    # wolfSSH looks a handle up by the id that it gave, so that one from another session is unknown.
    stale = expect_handle(other.open("/data/a", FXF_READ))
    expect_status(greedy.read(stale, 0, 1), FX_FAILURE)
    expect_ok(other.close(stale))
    greedy.finish()
    other.finish()
    for name in ("a", "b", "c"):
        drop(config, f"/data/{name}")


@case
def wire_disconnect_gives_every_handle_back(config, work):
    seed(config, "/data/" + "a", b"1")
    wire = Wire(config, "root")
    expect_handle(wire.open("/data/a", FXF_READ))
    expect_handle(wire.open("/data/up.filepart", FXF_WRITE | FXF_CREAT | FXF_TRUNC))
    expect_handle(wire.opendir("/data"))
    wire.kill()
    time.sleep(1.0)
    # All three transfers of the service, and its listing, are free for the next client.
    one = Wire(config, "root")
    two = Wire(config, "root")
    a = expect_handle(one.open("/data/a", FXF_READ))
    b = expect_handle(one.open("/data/up.filepart", FXF_WRITE))
    c = expect_handle(two.open("/data/a", FXF_READ))
    expect_true(isinstance(two.list("/data"), list), "the listing did not come back")
    for wire, handle in ((one, a), (one, b), (two, c)):
        expect_ok(wire.close(handle))
    expect_ok(one.remove("/data/up.filepart"))
    one.finish()
    two.finish()
    drop(config, "/data/a")


@case
def wire_two_clients_keep_the_writer_rule(config, work):
    seed(config, "/data/" + "doc", b"abc")
    one = Wire(config, "root")
    two = Wire(config, "root")
    handle = expect_handle(one.open("/data/doc", FXF_WRITE))
    expect_status(two.open("/data/doc", FXF_WRITE), FX_FAILURE)
    expect_status(two.open("/data/doc", FXF_READ), FX_FAILURE)
    expect_status(two.remove("/data/doc"), FX_FAILURE)
    expect_ok(one.close(handle))
    expect_ok(two.remove("/data/doc"))
    one.finish()
    two.finish()


@case
def wire_a_refused_name_is_recorded_with_no_text(config, work):
    root = Wire(config, "root")
    before = len(config.events())
    expect_status(root.open("/data/secret name", FXF_WRITE | FXF_CREAT), FX_FAILURE)
    time.sleep(0.2)
    new = config.events()[before:]
    expect_true(new, "no event for a refused name")
    expect_true(all(" name=- " in line and "secret" not in line for line in new), f"{new}")
    root.finish()


@case
def wire_a_path_too_long_for_wolfssh_ends_the_connection(config, work):
    # 256 is WOLFSSH_MAX_FILENAME. Below it the service refuses, and the session goes on. From it,
    # wolfSSH returns from the handler with no status, and the server has nothing to answer with.
    root = Wire(config, "root")
    expect_status(root.open("/data/" + "a" * 100, FXF_READ), FX_FAILURE, "Open File Error")
    expect_status(root.open("/data/" + "a" * 249, FXF_READ), FX_FAILURE, "Open File Error")
    expect_true(isinstance(expect_attrs(root.stat("/")), Attrs), "the session did not go on")
    ended = False
    try:
        root.open("/data/" + "a" * 250, FXF_READ)
    except ConnectionEnded:
        ended = True
    expect_true(ended, "a path of the buffer's size was answered")
    root.finish()


# ---- The requests of WinSCP, replayed, from source/core/SftpFileSystem.cpp ----


def winscp_lstat_says_absent(wire, path):
    """RemoteFileExists: any status short of a lost connection means that the file does not exist."""
    expect_true(isinstance(wire.stat(path), Status), f"{path} exists")


def winscp_set_time(wire, path):
    """What it sends after an upload with Preserve timestamp on, which is the default. A failure here
    stops the transfer with PRESERVE_TIME_PERM_ERROR3."""
    expect_ok(wire.setstat(path, ATTR_ACMODTIME, times=(int(time.time()), int(time.time()))))


def winscp_small_upload(wire, path, data):
    """Below the resume threshold, with overwrite confirmation on: EXCL first."""
    handle = wire.open(path, FXF_WRITE | FXF_CREAT | FXF_EXCL, size=len(data))
    if isinstance(handle, Status):
        return handle
    upload(wire, handle, data)
    expect_ok(wire.close(handle))
    winscp_set_time(wire, path)
    return None


@case
def winscp_session_start_and_listing(config, work):
    seed(config, "/data/" + "doc", b"abc")
    wire = Wire(config, "root")
    # Not a well known server, so it asks for version 3 at most, and gets it with no extensions.
    expect_true(wire.version == 3, f"version {wire.version}")
    expect_true(wire.extensions == b"", f"extensions {wire.extensions}")
    expect_true(wire.realpath(".") == "/", "the home directory")
    expect_true(wire.realpath("/") == "/", "the keepalive it sends")
    # TryOpenDirectory is an LSTAT, which must say directory for the root and for an area.
    expect_true(expect_attrs(wire.stat("/")).permissions & 0o040000, "the root is no directory")
    names = [name for name, _ in wire.list("/")]
    expect_true(sorted(names) == ["data", "logs", "web"], f"{names}")
    expect_true(expect_attrs(wire.stat("/data")).permissions & 0o040000, "the area is no directory")
    entries = wire.list("/data")
    expect_true([name for name, _ in entries] == ["doc"], f"{entries}")
    expect_true(entries[0][1].permissions == 0o100644 and entries[0][1].size == 3, "the entry")
    # The rights are not all set, or it would take a small file for a symbolic link and stop resuming.
    expect_true(entries[0][1].permissions & 0o777 != 0o777, "looks like a link")
    # An empty listing makes it read the parent, and only a failure there is an error.
    expect_true(wire.list("/web") == [], "web is empty")
    expect_true(expect_attrs(wire.stat("/web/..")).permissions & 0o040000, "the parent of an area")
    wire.finish()
    drop(config, "/data/doc")


@case
def winscp_small_upload_new_and_overwrite(config, work):
    wire = Wire(config, "root")
    first = pattern(4000, 1)
    expect_true(winscp_small_upload(wire, "/data/small", first) is None, "the new upload")
    with open(config.disk("data", "small"), "rb") as file:
        expect_true(file.read() == first, "the new upload's content")
    # The name exists: EXCL fails, LSTAT finds it, and after the user's confirmation it truncates.
    second = pattern(1000, 2)
    refused = wire.open("/data/small", FXF_WRITE | FXF_CREAT | FXF_EXCL, size=len(second))
    expect_status(refused, FX_FAILURE, "Open File Error")
    expect_true(expect_attrs(wire.stat("/data/small")).size == len(first), "LSTAT after EXCL")
    handle = expect_handle(wire.open("/data/small", FXF_WRITE | FXF_CREAT | FXF_TRUNC, size=len(second)))
    upload(wire, handle, second)
    expect_ok(wire.close(handle))
    winscp_set_time(wire, "/data/small")
    with open(config.disk("data", "small"), "rb") as file:
        expect_true(file.read() == second, "the overwrite's content")
    wire.finish()
    drop(config, "/data/small")


@case
def winscp_large_upload_through_filepart(config, work):
    wire = Wire(config, "root")
    data = pattern(RESUME_THRESHOLD + 50000, 3)
    # New: two LSTATs that fail mean nothing is there, then the partial name, then a rename.
    winscp_lstat_says_absent(wire, "/data/big.bin")
    winscp_lstat_says_absent(wire, "/data/big.bin.filepart")
    handle = expect_handle(wire.open("/data/big.bin.filepart", FXF_WRITE | FXF_CREAT | FXF_TRUNC,
                                     size=len(data)))
    upload(wire, handle, data)
    expect_ok(wire.close(handle))
    expect_ok(wire.rename("/data/big.bin.filepart", "/data/big.bin"))
    winscp_set_time(wire, "/data/big.bin")
    with open(config.disk("data", "big.bin"), "rb") as file:
        expect_true(file.read() == data, "the new upload's content")
    # Over an existing file: it removes the target and then renames, because this server has no
    # posix-rename and a rename over a name is an error.
    newer = pattern(RESUME_THRESHOLD + 1000, 4)
    expect_true(expect_attrs(wire.stat("/data/big.bin")).size == len(data), "the target")
    winscp_lstat_says_absent(wire, "/data/big.bin.filepart")
    handle = expect_handle(wire.open("/data/big.bin.filepart", FXF_WRITE | FXF_CREAT | FXF_TRUNC,
                                     size=len(newer)))
    upload(wire, handle, newer)
    expect_ok(wire.close(handle))
    expect_status(wire.rename("/data/big.bin.filepart", "/data/big.bin"), FX_FAILURE)
    expect_ok(wire.remove("/data/big.bin"))
    expect_ok(wire.rename("/data/big.bin.filepart", "/data/big.bin"))
    winscp_set_time(wire, "/data/big.bin")
    with open(config.disk("data", "big.bin"), "rb") as file:
        expect_true(file.read() == newer, "the overwrite's content")
    wire.finish()
    drop(config, "/data/big.bin")


@case
def winscp_resume_of_an_interrupted_upload(config, work):
    data = pattern(RESUME_THRESHOLD + 70000, 5)
    wire = Wire(config, "root")
    handle = expect_handle(wire.open("/data/res.bin.filepart", FXF_WRITE | FXF_CREAT | FXF_TRUNC,
                                     size=len(data)))
    upload(wire, handle, data[:BLOCK * 2])
    wire.kill()
    time.sleep(1.0)
    wire = Wire(config, "root")
    winscp_lstat_says_absent(wire, "/data/res.bin")
    offset = expect_attrs(wire.stat("/data/res.bin.filepart")).size
    expect_true(offset == BLOCK * 2, f"the partial size is {offset}")
    # Resuming: neither EXCL nor TRUNC, and writes at the offset.
    handle = expect_handle(wire.open("/data/res.bin.filepart", FXF_WRITE | FXF_CREAT))
    upload(wire, handle, data, start=offset)
    expect_ok(wire.close(handle))
    expect_ok(wire.rename("/data/res.bin.filepart", "/data/res.bin"))
    winscp_set_time(wire, "/data/res.bin")
    with open(config.disk("data", "res.bin"), "rb") as file:
        expect_true(file.read() == data, "the resumed content")
    # Declining to resume removes the partial file.
    handle = expect_handle(wire.open("/data/no.filepart", FXF_WRITE | FXF_CREAT | FXF_TRUNC))
    expect_ok(wire.write(handle, 0, b"x"))
    expect_ok(wire.close(handle))
    expect_ok(wire.remove("/data/no.filepart"))
    wire.finish()
    drop(config, "/data/res.bin")


@case
def winscp_name_with_a_space_is_refused_at_every_step(config, work):
    """F4 keeps the name rule. These are the requests it sends, and each is refused."""
    wire = Wire(config, "root")
    data = pattern(RESUME_THRESHOLD + 10, 6)
    # Below the threshold: EXCL fails, then the LSTAT fails, so it shows the first error.
    expect_status(wire.open("/data/my file.txt", FXF_WRITE | FXF_CREAT | FXF_EXCL, size=10), FX_FAILURE,
                  "Open File Error")
    expect_status(wire.stat("/data/my file.txt"), FX_FAILURE, "LSTAT error")
    # Above it: both LSTATs say absent, the partial name fails, it falls back to the target as above.
    winscp_lstat_says_absent(wire, "/data/my file.txt")
    winscp_lstat_says_absent(wire, "/data/my file.txt.filepart")
    expect_status(wire.open("/data/my file.txt.filepart", FXF_WRITE | FXF_CREAT | FXF_TRUNC, size=len(data)),
                  FX_FAILURE, "Open File Error")
    expect_status(wire.open("/data/my file.txt", FXF_WRITE | FXF_CREAT | FXF_EXCL, size=len(data)),
                  FX_FAILURE, "Open File Error")
    expect_status(wire.stat("/data/my file.txt"), FX_FAILURE, "LSTAT error")
    # The session goes on, and the same file under a name the rule accepts goes through.
    handle = expect_handle(wire.open("/data/my_file.txt", FXF_WRITE | FXF_CREAT | FXF_EXCL, size=4))
    expect_ok(wire.write(handle, 0, b"abcd"))
    expect_ok(wire.close(handle))
    # A name that exists only on the medium, with a space, is not listed, so it cannot be fetched.
    with open(config.disk("data", "from a card"), "wb") as file:
        file.write(b"x")
    names = [name for name, _ in wire.list("/data")]
    expect_true(names == ["my_file.txt"], f"{names}")
    # A foreign file is invisible to the service and is not counted, so that this leaves no trace.
    os.remove(config.disk("data", "from a card"))
    expect_ok(wire.remove("/data/my_file.txt"))
    wire.finish()


@case
def winscp_a_name_too_long_for_the_partial_suffix_falls_back(config, work):
    # 63 is the name limit. A name of 55 to 63 bytes fits, but its partial name does not, and the
    # smart resume of WinSCP then writes the target directly.
    name = "n" * 60
    wire = Wire(config, "root")
    data = pattern(RESUME_THRESHOLD + 10, 8)
    expect_status(wire.open(f"/data/{name}.filepart", FXF_WRITE | FXF_CREAT | FXF_TRUNC, size=len(data)),
                  FX_FAILURE, "Open File Error")
    handle = expect_handle(wire.open(f"/data/{name}", FXF_WRITE | FXF_CREAT | FXF_EXCL, size=len(data)))
    upload(wire, handle, data)
    expect_ok(wire.close(handle))
    expect_ok(wire.remove(f"/data/{name}"))
    wire.finish()


@case
def winscp_download_rename_and_delete(config, work):
    data = pattern(RESUME_THRESHOLD + 5000, 9)
    seed(config, "/data/" + "get.bin", data)
    wire = Wire(config, "root")
    # Download: LSTAT, OPEN for READ, reads of the block it computes, CLOSE.
    expect_true(expect_attrs(wire.stat("/data/get.bin")).size == len(data), "the size")
    handle = expect_handle(wire.open("/data/get.bin", FXF_READ))
    expect_true(download(wire, handle) == data, "the download's content")
    expect_ok(wire.close(handle))
    # wolfSSH's channel limit is what keeps its read request within what wolfSSH takes.
    handle = expect_handle(wire.open("/data/get.bin", FXF_READ))
    expect_true(len(wire.read(handle, 0, BLOCK)) == BLOCK, "a read of the largest block")
    expect_ok(wire.close(handle))
    expect_ok(wire.rename("/data/get.bin", "/data/got.bin"))
    expect_ok(wire.remove("/data/got.bin"))
    wire.finish()


# ---- Last, because it waits out the idle limit, and needs the short limit of its own server ----


def idle_session_ends_and_activity_does_not(config, idle_limit):
    wire = Wire(config, "root")
    # Requests that reach the port keep the session past the limit.
    deadline = time.monotonic() + idle_limit * 1.5
    while time.monotonic() < deadline:
        expect_attrs(wire.stat("/"))
        time.sleep(idle_limit / 4)
    # REALPATH, the keepalive of WinSCP, does not reach the port, so it keeps nothing.
    deadline = time.monotonic() + idle_limit + 2
    try:
        while time.monotonic() < deadline:
            wire.realpath("/")
            time.sleep(idle_limit / 4)
        raise AssertionError("the session outlived its idle limit")
    except ConnectionEnded:
        pass
    wire.finish()
    # And plain silence does too.
    wire = Wire(config, "vera")
    expect_true(wire.ended(idle_limit + 3), "the session outlived its idle limit, in silence")
    wire.finish()


def wait_for_server(host, port):
    deadline = time.monotonic() + 20
    while time.monotonic() < deadline:
        try:
            socket.create_connection((host, port), timeout=1).close()
            return
        except OSError:
            time.sleep(0.2)
    raise SystemExit(f"nothing listens on {host}:{port}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, required=True)
    parser.add_argument("--key", required=True)
    parser.add_argument("--directory", required=True, help="the directory that holds the areas")
    parser.add_argument("--server-log", required=True)
    parser.add_argument("--only", help="run only the cases whose name holds this text")
    parser.add_argument("--idle-limit", type=int,
                        help="only the idle case, on a server with this idle limit in seconds")
    arguments = parser.parse_args()
    config = Config(arguments)
    wait_for_server(config.host, config.port)
    if arguments.idle_limit is not None:
        idle_session_ends_and_activity_does_not(config, arguments.idle_limit)
        print("PASS idle_session_ends_and_activity_does_not")
        return 0
    failures = 0
    work = tempfile.mkdtemp(prefix="crux-sftp-")
    try:
        for function in CASES:
            if arguments.only and arguments.only not in function.__name__:
                continue
            start = time.monotonic()
            try:
                function(config, work)
                config.end_wires()
                print(f"PASS {function.__name__} ({time.monotonic() - start:.1f} s)", flush=True)
            except (AssertionError, ConnectionEnded, subprocess.TimeoutExpired) as error:
                failures += 1
                config.end_wires()
                print(f"FAIL {function.__name__}: {error}", flush=True)
    finally:
        shutil.rmtree(work)
    print(f"{len(CASES) if not arguments.only else 'selected'} cases, {failures} failures")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
