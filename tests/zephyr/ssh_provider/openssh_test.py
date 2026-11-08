#!/usr/bin/env python3
"""The wolfSSH provider against a real OpenSSH client, on native_sim over the native socket offload.

Each case boots the image, because delay state and limits belong to one run. It uses only the standard
library and the OpenSSH tools. What the image holds behind the provider is declared in
docs/ssh-wolf-provider.md: test Access ports, a generated test host key and stand-in sessions. These
results are native_sim results only.
"""
import argparse
import base64
import faulthandler
import hashlib
import os
from pathlib import Path
import re
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time

PORT = 2222
HOST = "127.0.0.1"
USER = "operator"
PASSWORD = "correct horse battery"
WRONG = "not the password"
STRICT_SERVER = "kex-strict-s-v00@openssh.com"
STRICT_CLIENT = "kex-strict-c-v00@openssh.com"
KEX = ["curve25519-sha256", "ecdh-sha2-nistp256"]
CIPHERS = ["aes128-gcm@openssh.com", "aes256-gcm@openssh.com", "aes128-ctr"]
MACS = ["hmac-sha2-256"]
SERVER_TIMEOUT = 60


class Image:
    def __init__(self, binary, *arguments, options=(), ready=True):
        command = [str(binary), *options]
        if arguments:
            command += ["-testargs", *arguments]
        self.process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                        text=True, errors="replace", cwd=binary.parent)
        self.lines = []
        self.lock = threading.Lock()
        self.reader = threading.Thread(target=self._read, daemon=True)
        self.reader.start()
        if ready:
            self.wait_for(r"^READY port=", SERVER_TIMEOUT)

    def _read(self):
        for line in self.process.stdout:
            with self.lock:
                self.lines.append(line.rstrip("\n"))

    def snapshot(self):
        with self.lock:
            return list(self.lines)

    def wait_for(self, pattern, timeout=20.0, start=0):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            for line in self.snapshot()[start:]:
                if re.search(pattern, line):
                    return line
            if self.process.poll() is not None:
                time.sleep(0.2)
                for line in self.snapshot()[start:]:
                    if re.search(pattern, line):
                        return line
                raise AssertionError(f"image exited before {pattern!r}: {self.snapshot()[-15:]}")
            time.sleep(0.02)
        raise AssertionError(f"no {pattern!r} in the image's output: {self.snapshot()[-15:]}")

    def mark(self):
        return len(self.snapshot())

    def count(self, pattern, start=0):
        return sum(1 for line in self.snapshot()[start:] if re.search(pattern, line))

    def events(self, name, start=0):
        return self.count(rf"^ACCESS event type={name} ", start)

    def derives(self, start=0):
        return self.count(r"^ACCESS derive ", start)

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.process.kill()
        self.process.wait()


class Client:
    """OpenSSH with no configuration file, and answers from an askpass script."""

    def __init__(self, directory):
        self.directory = Path(directory)
        self.answers = self.directory / "answers"
        self.askpass = self.directory / "askpass.py"
        self.askpass.write_text(
            "#!/usr/bin/env python3\n"
            "from pathlib import Path\n"
            f"path = Path({str(self.answers)!r})\n"
            "lines = path.read_text().split('\\n')\n"
            "print(lines[0])\n"
            "if len(lines) > 2:\n"
            "    path.write_text('\\n'.join(lines[1:]))\n")
        self.askpass.chmod(0o700)
        for name, kind in (("ed25519", "ed25519"), ("ecdsa", "ecdsa"), ("other", "ed25519"),
                           ("rsa", "rsa")):
            arguments = ["-b", "256"] if kind == "ecdsa" else []
            subprocess.run(["ssh-keygen", "-q", "-t", kind, "-N", "", "-f", str(self.key(name)),
                            *arguments], check=True)

    def key(self, name):
        return self.directory / name

    def blob_hex(self, name):
        text = (self.directory / f"{name}.pub").read_text().split()[1]
        return base64.b64decode(text).hex()

    def base(self, extra=()):
        return ["ssh", "-F", "/dev/null", "-p", str(PORT), "-o", "StrictHostKeyChecking=no",
                "-o", "UserKnownHostsFile=/dev/null", "-o", "ConnectTimeout=20", *extra]

    def password_options(self, prompts=1):
        return ["-o", "PreferredAuthentications=password", "-o", "PubkeyAuthentication=no",
                "-o", f"NumberOfPasswordPrompts={prompts}"]

    def key_options(self, name):
        return ["-o", "PreferredAuthentications=publickey", "-o", "IdentitiesOnly=yes",
                "-o", "IdentityAgent=none", "-o", "BatchMode=yes", "-i", str(self.key(name))]

    def run(self, command, options, answers=(PASSWORD,), user=USER, stdin=None, timeout=60,
            verbose=()):
        return self.start(command, options, answers, user, verbose, stdin, timeout)

    def start(self, command, options, answers, user, verbose, stdin, timeout):
        process = self.popen(command, options, answers, user, verbose)
        output, errors = process.communicate(stdin, timeout=timeout)
        return subprocess.CompletedProcess(process.args, process.returncode, output, errors)

    def popen(self, command, options, answers=(PASSWORD,), user=USER, verbose=()):
        self.answers.write_text("\n".join(answers) + "\n")
        environment = dict(os.environ, SSH_ASKPASS=str(self.askpass), SSH_ASKPASS_REQUIRE="force",
                           DISPLAY=":0")
        arguments = self.base(verbose) + options + [f"{user}@{HOST}"]
        if command is not None:
            arguments.append(command)
        return subprocess.Popen(arguments, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, env=environment)


