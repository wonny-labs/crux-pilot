#!/usr/bin/env python3
"""The command line against a real OpenSSH client, on native_sim over the native socket offload.

The image holds the real entry points of #193, the real CLI, the real access core and the provider of #192.
Its Access ports and host key are #192's test ones, and its commands are test commands. It borrows the image
and client helpers of #192's OpenSSH test. Each case boots the image. These are native_sim results only.
"""
import argparse
import importlib.util
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location(
    "provider_openssh_test", HERE / "../../../zephyr/ssh_provider/openssh_test.py")
provider_test = importlib.util.module_from_spec(spec)
spec.loader.exec_module(provider_test)
Image = provider_test.Image
Client = provider_test.Client
PASSWORD = provider_test.PASSWORD
ADMIN = provider_test.USER
VIEWER = "viewer"
NEW_PASSWORD = "a different long password"
LINE_MAX = 400
FLOOD_BYTES = 3 * 1024 * 1024

# A sample through exec, where each line costs a login. The shell case below runs the whole list.
ZEPHYR_SHELL_LINES = ("kernel", "kernel thread list", "kernel reboot cold", "kernel uptime", "kernel panic",
                      "device list", "devmem 0x0", "shell colors off", "history", "resize", "select", "retval",
                      "clear", "rem", "app", "zyclictest", "date get", "log status", "net iface", "gpio get",
                      "i2c scan", "flash read", "fs ls /")
ALL_ZEPHYR_SHELL_LINES = tuple(re.findall(r'^    "(.*)",$', (HERE / "../zephyr_shell_lines.h").read_text(), re.M))


def check(condition, message):
    if not condition:
        raise AssertionError(message)


def expect(result, stdout, status, label=""):
    check(result.stdout == stdout, f"{label} stdout {result.stdout!r}, expected {stdout!r}, {result.stderr!r}")
    check(result.returncode == status, f"{label} status {result.returncode}, expected {status}, {result.stderr!r}")


