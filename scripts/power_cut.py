#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""One power cut with a supply check: the --cut-cmd of the power-cut host scripts.

It runs the off command, waits the planned pause, runs the read command at the end of that wait
and exits non-zero unless the target voltage it read is at or below a limit. It switches nothing on
and starts no command of its own: the off command and the voltage reader are given on the command
line, so the wrapper is the same for a USB hub port, a relay or a bench supply, and which reader
is used is a decision of the board session, not of this file.

The pause counts from the return of the off command. The wrapper's own run time is the off
command's time, plus the pause, plus the read latency. The log records all three, so the
host script's off time is checked against what happened and not assumed.

Reader contract: the last non-empty line of its standard output is a whole number of millivolts,
optionally followed by "mV" (for example "12" or "12 mV"). Anything else, a non-zero exit status
or a run longer than --read-timeout is a failed reading, never a low one.

Both commands run without a shell (shlex.split, no pipes, redirection or "~"). A host script
runs this wrapper through its own shell, so the wrapper's options need nested quoting there.

Exit status: 0 power removed (reading at or below --max-mv); 1 power still present, and only
that; 2 usage (argparse), before any command has run; 3 the off command failed, timed out or
could not run; 4 the reading failed; 5 the log could not be written; 6 an unexpected error after
the off command had started. Only 0 is a cut that the host script may judge.

Log: one JSON object per cut, appended to --log, for every outcome that reaches the logging. The
one-line summary goes to standard error, so that the host script's standard output stays its own.
"""
import argparse
import datetime
import json
import math
import re
import shlex
import subprocess
import sys
import time

OFF_TIMEOUT_S = 15.0
MAX_SECONDS = 60.0
READING = re.compile(r"^(\d+)\s*(?:mV)?$")


def utc_now():
    return datetime.datetime.now(datetime.timezone.utc).isoformat(timespec="milliseconds")


def finite_seconds(text):
    """A finite number of seconds, at most MAX_SECONDS (nan, inf and 1e300 are refused)."""
    value = float(text)
    if not math.isfinite(value) or value > MAX_SECONDS:
        raise argparse.ArgumentTypeError(f"must be a finite number of at most {MAX_SECONDS:g}")
    return value


def positive_number(text):
    value = finite_seconds(text)
    if value <= 0:
        raise argparse.ArgumentTypeError("must be greater than 0")
    return value


def non_negative_number(text):
    value = finite_seconds(text)
    if value < 0:
        raise argparse.ArgumentTypeError("must not be negative")
    return value


def non_negative_int(text):
    value = int(text)
    if value < 0:
        raise argparse.ArgumentTypeError("must not be negative")
    return value


def command_line(text):
    """A command line that shlex can split into at least one word."""
    try:
        words = shlex.split(text)
    except ValueError as error:
        raise argparse.ArgumentTypeError(f"cannot be parsed: {error}") from error
    if not words:
        raise argparse.ArgumentTypeError("must not be empty")
    return text


def parse_reading(output):
    """Whole millivolts from the last non-empty line of the reader's output, or None."""
    lines = [entry.strip() for entry in output.splitlines() if entry.strip()]
    if not lines:
        return None
    found = READING.match(lines[-1])
    return int(found.group(1)) if found else None


def run(command, timeout):
    """(returncode, stdout) of a command line without a shell; returncode None if it could not
    run or did not finish in time."""
    try:
        done = subprocess.run(shlex.split(command), capture_output=True, text=True, timeout=timeout)
    except Exception:  # any failure to run or finish is a command that did not succeed
        return None, ""
    return done.returncode, done.stdout


def write_log(path, record):
    try:
        with open(path, "a", encoding="utf-8") as handle:
            handle.write(json.dumps(record, sort_keys=True) + "\n")
    except OSError as error:
        print(f"power_cut: cannot write the log: {error}", file=sys.stderr)
        return False
    return True


def build_parser():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--off-cmd", required=True, type=command_line,
                        help="command line that switches the port off")
    parser.add_argument("--read-cmd", required=True, type=command_line,
                        help="command line that prints the voltage in millivolts")
    parser.add_argument("--pause", required=True, type=non_negative_number,
                        help="seconds from the return of the off command to the reading")
    parser.add_argument("--max-mv", required=True, type=non_negative_int,
                        help="highest reading, in millivolts, that counts as power removed")
    parser.add_argument("--read-timeout", required=True, type=positive_number,
                        help="seconds the reader may run before the reading is a failure")
    parser.add_argument("--log", required=True, help="file that receives one JSON line per cut")
    return parser


def cut(args, record):
    """Run the cut and fill the record; returns the exit status before the log is written."""
    record["off_start_utc"] = utc_now()
    record["off_start_monotonic"] = time.monotonic()
    off_rc, _ = run(args.off_cmd, OFF_TIMEOUT_S)
    record["off_returned_monotonic"] = time.monotonic()
    record["off_returned_utc"] = utc_now()
    record["off_rc"] = off_rc
    if off_rc != 0:
        record["verdict"] = "off_failed"
        return 3
    remaining = record["off_returned_monotonic"] + args.pause - time.monotonic()
    if remaining > 0:
        time.sleep(remaining)
    record["read_start_utc"] = utc_now()
    record["read_start_monotonic"] = time.monotonic()
    read_rc, output = run(args.read_cmd, args.read_timeout)
    record["read_end_monotonic"] = time.monotonic()
    record["read_end_utc"] = utc_now()
    record["read_latency_s"] = record["read_end_monotonic"] - record["read_start_monotonic"]
    record["read_rc"] = read_rc
    mv = parse_reading(output) if read_rc == 0 else None
    record["mv"] = mv
    if mv is None:
        record["verdict"] = "read_failed"
        return 4
    if mv > args.max_mv:
        record["verdict"] = "power_present"
        return 1
    record["verdict"] = "power_removed"
    return 0


def main(argv=None):
    args = build_parser().parse_args(argv)
    record = {"pause_s": args.pause, "max_mv": args.max_mv, "mv": None,
              "off_cmd": args.off_cmd, "read_cmd": args.read_cmd}
    try:
        status = cut(args, record)
    except Exception as error:  # after the off command started, so say so and never exit 0 or 1
        record["verdict"] = "wrapper_error"
        record["error"] = f"{type(error).__name__}: {error}"
        status = 6
    logged = write_log(args.log, record)
    print(f"power_cut: {record['verdict']} mv={record['mv']} max_mv={args.max_mv} "
          f"pause_s={args.pause}", file=sys.stderr)
    return status if logged else 5


if __name__ == "__main__":
    sys.exit(main())