def check(condition, message):
    if not condition:
        raise AssertionError(message)


# ---- Raw protocol, for what a normal client hides ----

def read_exact(connection, length):
    data = b""
    while len(data) < length:
        piece = connection.recv(length - len(data))
        if not piece:
            raise ConnectionError("closed")
        data += piece
    return data


def read_version(connection):
    line = b""
    while not line.endswith(b"\n"):
        piece = connection.recv(1)
        if not piece:
            raise ConnectionError("closed")
        line += piece
    return line


def read_plain_packet(connection):
    length, padding = struct.unpack(">IB", read_exact(connection, 5))
    body = read_exact(connection, length - 1)
    return body[:len(body) - padding]


def plain_packet(payload):
    padding = 8 - ((5 + len(payload)) % 8)
    if padding < 4:
        padding += 8
    return struct.pack(">IB", 1 + len(payload) + padding, padding) + payload + bytes(padding)


def name_lists(payload, count=10):
    at = 17
    lists = []
    for _ in range(count):
        length = struct.unpack(">I", payload[at:at + 4])[0]
        lists.append(payload[at + 4:at + 4 + length].decode().split(","))
        at += 4 + length
    return lists


def client_kexinit(strict):
    kex = "curve25519-sha256" + (f",{STRICT_CLIENT}" if strict else "")
    fields = [kex, "ssh-ed25519", "aes128-ctr", "aes128-ctr", "hmac-sha2-256", "hmac-sha2-256",
              "none", "none", "", ""]
    body = b"\x14" + bytes(16)
    for field in fields:
        body += struct.pack(">I", len(field)) + field.encode()
    return body + b"\x00" + bytes(4)


def raw_connect():
    return socket.create_connection((HOST, PORT), timeout=10)


def closed_within(connection, seconds):
    """True if the server ends the connection in time. Drains what it sends meanwhile."""
    connection.settimeout(seconds)
    end = time.monotonic() + seconds
    try:
        while time.monotonic() < end:
            if connection.recv(4096) == b"":
                return True
    except socket.timeout:
        return False
    except ConnectionError:
        return True
    return False


# ---- Cases ----

class Flood:
    """Sends SSH_MSG_IGNORE in the clear before any key exchange, which a client may do."""

    def __init__(self):
        self.connection = raw_connect()
        read_version(self.connection)
        self.connection.sendall(b"SSH-2.0-test\r\n")
        self.started = time.monotonic()
        self.ended = None
        self.thread = threading.Thread(target=self._run, daemon=True)
        self.thread.start()

    def _run(self):
        batch = plain_packet(b"\x02\x00\x00\x00\x00") * 2048
        self.connection.settimeout(1.0)
        limit = self.started + 30.0
        try:
            while time.monotonic() < limit:
                try:
                    self.connection.sendall(batch)
                except socket.timeout:
                    continue
        except OSError:
            self.ended = time.monotonic()

    def finish(self):
        self.thread.join(35.0)
        self.connection.close()
        return None if self.ended is None else self.ended - self.started


def sftp_packet(kind, body):
    return struct.pack(">IB", 1 + len(body), kind) + body


def read_sftp_packet(stream):
    length = struct.unpack(">I", stream.read(4))[0]
    return stream.read(length)


def hold_with_large_packet():
    """A client before keys that announces the largest packet, and sends only a little of it."""
    connection = raw_connect()
    read_version(connection)
    connection.sendall(b"SSH-2.0-test\r\n")
    connection.sendall(struct.pack(">I", 34988) + bytes(16))
    return connection