class Cases:
    def __init__(self, binary, directory):
        self.binary = binary
        self.client = Client(directory)
        self.password = self.client.password_options()

    def image(self, *arguments):
        return Image(self.binary, *arguments)

    def exec(self, command, user=ADMIN, password=PASSWORD, extra=()):
        return self.client.run(command, self.password + list(extra), answers=(password,), user=user)

    def shell(self, stdin, terminal, user=ADMIN, password=PASSWORD, timeout=60):
        options = self.password + (["-tt"] if terminal else ["-T"])
        return self.client.run(None, options, answers=(password,), user=user, stdin=stdin, timeout=timeout)

    def case_exec_status_and_output(self):
        with self.image():
            for command, stdout, status in (
                    ("probe-read", b"ran\n", 0),
                    ("probe-admin", b"ran\n", 0),
                    ("probe-fail", b"command failed\n", 1),
                    ("any a b", b"usage: any: any session\n", 2),
                    ("passwd", b"not available for exec\n", 2),
                    ("\"unterminated", b"invalid line\n", 2),
                    ("no-such", b"unknown command\n", 127),
                    ("!setup", b"unknown command\n", 127),
                    ("serial-only", b"unknown command\n", 127),
                    ("logout", b"", 0)):
                expect(self.exec(command), stdout, status, command)

    def case_exec_follows_the_permission(self):
        with self.image() as image:
            expect(self.exec("probe-read", user=VIEWER), b"ran\n", 0, "viewer")
            mark = image.mark()
            expect(self.exec("probe-admin", user=VIEWER), b"unknown command\n", 127, "viewer")
            # Access reports the denial itself, and the CLI adds no event of its own.
            image.wait_for(r"^ACCESS event type=OTHER result=-\d+ channel=3 ", start=mark)
            check(image.count(r"^CLI event ", mark) == 0, "a CLI event on top of Access's")
            help_text = self.exec("help", user=VIEWER).stdout
            check(b"probe-read" in help_text and b"probe-admin" not in help_text, help_text)
            check(b"probe-admin" in self.exec("help").stdout, "the administrator's help")
            expect(self.exec("help probe-admin", user=VIEWER), b"unknown command\n", 127, "help")

    def case_shell_with_no_terminal(self):
        with self.image():
            # No echo and no CR, and a logout is the clean end.
            expect(self.shell(b"probe-read\nno-such\nlogout\n", False),
                   b"> ran\n> unknown command\n> ", 0)
            # Without one, the client's EOF is a transport end.
            expect(self.shell(b"probe-read\n", False), b"> ran\n> ", 1)

    def case_shell_with_a_terminal(self):
        with self.image():
            # Echo, and CR LF for each LF. A terminal sends CR for Enter.
            expect(self.shell(b"probe-read\rno-such\rlogout\r", True),
                   b"> probe-read\r\nran\r\n> no-such\r\nunknown command\r\n> logout\r\n", 0)
            # The line editor, as on serial: DEL, Ctrl-U and an arrow key.
            expect(self.shell(b"probe-readd\x7f\rgarbage\x15logout\r", True),
                   b"> probe-readd\b \b\r\nran\r\n> garbage\b \b\b \b\b \b\b \b\b \b\b \b\b \blogout\r\n", 0)
            expect(self.shell(b"\x1b[Alogout\r", True), b"> logout\r\n", 0)

    def case_exec_with_a_terminal_has_no_line_discipline(self):
        # A limit that docs/cli-over-ssh.md declares: the exec callback is not told about a terminal.
        with self.image():
            result = self.exec("probe-read", extra=["-tt"])
            check(result.returncode == 0, result)
            check(result.stdout == b"ran\n", result.stdout)

    def case_the_longest_line(self):
        # The image raises the limit to 400, so this overruns if the entry points and the core disagree.
        with self.image():
            longest = "any " + "x" * (LINE_MAX - 4)
            expect(self.exec(longest), b"ran\n", 0, "at the limit")
            expect(self.exec(longest + "x"), b"line too long\n", 2, "over the limit")
            expect(self.shell(longest.encode() + b"\nlogout\n", False), b"> ran\n> ", 0)
            expect(self.shell(longest.encode() + b"x\nlogout\n", False), b"> line too long\n> ", 0)

    def case_password_change(self):
        with self.image():
            typed = f"passwd\r{PASSWORD}\r{NEW_PASSWORD}\r{NEW_PASSWORD}\rlogout\r".encode()
            result = self.shell(typed, True, user=VIEWER)
            expect(result, b"> passwd\r\ncurrent password: \r\nnew password: \r\nrepeat: \r\n"
                           b"password changed\r\n> logout\r\n", 0)
            expect(self.exec("probe-read", user=VIEWER, password=NEW_PASSWORD), b"ran\n", 0, "new password")
            refused = self.exec("probe-read", user=VIEWER)
            check(refused.returncode == 255 and refused.stdout == b"", refused)

    def case_forced_change(self):
        with self.image("forced-change"):
            expect(self.exec("probe-read", user=VIEWER), b"unknown command\n", 127, "held back")
            typed = f"probe-read\rpasswd\r{PASSWORD}\r{NEW_PASSWORD}\r{NEW_PASSWORD}\rprobe-read\rlogout\r".encode()
            expect(self.shell(typed, True, user=VIEWER),
                   b"password change required, use passwd\r\n"
                   b"> probe-read\r\nunknown command\r\n"
                   b"> passwd\r\ncurrent password: \r\nnew password: \r\nrepeat: \r\npassword changed\r\n"
                   b"> probe-read\r\nran\r\n> logout\r\n", 0)

    def case_the_zephyr_shell_is_not_reachable(self):
        with self.image() as image:
            for line in ZEPHYR_SHELL_LINES:
                expect(self.exec(line), b"unknown command\n", 127, line)
            # One shell for the whole generated list. A help line answers for itself, with no event.
            typed = "".join(f"{line}\r" for line in ALL_ZEPHYR_SHELL_LINES) + "logout\r"
            result = self.shell(typed.encode(), True, timeout=180)
            check(result.returncode == 0, result)
            check(b"uart:~$\r\n" not in result.stdout.replace(b"> uart:~$\r\n", b""), result.stdout)
            check(result.stdout.count(b"unknown command\r\n") == len(ALL_ZEPHYR_SHELL_LINES), result.stdout)
            events = sum(not line.startswith("help ") for line in ALL_ZEPHYR_SHELL_LINES)
            check(image.count(r"^CLI event type=0 outcome=0 channel=3 user=operator$") ==
                  len(ZEPHYR_SHELL_LINES) + events, "one event for each rejected line")
            # Tab completes nothing.
            expect(self.shell(b"ker\t\rlogout\r", True), b"> ker\r\nunknown command\r\n> logout\r\n", 0)

    def case_a_slow_client_holds_only_itself(self):
        # OpenSSH holds up to its window for a stdout that nobody reads, and the rest waits in the server.
        with self.image() as image:
            stuck = self.client.popen(f"flood {FLOOD_BYTES}", self.password)
            image.wait_for(r"^CLI write timeout$", 60)
            for _ in range(3):
                start = time.monotonic()
                expect(self.exec("probe-read"), b"ran\n", 0, "beside a stuck client")
                check(time.monotonic() - start < 30, "a session took too long beside a stuck client")
                expect(self.shell(b"probe-read\nlogout\n", False), b"> ran\n> ", 0, "beside a stuck client")
                check(stuck.poll() is None, "the stuck client ended early")
            # It catches up, and has lost nothing.
            output, _ = stuck.communicate(timeout=180)
            check(stuck.returncode == 0, stuck.returncode)
            check(len(output) == FLOOD_BYTES, f"{len(output)} bytes of {FLOOD_BYTES}")

    def case_a_stuck_client_ends_at_the_idle_limit(self):
        with self.image("idle=6", "connections=1") as image:
            mark = image.mark()
            stuck = self.client.popen(f"flood {FLOOD_BYTES}", self.password)
            image.wait_for(r"^CLI write timeout$", 60, start=mark)
            image.wait_for(r"^CLI channel close$", 60, start=mark)
            # The provider lets it drain, then frees the slot for the next client.
            image.wait_for(r"^SSH worker ended", 60, start=mark)
            result = self.exec("probe-read")
            for _ in range(10):
                if b"too many connections" not in result.stderr:
                    break
                time.sleep(1)
                result = self.exec("probe-read")
            expect(result, b"ran\n", 0, "after the stuck client")
            output, _ = stuck.communicate(timeout=30)
            check(len(output) < FLOOD_BYTES, "the stuck client received everything")

    def case_an_idle_shell_ends_at_the_idle_limit(self):
        with self.image("idle=5"):
            start = time.monotonic()
            process = self.client.popen(None, self.password + ["-T"])
            output = process.stdout.read()
            process.wait(timeout=30)
            check(output == b"> session ended\n", output)
            check(process.returncode == 1, process.returncode)
            check(4 <= time.monotonic() - start < 40, "not at the limit")
            process.stdin.close()

    def case_a_rekey_during_a_session(self):
        # The client asks for a new key every few KiB, which reaches read and write in the provider.
        rekey = ["-o", "RekeyLimit=16K"]
        with self.image():
            result = self.client.run("flood 262144", self.password + rekey)
            check(result.returncode == 0, result.stderr)
            check(len(result.stdout) == 262144, f"{len(result.stdout)} bytes")
            lines = b"probe-read\n" * 2000 + b"logout\n"
            result = self.client.run(None, self.password + rekey + ["-T"], stdin=lines, timeout=180)
            check(result.returncode == 0, result.stderr)
            check(result.stdout.count(b"ran\n") == 2000, result.stdout[-200:])

    def case_resource_use(self):
        # Simulation results only, on a 64-bit image.
        with self.image() as image:
            typed = f"account list\rpasswd\r{PASSWORD}\r{NEW_PASSWORD}\r{NEW_PASSWORD}\rlogout\r".encode()
            self.shell(typed, True)
            self.exec("flood 65536", password=NEW_PASSWORD)
            expect(self.exec("measure", password=NEW_PASSWORD), b"", 0)
            print(f"    {image.wait_for(r'^MEASURE ')}")
            # A stop ends this connection too, so its own exit status may not arrive.
            self.exec("stop", password=NEW_PASSWORD)
            image.wait_for(r"^STOPPED 0$", 30)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("--only", action="append", default=[])
    arguments = parser.parse_args()
    failures = []
    with tempfile.TemporaryDirectory() as directory:
        cases = Cases(arguments.binary.resolve(), directory)
        for name in sorted(n for n in dir(Cases) if n.startswith("case_")):
            short = name.removeprefix("case_")
            if arguments.only and short not in arguments.only:
                continue
            try:
                getattr(cases, name)()
                print(f"PASS {short}", flush=True)
            except Exception as error:  # A failed case must not hide the next one.
                failures.append(short)
                print(f"FAIL {short}: {error}", flush=True)
    if failures:
        print(f"{len(failures)} cases failed: {', '.join(failures)}")
        return 1
    print("All cases passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
