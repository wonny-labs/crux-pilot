#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Rehearse the Reliance Edge Stage A images (samples/storage_reliance_sd) on native_sim.

THE LIMIT OF THIS RUN. The images run here on the eMMC simulator of tests/support/emmc_sim, a model
of one disk that the unmodified MMC stack drives. The rehearsal exercises the application logic of
the five images, the File provider and Reliance Edge on that model. It does not exercise the SD host
driver, the SD layer or a real card, and the RT1060 build (the microSD disk driver, a different
stack) is not what runs here. A pass says the logic and the file system behave as intended on the
model. It says nothing about the EVKB, a card or power loss on either.

Every scenario runs on one simulator image file, which outlives each process, and on two
profiles of the simulator: no cache, and a 64 KiB volatile cache that is lost when the process
ends. The images are built with the corrected cache flush, so a sync reaches the file.

  format   a blank medium: `reliance_format: 0`; a second run: `reliance_format: 2` (volume present)
           with the medium digest unchanged
  start    the mount check on the formatted medium: `emmc_storage_start: 0` and no write reaches the
           simulator; on a blank medium: status 2 (mount failed), no write, digest unchanged; the
           default image prints nothing and writes nothing
  append   the interrupted-write image, killed with SIGKILL after enough ACK lines and restarted:
           every RECOVER report holds at least the last ACK seen and no damaged record
  replace  the same with CLASS lines: after the first generation cfg is never none, never damaged
           and never older than the last GEN printed; leftover temporaries are counted

A SIGKILL ends the process and keeps the file, which drops the simulated cache and every buffer in
the process. That is the nearest this host run gets to an interruption. It is not a power cut.
"""
import argparse
import hashlib
import os
import random
import re
import select
import signal
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", "..", ".."))
for directory in ("tests/native/emmc_restart", "samples/storage_sd_interrupt/host",
                  "samples/storage_sd_replace/host"):
    sys.path.insert(0, os.path.join(ROOT, directory))
import interrupt_capture as APPEND  # noqa: E402  (its line parser and judging function)
import replace_capture as REPLACE  # noqa: E402
from run_restart import MEDIUM_SIZE, TRACE_WRITE, read_trace  # noqa: E402

PROFILES = (("nocache", "--emmc-cache-kb=0"), ("cache64k", "--emmc-cache-kb=64"))
# Seconds of simulated time that an image that returns from main stays up. Time is not paced to the
# wall clock in these builds.
IDLE_STOP = "--stop_at=2"
FORMAT_STATUS_OK, FORMAT_STATUS_PRESENT = 0, 2
START_READY, START_MOUNT_FAILED = 0, 2


def expect(condition, message):
    if not condition:
        raise AssertionError(message)


# ---- judging helpers (unit-tested by test_rehearsal.py) -----------------------------------------

def feed(lines, module, event):
    """Run console lines through a host script's own parser and collect what it pairs and judges."""
    state, entries = {}, []
    for text in lines:
        if module.parse_line(text, state) == event and not state.get("judged"):
            state["judged"] = True
            entry = module.pair(state)
            if entry is not None:
                entries.append(entry)
    return entries


def judge_append(lines, restarts):
    """Accept only if `restarts` reports were paired with the run before them and none lost an ACK."""
    entries = feed(lines, APPEND, "recover")
    judged = [entry for entry in entries if "acked_lost" in entry]
    expect(len(judged) == restarts,
           f"{len(judged)} restarts judged, expected {restarts}: {entries}")
    for entry in judged:
        expect(entry["acked_lost"] == 0,
               f"an acknowledged record was lost: last ACK {entry['last_ack']}, recovered "
               f"{entry['last_valid']} (boot {entry['boot']})")
        expect(not entry["damaged_record"], f"a damaged record after a restart: {entry}")
    return {"judged": len(judged),
            "with_torn_tail": sum(1 for entry in judged if entry["torn_bytes"] > 0),
            "with_unacked_present": sum(1 for entry in judged if entry["unacked_present"] > 0)}


def judge_replace(lines, restarts):
    """Accept only if `restarts` reports were judged and cfg held the last generation each time."""
    entries = feed(lines, REPLACE, "class")
    judged = [entry for entry in entries if entry["outcome"] != "previous_run_not_observed"]
    expect(len(judged) == restarts,
           f"{len(judged)} restarts judged, expected {restarts}: {entries}")
    for entry in judged:
        expect(entry["last_gen"] >= 1, f"no generation was printed before the kill: {entry}")
        expect(entry["outcome"] == "ok",
               f"cfg after a restart is {entry['cfg']} at generation {entry['cfg_gen']} (outcome "
               f"{entry['outcome']}, last GEN printed {entry['last_gen']}, cmp {entry['cmp']})")
    return {"judged": len(judged),
            "cmp_valid": sum(1 for entry in judged if entry["cmp"] == "valid"),
            "cmp_bad": sum(1 for entry in judged if entry["cmp"] == "bad")}