class Cases:
    def __init__(self, binary, client):
        self.binary = binary
        self.client = client
        self.keys = [f"key={client.blob_hex('ed25519')}", f"key={client.blob_hex('ecdsa')}"]

    def image(self, *arguments):
        return Image(self.binary, *self.keys, *arguments)

    def case_host_key_fingerprint_matches_openssh(self):
        for argument, kind in ((), "ed25519"), (("host=ecdsa",), "ecdsa"):
            with self.image(*argument) as image:
                printed = image.wait_for(r"^HOST-KEY SHA256:").split()[1]
                scan = subprocess.run(["ssh-keyscan", "-p", str(PORT), "-t", kind, HOST],
                                      capture_output=True, text=True, timeout=30, check=True)
                seen = subprocess.run(["ssh-keygen", "-lf", "-"], input=scan.stdout,
                                      capture_output=True, text=True, check=True).stdout
                check(printed in seen, f"{kind}: printed {printed}, OpenSSH saw {seen}")

    def case_password_login(self):
        with self.image() as image:
            result = self.client.run("whoami", self.client.password_options())
            check(result.returncode == 0, result.stderr)
            check(result.stdout == b"operator administrator\n", result.stdout)
            image.wait_for(r"^ACCESS event type=LOGIN_SUCCESS .*channel=3 .*account=operator")
            image.wait_for(r"^ACCESS event type=LOGOUT ")

    def case_key_login(self):
        with self.image() as image:
            for name in "ed25519", "ecdsa":
                mark = image.mark()
                result = self.client.run("whoami", self.client.key_options(name))
                check(result.returncode == 0, f"{name}: {result.stderr}")
                check(result.stdout == b"operator administrator\n", result.stdout)
                check(image.derives(mark) == 0, "a key login ran the password verifier")
                image.wait_for(r"^ACCESS event type=LOGOUT ", start=mark)

    def case_wrong_password(self):
        with self.image() as image:
            mark = image.mark()
            result = self.client.run("whoami", self.client.password_options(), answers=(WRONG,))
            check(result.returncode == 255, result.returncode)
            check(b"Permission denied" in result.stderr, result.stderr)
            check(result.stdout == b"", result.stdout)
            check(image.events("LOGIN_FAILURE", mark) == 1, image.snapshot()[mark:])
            check(image.events("LOGIN_SUCCESS", mark) == 0, image.snapshot()[mark:])

    def case_wrong_user(self):
        with self.image() as image:
            mark = image.mark()
            known = self.client.run("whoami", self.client.password_options(), answers=(WRONG,))
            unknown = self.client.run("whoami", self.client.password_options(), answers=(WRONG,),
                                      user="nobody")
            check(unknown.returncode == 255, unknown.returncode)
            check(image.events("LOGIN_SUCCESS", mark) == 0, image.snapshot()[mark:])
            # The client sees no difference between a wrong password and an unknown name.
            words = lambda text: text.replace(USER.encode(), b"").replace(b"nobody", b"")
            check(words(known.stderr) == words(unknown.stderr), (known.stderr, unknown.stderr))
            check(image.derives(mark) == 2, "an unknown name must cost the same verification")

    def case_wrong_key(self):
        with self.image() as image:
            for name in "other", "rsa":
                mark = image.mark()
                result = self.client.run("whoami", self.client.key_options(name))
                check(result.returncode == 255, f"{name}: {result.returncode}")
                check(b"Permission denied" in result.stderr, result.stderr)
                check(image.events("LOGIN_SUCCESS", mark) == 0, image.snapshot()[mark:])

    def case_attempts_on_a_connection_are_bounded(self):
        with self.image("attempts=3") as image:
            mark = image.mark()
            result = self.client.run("whoami", self.client.password_options(6),
                                     answers=(WRONG,) * 6)
            check(result.returncode == 255, result.returncode)
            check(image.events("LOGIN_FAILURE", mark) == 3, image.snapshot()[mark:])
            check(b"disconnect" in result.stderr.lower() or b"closed" in result.stderr.lower(),
                  result.stderr)

    def case_refused_attempts_meet_the_delay(self):
        # Five failures are free, and the sixth opens a window of a second. The attempts share a
        # connection, because a new client process can take longer than the window to start.
        with self.image("attempts=10") as image:
            mark = image.mark()
            started = time.monotonic()
            result = self.client.run("whoami", self.client.password_options(8),
                                     answers=(WRONG,) * 6 + (PASSWORD,) + (WRONG,))
            elapsed = time.monotonic() - started
            check(result.returncode == 255, f"the right password in the window: {result.stderr}")
            check(image.derives(mark) == 6, f"derived {image.derives(mark)} times, not 6")
            check(image.events("LOGIN_FAILURE", mark) == 6, image.snapshot()[mark:])
            check(image.events("DELAY_APPLIED", mark) == 2, image.snapshot()[mark:])
            check(image.events("LOGIN_SUCCESS", mark) == 0, image.snapshot()[mark:])
            print(f"    the eight attempts took {elapsed:.2f} s of wall clock")

            # The window ends by itself, and the right password then works.
            time.sleep(1.2)
            mark = image.mark()
            result = self.client.run("whoami", self.client.password_options())
            check(result.returncode == 0, result.stderr)
            check(image.derives(mark) == 1, image.snapshot()[mark:])

    def case_algorithm_list(self):
        with self.image() as image:
            connection = raw_connect()
            version = read_version(connection)
            check(version == b"SSH-2.0-Crux\r\n", version)
            connection.sendall(b"SSH-2.0-test\r\n")
            payload = read_plain_packet(connection)
            check(payload[0] == 20, payload[0])
            lists = name_lists(payload)
            connection.close()
            print(f"    kex {lists[0]}\n    host key {lists[1]}\n    cipher {lists[2]}\n"
                  f"    mac {lists[4]}\n    compression {lists[6]}")
            # wolfSSH adds these markers itself. They name no algorithm that can be chosen.
            check(lists[0] == KEX + ["ext-info-s", "kex-strict-s", STRICT_SERVER], lists[0])
            check(lists[1] == ["ssh-ed25519"], lists[1])
            check(lists[2] == CIPHERS and lists[3] == CIPHERS, lists[2:4])
            check(lists[4] == MACS and lists[5] == MACS, lists[4:6])
            check(lists[6] == ["none"] and lists[7] == ["none"], lists[6:8])
            everything = ",".join(",".join(item) for item in lists).lower()
            for word in "sha1", "cbc", "3des", "des-", "rc4", "arcfour", "rsa", "group1":
                check(word not in everything, f"{word} is offered")
            image.process.poll()

    def case_each_algorithm_negotiates(self):
        with self.image() as image:
            for option, values in (("KexAlgorithms", KEX), ("Ciphers", CIPHERS), ("MACs", MACS)):
                for value in values:
                    extra = ["-o", f"{option}={value}"]
                    if option == "MACs":
                        extra += ["-o", "Ciphers=aes128-ctr"]
                    result = self.client.run("whoami", self.client.key_options("ed25519") + extra,
                                             verbose=("-v",))
                    check(result.returncode == 0, f"{value}: {result.stderr}")
                    check(value.encode() in result.stderr, f"{value} not chosen: {result.stderr}")
            image.process.poll()

    def case_refused_algorithms_do_not_negotiate(self):
        refused = (("Ciphers", "aes128-cbc"), ("Ciphers", "aes256-cbc"), ("Ciphers", "3des-cbc"),
                   ("MACs", "hmac-sha1"), ("KexAlgorithms", "diffie-hellman-group14-sha1"),
                   ("KexAlgorithms", "diffie-hellman-group14-sha256"), ("HostKeyAlgorithms", "ssh-rsa"),
                   ("HostKeyAlgorithms", "rsa-sha2-256"))
        with self.image() as image:
            for option, value in refused:
                extra = ["-o", f"{option}={value}"]
                if option == "MACs":
                    extra += ["-o", "Ciphers=aes128-ctr"]
                result = self.client.run("whoami", self.client.key_options("ed25519") + extra)
                check(result.returncode == 255, f"{value} negotiated")
                check(b"no matching" in result.stderr.lower() or b"unable to negotiate"
                      in result.stderr.lower(), f"{value}: {result.stderr}")
            image.process.poll()

    def case_strict_key_exchange(self):
        with self.image() as image:
            # OpenSSH says that it applies.
            result = self.client.run("whoami", self.client.key_options("ed25519"),
                                     verbose=("-vvv",))
            check(result.returncode == 0, result.stderr)
            check(b"will use strict KEX ordering" in result.stderr.lower()
                  or b"strict kex" in result.stderr.lower(), result.stderr[-600:])

            # A message out of order, during the first key exchange of a strict client, ends it.
            connection = raw_connect()
            read_version(connection)
            connection.sendall(b"SSH-2.0-test\r\n")
            read_plain_packet(connection)
            connection.sendall(plain_packet(client_kexinit(True)))
            connection.sendall(plain_packet(b"\x02\x00\x00\x00\x00"))
            check(closed_within(connection, 3.0), "the server kept a strict client's stray message")
            connection.close()

            # The control: the same message from a client without the marker is ignored.
            connection = raw_connect()
            read_version(connection)
            connection.sendall(b"SSH-2.0-test\r\n")
            read_plain_packet(connection)
            connection.sendall(plain_packet(client_kexinit(False)))
            connection.sendall(plain_packet(b"\x02\x00\x00\x00\x00"))
            check(not closed_within(connection, 1.0), "the control was closed")
            connection.close()
            image.process.poll()

    def case_connection_limit(self):
        with self.image("connections=2", "auth-ms=20000") as image:
            held = [raw_connect(), raw_connect()]
            for connection in held:
                read_version(connection)
            mark = image.mark()
            # Several times, because the message once lost a race with the client's own write.
            for _ in range(10):
                result = self.client.run("whoami", self.client.key_options("ed25519"))
                check(result.returncode == 255, result.returncode)
                check(b"too many connections" in result.stderr.lower(), result.stderr)
            check(not image.count(r"^ACCESS ", mark), image.snapshot()[mark:])
            check(not closed_within(held[0], 0.3), "the limit disturbed a held connection")
            held[0].close()
            time.sleep(0.3)
            result = self.client.run("whoami", self.client.key_options("ed25519"))
            check(result.returncode == 0, result.stderr)
            held[1].close()

    def case_unauthenticated_time_limit(self):
        with self.image("auth-ms=1500") as image:
            connection = raw_connect()
            started = time.monotonic()
            read_version(connection)
            check(closed_within(connection, 5.0), "a silent client stayed connected")
            elapsed = time.monotonic() - started
            check(1.2 < elapsed < 3.0, f"closed after {elapsed:.2f} s")
            print(f"    a silent client was closed after {elapsed:.2f} s, for a limit of 1.50 s")

            # It is a limit on logging in, so a session may outlast it.
            result = self.client.run("sleep 2500", self.client.key_options("ed25519"))
            check(result.returncode == 0, result.stderr)
            image.process.poll()

    def case_flood_before_keys_meets_the_login_limit(self):
        with self.image("auth-ms=1500", "connections=4") as image:
            flood = Flood()
            started = time.monotonic()
            other = self.client.run("whoami", self.client.key_options("ed25519"))
            other_elapsed = time.monotonic() - started
            check(other.returncode == 0, other.stderr)
            check(other_elapsed < 3.0, f"another client took {other_elapsed:.2f} s")
            elapsed = flood.finish()
            check(elapsed is not None, "the flood was never closed")
            check(elapsed < 4.0, f"the flood lasted {elapsed:.2f} s for a limit of 1.50 s")
            print(f"    the flood ended after {elapsed:.2f} s, and another client took "
                  f"{other_elapsed:.2f} s")
            image.process.poll()

    def case_stop_ends_a_flood(self):
        with self.image("auth-ms=60000", "connections=4") as image:
            flood = Flood()
            time.sleep(0.3)
            mark = image.mark()
            started = time.monotonic()
            self.client.run("stop", self.client.key_options("ed25519"))
            image.wait_for(r"^STOPPED 0", 10.0, mark)
            print(f"    stop returned {time.monotonic() - started:.2f} s after it was asked")
            check(flood.finish() is not None, "the flood's connection was not ended")

    def case_rekey_during_a_transfer(self):
        with self.image() as image:
            data = os.urandom(200000)
            result = self.client.run("cat", self.client.key_options("ed25519") +
                                     ["-o", "RekeyLimit=32K"], stdin=data, verbose=("-v",))
            check(result.returncode == 0, (result.returncode, result.stderr[-400:]))
            check(result.stdout == data, f"{len(result.stdout)} bytes came back")
            rekeys = result.stderr.count(b"SSH2_MSG_NEWKEYS sent") - 1
            check(rekeys >= 2, f"{rekeys} rekeys")
            print(f"    {rekeys} rekeys during 200 kB each way")
            image.process.poll()

    def case_rekey_while_a_shell_waits(self):
        with self.image() as image:
            shell = self.client.popen(None, self.client.key_options("ed25519") +
                                      ["-o", "RekeyLimit=default 2"], verbose=("-v",))
            check(shell.stdout.readline() == b"terminal=0\n", "no greeting")
            time.sleep(5.0)
            check(shell.poll() is None, "the rekey ended the shell")
            shell.stdin.write(b"still here\n")
            shell.stdin.flush()
            check(shell.stdout.readline() == b"still here\n", "no echo after the rekey")
            output, errors = shell.communicate(timeout=20)
            check(shell.returncode == 0, (shell.returncode, errors[-400:]))
            rekeys = errors.count(b"SSH2_MSG_NEWKEYS sent") - 1
            check(rekeys >= 1, f"{rekeys} rekeys")
            image.process.poll()

    def case_rekey_while_sftp_waits(self):
        with self.image() as image:
            mark = image.mark()
            arguments = self.client.key_options("ed25519") + ["-o", "RekeyLimit=default 2", "-s"]
            sftp = self.client.popen("sftp", arguments, verbose=("-v",))
            sftp.stdin.write(sftp_packet(1, struct.pack(">I", 3)))
            sftp.stdin.flush()
            check(read_sftp_packet(sftp.stdout)[0] == 2, "no SSH_FXP_VERSION")
            time.sleep(5.0)
            check(sftp.poll() is None, "the rekey ended SFTP")
            # A request of an unknown type, which the server answers with a status.
            sftp.stdin.write(sftp_packet(200, struct.pack(">II", 9, 0)))
            sftp.stdin.flush()
            check(read_sftp_packet(sftp.stdout)[0] == 101, "no SSH_FXP_STATUS after the rekey")
            _, errors = sftp.communicate(timeout=20)
            check(errors.count(b"SSH2_MSG_NEWKEYS sent") >= 2, "no rekey")
            image.wait_for(r"^SFTP close", start=mark)

    def case_exit_status_survives_back_pressure(self):
        # Every third send reports that the socket would block, as a slow client makes it do.
        with self.image("send-block=3", "connections=4") as image:
            options = self.client.key_options("ed25519")
            for status in 7, 0, 255, 1, 42:
                result = self.client.run(f"exit {status}", options)
                check(result.returncode == status, (status, result.returncode, result.stderr))
            result = self.client.run("whoami", self.client.password_options())
            check(result.returncode == 0, result.stderr)
            check(result.stdout == b"operator administrator\n", result.stdout)
            result = self.client.run("output 100000", options)
            check(result.returncode == 0, (result.returncode, result.stderr))
            check(len(result.stdout) == 100000, len(result.stdout))
            data = os.urandom(30000)
            result = self.client.run("cat", options, stdin=data)
            check(result.returncode == 0 and result.stdout == data, result.returncode)
            image.process.poll()

    def small_arena_fails_closed(self):
        # Run by itself, on an image built with a small arena: see scripts/test-ssh-provider.sh.
        with self.image("connections=4", "auth-ms=20000") as image:
            options = self.client.key_options("ed25519")
            held = [hold_with_large_packet() for _ in range(3)]
            time.sleep(0.5)
            result = self.client.run("whoami", options)
            check(result.returncode == 255, f"a login fitted: {result.stdout}")
            print(f"    with three clients holding memory, a login got: "
                  f"{result.stderr.decode(errors='replace').strip()[-80:]}")
            for connection in held:
                connection.close()
            time.sleep(0.5)
            result = self.client.run("whoami", options)
            check(result.returncode == 0, f"no recovery: {result.stderr}")

    def case_concurrent_logins_do_not_stall_others(self):
        # A sleep inside the derivation stands for a slow verifier, because CPU work takes no
        # simulated time. The sleep holds its worker only if the provider is right.
        with self.image("connections=4") as image:
            result = self.client.run("derive-delay 3000", self.client.key_options("ed25519"))
            check(result.returncode == 0, result.stderr)
            mark = image.mark()
            slow = self.client.popen("whoami", self.client.password_options())
            image.wait_for(r"^ACCESS derive ", 20.0, mark)
            started = time.monotonic()

            # A key login and its whole session, while that verification runs.
            other = self.client.run("whoami", self.client.key_options("ecdsa"))
            key_elapsed = time.monotonic() - started
            check(other.returncode == 0, other.stderr)
            check(slow.poll() is None, "the slow login ended early")

            # A second password login is refused at once, and does not queue.
            mark_busy = image.mark()
            second = self.client.run("whoami", self.client.password_options())
            check(second.returncode == 255, second.stderr)
            check(image.events("LOGIN_BUSY", mark_busy) == 1, image.snapshot()[mark_busy:])
            busy_elapsed = time.monotonic() - started
            check(slow.poll() is None, "the slow login ended before the others finished: "
                  f"{busy_elapsed:.2f} s")

            output, errors = slow.communicate(timeout=30)
            total = time.monotonic() - started
            check(slow.returncode == 0, errors)
            check(output == b"operator administrator\n", output)
            print(f"    during a 3 s verification: key login and session done at "
                  f"{key_elapsed:.2f} s, busy refusal at {busy_elapsed:.2f} s, slow login at "
                  f"{total:.2f} s")

    def case_clean_close(self):
        with self.image("connections=4") as image:
            options = self.client.key_options("ed25519")
            result = self.client.run("exit 7", options)
            check(result.returncode == 7, (result.returncode, result.stderr))

            # A lost exit status or lost output is what a close that does not wait gives.
            size = 200000
            expected = hashlib.sha256()
            block = bytes((ord("a") + i % 26) for i in range(512))
            remaining = size
            while remaining > 0:
                expected.update(block[:min(remaining, 512)])
                remaining -= 512
            for _ in range(5):
                result = self.client.run(f"output {size}", options)
                check(result.returncode == 0, (result.returncode, result.stderr))
                check(len(result.stdout) == size, len(result.stdout))
                check(hashlib.sha256(result.stdout).digest() == expected.digest(), "bad data")
            for status in 0, 1, 255:
                result = self.client.run(f"exit {status}", options)
                check(result.returncode == status, (status, result.returncode))

            data = os.urandom(50000)
            result = self.client.run("cat", options, stdin=data)
            check(result.returncode == 0, result.stderr)
            check(result.stdout == data, f"{len(result.stdout)} bytes came back")

            result = self.client.run(None, options, stdin=b"typed\n")
            check(result.stdout.startswith(b"terminal=0\n"), result.stdout)
            check(result.stdout.endswith(b"typed\n"), result.stdout)
            check(result.returncode == 0, result.returncode)

            result = self.client.run(None, options + ["-tt"], stdin=b"\x04")
            check(b"terminal=1" in result.stdout, result.stdout)
            image.wait_for(r"^ACCESS event type=LOGOUT ")

    def case_nothing_is_left_behind(self):
        with self.image("connections=4") as image:
            options = self.client.key_options("ed25519")

            def allocated():
                mark = image.mark()
                check(self.client.run("measure", options).returncode == 0, "measure")
                line = image.wait_for(r"^MEASURE ", start=mark)
                return int(re.search(r"heap_allocated=(\d+)", line).group(1)), line

            first, first_line = allocated()
            # Under the five free failures, or the delay would refuse the key login that measures.
            for _ in range(4):
                self.client.run("whoami", options)
                self.client.run("whoami", self.client.password_options(), answers=(WRONG,))
            connection = raw_connect()
            read_version(connection)
            connection.close()
            time.sleep(0.3)
            second, second_line = allocated()
            print(f"    {first_line}\n    {second_line}")
            check(second == first, f"the heap grew from {first} to {second}")

            mark = image.mark()
            # The stop races this session's own close, so its exit status means nothing here.
            self.client.run("stop", options)
            image.wait_for(r"^STOPPED 0", start=mark)
            final = image.wait_for(r"^MEASURE ", start=mark)
            image.wait_for(r"^EXIT 0", start=mark)
            print(f"    after stop: {final}")

    def case_resource_use(self):
        # Simulation results only. Each figure is the image's own, after what the line names.
        with self.image("connections=4") as image:
            def measure(label):
                mark = image.mark()
                check(self.client.run("measure", key).returncode == 0, "measure")
                line = image.wait_for(r"^MEASURE ", start=mark)
                print(f"    {label}: {line}")

            key = self.client.key_options("ed25519")
            measure("first session")
            check(self.client.run("exit 0", self.client.password_options()).returncode == 0, "pw")
            check(self.client.run("exit 0", self.client.key_options("ecdsa")).returncode == 0, "k")
            measure("each login kind")
            check(self.client.run("output 300000", key).returncode == 0, "output")
            check(self.client.run("cat", key, stdin=os.urandom(100000)).returncode == 0, "cat")
            measure("300 kB down, 100 kB up")
            held = [self.client.popen("sleep 2500", key) for _ in range(3)]
            time.sleep(1.5)
            measure("four sessions at once")
            for process in held:
                process.communicate(timeout=30)
            time.sleep(0.2)
            measure("back to none")
            held = hold_with_large_packet()
            time.sleep(0.3)
            measure("one client before keys, with a 34,988 B packet announced")
            held.close()
            time.sleep(0.3)

    def case_forwarding_is_refused(self):
        with self.image() as image:
            options = self.client.key_options("ed25519")
            for flag in (["-L", "9000:127.0.0.1:2222"], ["-R", "9001:127.0.0.1:2222"]):
                result = self.client.run("exit 0", options + flag + ["-o", "ExitOnForwardFailure=yes"])
                text = result.stderr.lower()
                if flag[0] == "-R":
                    check(result.returncode == 255, result.returncode)
                    check(b"forward" in text, text)
            # A local forward is refused when it is used, so use it.
            forward = self.client.popen("sleep 3000", options + ["-L", "9002:127.0.0.1:2222"])
            time.sleep(1.0)
            try:
                probe = socket.create_connection((HOST, 9002), timeout=5)
                probe.settimeout(3)
                check(probe.recv(16) == b"", "a forward carried data")
                probe.close()
            except (ConnectionError, socket.timeout) as error:
                print(f"    forward attempt: {error}")
            _, errors = forward.communicate(timeout=20)
            check(b"administratively prohibited" in errors or b"open failed" in errors, errors)
            image.process.poll()

    def case_sftp_reaches_the_stand_in(self):
        with self.image() as image:
            mark = image.mark()
            command = ["sftp", "-F", "/dev/null", "-P", str(PORT), "-b", "-",
                       "-o", "StrictHostKeyChecking=no", "-o", "UserKnownHostsFile=/dev/null",
                       *self.client.key_options("ed25519"), f"{USER}@{HOST}"]
            result = subprocess.run(command, input=b"ls /\n", capture_output=True, timeout=60)
            image.wait_for(r"^SFTP open", start=mark)
            image.wait_for(r"^SFTP close", start=mark)
            # Every file operation of the refusing port fails, so a client gets no further.
            check(result.returncode != 0, result.stdout)
            print(f"    sftp said: {result.stderr.decode(errors='replace').strip()[:100]}")
            image.wait_for(r"^ACCESS event type=LOGOUT ", start=mark)

    def case_repository_stand_ins_refuse(self):
        with self.image("stand-ins=repository") as image:
            options = self.client.key_options("ed25519")
            mark = image.mark()
            result = self.client.run("whoami", options)
            check(result.returncode == 255, (result.returncode, result.stderr))
            check(result.stdout == b"", result.stdout)
            result = self.client.run(None, options, stdin=b"")
            check(result.returncode == 255, (result.returncode, result.stderr))
            image.wait_for(r"^ACCESS event type=LOGOUT ", start=mark)
            command = ["sftp", "-F", "/dev/null", "-P", str(PORT), "-b", "-",
                       "-o", "StrictHostKeyChecking=no", "-o", "UserKnownHostsFile=/dev/null",
                       *self.client.key_options("ed25519"), f"{USER}@{HOST}"]
            result = subprocess.run(command, input=b"ls /\n", capture_output=True, timeout=60)
            check(result.returncode != 0, result.stdout)


