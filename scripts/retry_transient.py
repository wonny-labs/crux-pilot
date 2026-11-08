#!/usr/bin/env python3
"""Retry a download a few times when GitHub Releases or the network fails for a moment.

On 8 November 2026 GitHub Releases answered HTTP 504 for hours, and every job that downloaded
the Zephyr SDK or the pinned lychee binary failed with no problem in the content. A short, bounded
retry covers a blip. It does not hide an outage: after the last attempt the failure is reported
as it was, and the job fails.

Only transient failures are repeated: HTTP 5xx, a network error or a timeout. A 4xx answer, a
checksum mismatch and every other error end the run at once, so verification is never weakened.

As a module:   retry.retry(operation)           # operation() raises on failure
As a program:  retry_transient.py [--attempts N] [--base-delay S] [--max-delay S] -- COMMAND...

The program form is for commands that give no structured error, such as `west sdk install`. It
repeats the command only when the exit status is not zero and the output shows a transient
failure, and it never repeats after output that shows a checksum mismatch.
"""
import argparse
import collections
import http.client
import re
import subprocess
import sys
import threading
import time
import urllib.error

ATTEMPTS = 4      # Waits of 10, 20 and 40 seconds between four attempts.
BASE_DELAY = 10   # Seconds before the second attempt; doubled for each later one.
MAX_DELAY = 60    # No single wait is longer than this.

# What a failed command printed. A line is transient when it names a 5xx answer or a network
# failure. West prints `Failed to download <url>: 504` and `Failed to fetch: 502, <body>`. The SDK's
# setup.sh downloads the toolchain with `wget -q`, which hides the status; it prints only
# `ERROR: GNU toolchain download failed`, and removes the partial file before it exits.
TRANSIENT_OUTPUT = re.compile(
    r"(Failed to (download|fetch)\b[^\n]*?[:,] 5\d\d\b"
    r"|toolchain download failed"
    r"|HTTP(?: Error)?[ :]+5\d\d\b"
    r"|\b5\d\d (Server Error|Bad Gateway|Service Unavailable|Gateway Time-?out)"
    r"|ConnectionError|ConnectionResetError|ReadTimeout|ConnectTimeout|Connection (reset|aborted|timed out)"
    r"|Temporary failure in name resolution|Max retries exceeded|IncompleteRead|Read timed out)",
    re.IGNORECASE)
# Evidence that the content is wrong. It overrides everything above.
NEVER_RETRY_OUTPUT = re.compile(r"sha256 mismatch|checksum mismatch|SHA-256 mismatch", re.IGNORECASE)

TAIL_LINES = 400  # How much of a command's output is kept for classification.


def is_transient(error):
    """True for an HTTP 5xx, a dropped or timed-out connection and a short read."""
    if isinstance(error, urllib.error.HTTPError):
        return 500 <= error.code <= 599
    return isinstance(error, (urllib.error.URLError, TimeoutError, ConnectionError,
                              http.client.IncompleteRead))


def delays(attempts, base_delay, max_delay):
    return [min(base_delay * 2 ** n, max_delay) for n in range(attempts - 1)]


def retry(operation, attempts=ATTEMPTS, base_delay=BASE_DELAY, max_delay=MAX_DELAY,
          sleep=None, log=lambda text: print(text, end="", file=sys.stderr, flush=True)):
    """Return operation(), calling it up to `attempts` times while it raises a transient error."""
    sleep = sleep or time.sleep
    waits = delays(attempts, base_delay, max_delay)
    for attempt in range(1, attempts + 1):
        try:
            return operation()
        except Exception as error:
            if not is_transient(error):
                log(f"retry: attempt {attempt}/{attempts} failed, not a transient failure: {error!r}\n")
                raise
            if attempt == attempts:
                log(f"retry: attempt {attempt}/{attempts} failed: {error!r}\n"
                    f"retry: giving up after {attempts} attempts\n")
                raise
            wait = waits[attempt - 1]
            log(f"retry: attempt {attempt}/{attempts} failed: {error!r}; retrying in {wait} s\n")
            sleep(wait)


def run_command(command):
    """Run `command`, pass each output stream through unchanged, and return
    (exit status, last lines of output)."""
    tail = collections.deque(maxlen=TAIL_LINES)

    def copy(source, sink):
        for line in source:
            sink.write(line)
            sink.flush()
            tail.append(line)

    with subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          text=True, errors="replace") as process:
        stderr_copy = threading.Thread(target=copy, args=(process.stderr, sys.stderr))
        stderr_copy.start()
        copy(process.stdout, sys.stdout)
        stderr_copy.join()
    return process.returncode, "".join(tail)


def retry_command(command, attempts=ATTEMPTS, base_delay=BASE_DELAY, max_delay=MAX_DELAY,
                  sleep=None):
    sleep = sleep or time.sleep
    waits = delays(attempts, base_delay, max_delay)
    for attempt in range(1, attempts + 1):
        try:
            status, output = run_command(command)
        except OSError as error:
            print(f"retry: cannot run {command[0]}: {error}", file=sys.stderr)
            return 127
        if status == 0:
            return 0
        if NEVER_RETRY_OUTPUT.search(output) or not TRANSIENT_OUTPUT.search(output):
            print(f"retry: attempt {attempt}/{attempts} exited {status}, not a transient failure",
                  file=sys.stderr)
            return status
        if attempt == attempts:
            print(f"retry: attempt {attempt}/{attempts} exited {status} after a transient failure\n"
                  f"retry: giving up after {attempts} attempts", file=sys.stderr)
            return status
        wait = waits[attempt - 1]
        print(f"retry: attempt {attempt}/{attempts} failed with exit status {status} after a "
              f"transient failure; retrying in {wait} s", file=sys.stderr)
        sleep(wait)
    return 1  # Not reached: the last attempt returns above.


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--attempts", type=int, default=ATTEMPTS)
    parser.add_argument("--base-delay", type=float, default=BASE_DELAY)
    parser.add_argument("--max-delay", type=float, default=MAX_DELAY)
    parser.add_argument("command", nargs=argparse.REMAINDER, help="-- followed by the command")
    args = parser.parse_args(argv)
    command = args.command[1:] if args.command[:1] == ["--"] else args.command
    if not command or args.attempts < 1:
        parser.error("give a command after --, and at least one attempt")
    return retry_command(command, args.attempts, args.base_delay, args.max_delay)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