def observe(state, kind, text):
    """Parse one console line of the run being watched and count it if it is this run's own ACK
    (append) or GEN (replace) line. The counter is of events printed by this process: the numbers
    in the lines cannot stand for it, because a replace run starts at the generation after the
    highest one on the medium. A corrupt line is not counted."""
    event = (APPEND if kind == "append" else REPLACE).parse_line(text, state)
    if event == ("ack" if kind == "append" else "gen"):
        state["events"] = state.get("events", 0) + 1
    return event


def progress(state):
    """The number of ACK or GEN lines that the watched run has printed so far."""
    return state.get("events", 0)


def status_line(text, name):
    """The integer of a `<name>: <n>` console line, or None for any other line."""
    match = re.fullmatch(re.escape(name) + r": (-?\d+)", text.strip())
    return int(match.group(1)) if match else None


def count_writes(records):
    """Write requests in a simulator trace (read_trace records)."""
    return sum(1 for record in records if record[0] == TRACE_WRITE)


# ---- running the images -------------------------------------------------------------------------

def blank_medium(image):
    with open(image, "wb") as handle:
        handle.write(bytes(MEDIUM_SIZE))
    if os.path.exists(image + ".trace"):
        os.remove(image + ".trace")


def digest(image):
    with open(image, "rb") as handle:
        return hashlib.sha256(handle.read()).hexdigest()


def command(exe, image, profile):
    return [exe, f"--emmc-image={image}", profile, f"--emmc-trace={image}.trace"]


def run_idle(exe, image, profile):
    """Run an image that returns from main to the end of its simulated time.

    Returns (console lines, simulator write requests)."""
    proc = subprocess.run(command(exe, image, profile) + [IDLE_STOP], capture_output=True,
                          text=True, timeout=120)
    expect(proc.returncode == 0, f"{os.path.basename(exe)} exited {proc.returncode}: "
                                 f"{proc.stdout} {proc.stderr}")
    # native_sim prints a blank line and its own "Stopped at" line when --stop_at ends the run.
    lines = [text for text in proc.stdout.splitlines()
             if text.strip() and not re.fullmatch(r"Stopped at [\d.]+s", text.strip())]
    return lines, count_writes(read_trace(image + ".trace"))


def one_status(lines, name, what):
    values = [status_line(text, name) for text in lines]
    values = [value for value in values if value is not None]
    expect(len(values) == 1, f"{what}: expected one '{name}' line, console was {lines}")
    return values[0]


def run_until_killed(exe, image, profile, kind, target, pause, label, timeout=300):
    """Run an image that never returns until it has printed `target` ACK or GEN lines, then SIGKILL.

    The kill follows after `pause` seconds, so that it lands at varying points of the loop. `label`
    names the run in a failure message.

    Returns (console lines, ACK or GEN lines printed), including what was still in the pipe when
    the process died."""
    where = f"{label}, target {target}, pause {pause:.3f} s"
    with tempfile.TemporaryFile() as errors:
        proc = subprocess.Popen(command(exe, image, profile), stdout=subprocess.PIPE, stderr=errors)
        fd = proc.stdout.fileno()
        lines, state, buffer = [], {}, b""
        deadline = time.monotonic() + timeout
        try:
            while progress(state) < target:
                remaining = deadline - time.monotonic()
                expect(remaining > 0, f"{where}: {progress(state)} lines before the timeout; "
                                      f"last lines {lines[-3:]}")
                if not select.select([fd], [], [], min(remaining, 1.0))[0]:
                    continue
                chunk = os.read(fd, 65536)
                if not chunk:
                    errors.seek(0)
                    raise AssertionError(f"{where}: the image ended after {progress(state)} "
                                         f"lines; {lines[-3:]} {errors.read()[-400:]}")
                buffer += chunk
                while b"\n" in buffer:
                    raw, buffer = buffer.split(b"\n", 1)
                    lines.append(raw.decode("ascii", "replace").strip())
                    observe(state, kind, lines[-1])
            time.sleep(pause)
        finally:
            proc.send_signal(signal.SIGKILL)
            proc.wait()
        # What the process had already written is still in the pipe. A last line cut short is
        # dropped by the parsers, so it cannot move a counter.
        rest = buffer + proc.stdout.read()
        proc.stdout.close()
        for raw in rest.split(b"\n"):
            if raw:
                lines.append(raw.decode("ascii", "replace").strip())
                observe(state, kind, lines[-1])
    expect(progress(state) >= target, f"{where}: only {progress(state)} lines")
    return lines, progress(state)


# ---- the scenarios ------------------------------------------------------------------------------

def prepare(args, image, profile):
    """A blank medium, then the format image once: the state that the card is in before Stage A."""
    blank_medium(image)
    lines, writes = run_idle(args.format_exe, image, profile)
    status = one_status(lines, "reliance_format", "prepare")
    expect(status == FORMAT_STATUS_OK and writes > 0,
           f"the format image on a blank medium: status {status}, {writes} writes")