class StoredCases:
    """An image built with CRUX_SSH_TEST_STORED_HOST_KEY: the host key comes from the credential
    store on its PSA ports, on a disk whose host file outlives each boot. Each case boots the image
    several times on one file, as a device restarts."""

    KEY_GENERATED = r"^STORE event=credential\.key_generated record=4 "

    def __init__(self, binary, client, directory):
        self.binary = binary
        self.client = client
        self.directory = Path(directory)
        self.keys = [f"key={client.blob_hex('ed25519')}"]

    def boot(self, flash, *arguments, ready=True, seed=None):
        options = [f"--flash={flash}"] + ([f"-seed={seed}"] if seed is not None else [])
        return Image(self.binary, *self.keys, *arguments, options=options, ready=ready)

    def fresh(self, name):
        flash = self.directory / f"{name}.bin"
        flash.unlink(missing_ok=True)
        return flash

    def scan(self, kind):
        scan = subprocess.run(["ssh-keyscan", "-p", str(PORT), "-t", kind, HOST],
                              capture_output=True, text=True, timeout=30, check=True)
        return subprocess.run(["ssh-keygen", "-lf", "-"], input=scan.stdout, capture_output=True,
                              text=True, check=False).stdout

    def login(self, known_hosts, checking):
        command = ["ssh", "-F", "/dev/null", "-p", str(PORT), "-o", f"StrictHostKeyChecking={checking}",
                   "-o", f"UserKnownHostsFile={known_hosts}", "-o", "ConnectTimeout=20",
                   *self.client.key_options("ed25519"), f"{USER}@{HOST}", "whoami"]
        return subprocess.run(command, capture_output=True, timeout=60, check=False)

    def case_generated_once_then_kept(self):
        flash = self.fresh("ed25519")
        known_hosts = self.directory / "known_hosts"
        known_hosts.unlink(missing_ok=True)
        with self.boot(flash, "format") as image:
            first = image.wait_for(r"^HOST-KEY SHA256:").split()[1]
            check(image.count(self.KEY_GENERATED) == 1, image.snapshot())
            seen = self.scan("ed25519")
            check(first in seen, f"printed {first}, OpenSSH saw {seen}")
            result = self.login(known_hosts, "accept-new")
            check(result.returncode == 0, result.stderr)
        check(known_hosts.read_text().count("ssh-ed25519") == 1, known_hosts.read_text())
        # A restart on the same disk: the same key, no generation, and a strict client trusts it.
        for _ in range(2):
            with self.boot(flash) as image:
                again = image.wait_for(r"^HOST-KEY SHA256:").split()[1]
                check(again == first, f"first boot {first}, later boot {again}")
                check(image.count(self.KEY_GENERATED) == 0, image.snapshot())
                result = self.login(known_hosts, "yes")
                check(result.returncode == 0, result.stderr)
                check(result.stdout == b"operator administrator\n", result.stdout)
        # Asking for another type does not replace the stored key.
        with self.boot(flash, "host=ecdsa") as image:
            check(image.wait_for(r"^HOST-KEY SHA256:").split()[1] == first, image.snapshot())
            check(image.count(self.KEY_GENERATED) == 0, image.snapshot())
            check(first in self.scan("ed25519"), "the stored Ed25519 key was not offered")
        # Control: a blank disk gives another key, which the strict client refuses. So the checks
        # above would see a key generated again at boot. The test generator repeats its numbers for
        # one seed, so every blank disk would get the same key without another seed.
        with self.boot(self.fresh("other"), "format", seed=192) as image:
            other = image.wait_for(r"^HOST-KEY SHA256:").split()[1]
            check(other != first, f"two blank disks gave one key, {first}")
            result = self.login(known_hosts, "yes")
            check(result.returncode == 255, (result.returncode, result.stderr))
            check(b"HOST IDENTIFICATION HAS CHANGED" in result.stderr, result.stderr)

    def case_ecdsa_key_is_stored_too(self):
        flash = self.fresh("ecdsa")
        with self.boot(flash, "format", "host=ecdsa") as image:
            first = image.wait_for(r"^HOST-KEY SHA256:").split()[1]
            check(image.count(self.KEY_GENERATED) == 1, image.snapshot())
            check(first in self.scan("ecdsa"), "OpenSSH saw another ECDSA key")
        with self.boot(flash) as image:
            check(image.wait_for(r"^HOST-KEY SHA256:").split()[1] == first, image.snapshot())
            check(image.count(self.KEY_GENERATED) == 0, image.snapshot())
            check(first in self.scan("ecdsa"), "OpenSSH saw another ECDSA key after a restart")

    def case_no_store_means_no_server(self):
        flash = self.fresh("unformatted")
        with self.boot(flash, ready=False) as image:
            image.wait_for(r"^EXIT 2$", SERVER_TIMEOUT)
            check(image.count(r"^STORE medium not ready") == 1, image.snapshot())
            check(image.count(r"^HOST-KEY unavailable") == 1, image.snapshot())
            check(image.count(r"^READY ") == 0, image.snapshot())
            check(image.count(self.KEY_GENERATED) == 0, image.snapshot())
        with socket.socket() as probe:
            probe.settimeout(2)
            check(probe.connect_ex((HOST, PORT)) != 0, "something listens with no host key")