def scenario_format(args, image, profile):
    blank_medium(image)
    blank = digest(image)
    lines, writes = run_idle(args.format_exe, image, profile)
    first = one_status(lines, "reliance_format", "format on a blank medium")
    expect(first == FORMAT_STATUS_OK, f"format on a blank medium printed status {first}")
    expect(writes > 0 and digest(image) != blank,
           f"the format image wrote {writes} requests and the digest "
           f"{'changed' if digest(image) != blank else 'is unchanged'}: nothing reached the medium")
    formatted = digest(image)
    lines, writes_again = run_idle(args.format_exe, image, profile)
    second = one_status(lines, "reliance_format", "second format")
    expect(second == FORMAT_STATUS_PRESENT, f"the second format run printed status {second}, "
                                            f"expected {FORMAT_STATUS_PRESENT} (volume present)")
    expect(digest(image) == formatted, "the second format run changed the medium")
    return f"first status {first} ({writes} writes), second status {second} ({writes_again} writes), digest unchanged"


def scenario_start(args, image, profile):
    prepare(args, image, profile)
    formatted = digest(image)
    lines, writes = run_idle(args.start_exe, image, profile)
    status = one_status(lines, "emmc_storage_start", "start on the formatted medium")
    expect(status == START_READY, f"start on the formatted medium printed status {status}")
    expect(writes == 0 and digest(image) == formatted,
           f"start on the formatted medium wrote {writes} requests")
    lines, writes = run_idle(args.default_exe, image, profile)
    expect(lines == [] and writes == 0 and digest(image) == formatted,
           f"the default image printed {lines} and wrote {writes} requests")
    blank_medium(image)
    blank = digest(image)
    lines, writes = run_idle(args.start_exe, image, profile)
    status = one_status(lines, "emmc_storage_start", "start on a blank medium")
    expect(status == START_MOUNT_FAILED, f"start on a blank medium printed status {status}, "
                                         f"expected {START_MOUNT_FAILED} (mount failed)")
    expect(writes == 0 and digest(image) == blank,
           f"start on a blank medium wrote {writes} requests or changed the medium")
    return "ready on the formatted medium, mount failed on a blank one, no writes; default image silent"


def interrupted(args, image, profile, kind, exe, judge):
    prepare(args, image, profile)
    log, counts = [], []
    rng = random.Random(args.seed)
    for run in range(args.restarts + 1):
        lines, count = run_until_killed(exe, image, profile, kind, args.target + rng.randrange(40),
                                        rng.uniform(0.0, 0.03), f"{kind} run {run + 1}")
        starts = [status_line(text, "sd_storage_start") for text in lines]
        expect([value for value in starts if value is not None][:1] == [0],
               f"{kind} run {run + 1}: the start status was not 0: {lines[:3]}")
        log.extend(lines)
        counts.append(count)
    return judge(log, args.restarts) | {"counts": counts}


def scenario_append(args, image, profile):
    result = interrupted(args, image, profile, "append", args.append_exe, judge_append)
    return (f"{result['judged']} restarts, no acknowledged record lost, no damaged record; "
            f"{result['with_torn_tail']} with a torn tail, "
            f"{result['with_unacked_present']} with records past the last ACK seen; ACK lines per run "
            f"{result['counts']}")


def scenario_replace(args, image, profile):
    result = interrupted(args, image, profile, "replace", args.replace_exe, judge_replace)
    return (f"{result['judged']} restarts, cfg never none, bad or stale; leftover cmp: "
            f"{result['cmp_valid']} valid, {result['cmp_bad']} incomplete; GEN lines per run "
            f"{result['counts']}")


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--format-exe", required=True)
    parser.add_argument("--append-exe", required=True)
    parser.add_argument("--replace-exe", required=True)
    parser.add_argument("--start-exe", required=True, help="the mount-check (.start) image")
    parser.add_argument("--default-exe", required=True, help="the default image")
    parser.add_argument("--image", required=True,
                        help="host file for the simulated medium; it is overwritten")
    parser.add_argument("--restarts", type=int, default=5)
    parser.add_argument("--target", type=int, default=200,
                        help="at least this many ACK lines (append) or GEN lines (replace) printed by "
                             "the run itself before each SIGKILL (a seeded 0-39 more)")
    parser.add_argument("--profile", choices=[name for name, _ in PROFILES], action="append",
                        help="run only this simulator profile (repeatable); the default is both")
    parser.add_argument("--seed", type=int, default=1,
                        help="seed of the extra lines and the pause before each kill")
    args = parser.parse_args()

    print(f"rehearsal: seed {args.seed}, target {args.target}, restarts {args.restarts}", flush=True)
    checks = 0
    for name, scenario in (("format", scenario_format), ("start", scenario_start),
                           ("append", scenario_append), ("replace", scenario_replace)):
        for profile_name, profile in PROFILES:
            if args.profile and profile_name not in args.profile:
                continue
            summary = scenario(args, args.image, profile)
            print(f"PASS {name} {profile_name}: {summary}", flush=True)
            checks += 1
    print(f"rehearsal: {checks} scenario runs passed (on a model; no SD host driver, SD layer or "
          "card was exercised)")


if __name__ == "__main__":
    main()