def run_cases(cases, only):
    failed = []
    names = [name for name in dir(cases) if name.startswith("case_")]
    names.sort(key=lambda name: getattr(type(cases), name).__code__.co_firstlineno)
    for name in names:
        short = name[len("case_"):]
        if only and short not in only:
            continue
        started = time.monotonic()
        try:
            getattr(cases, name)()
            print(f"PASS {short} ({time.monotonic() - started:.1f} s)", flush=True)
        except Exception as error:  # A case's failure must not stop the others.
            failed.append(short)
            print(f"FAIL {short}: {error}", flush=True)
    return failed


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("--only", action="append", default=[])
    parser.add_argument("--small-arena", action="store_true")
    parser.add_argument("--stored-host-key", action="store_true",
                        help="the image was built with CRUX_SSH_TEST_STORED_HOST_KEY")
    arguments = parser.parse_args()
    # A hang must end the run and say where it was, since one run stopped silently under emulation.
    faulthandler.dump_traceback_later(420, exit=True)
    version = subprocess.run(["ssh", "-V"], capture_output=True, text=True, timeout=60).stderr.strip()
    print(f"Client: {version}")
    failed = []
    with tempfile.TemporaryDirectory() as directory:
        if arguments.stored_host_key:
            stored = StoredCases(arguments.binary.resolve(), Client(directory), directory)
            failed = run_cases(stored, arguments.only)
            print(f"{len(failed)} failed: {failed}" if failed else "All cases passed.")
            return 1 if failed else 0
        cases = Cases(arguments.binary.resolve(), Client(directory))
        if arguments.small_arena:
            cases.small_arena_fails_closed()
            print("All cases passed.")
            return 0
        failed = run_cases(cases, arguments.only)
    print(f"{len(failed)} failed: {failed}" if failed else "All cases passed.")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
