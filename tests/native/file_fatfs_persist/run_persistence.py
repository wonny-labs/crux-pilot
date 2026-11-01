#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Process-restart and process-kill scenarios for the FatFs File provider.

The disk is a zephyr,flash-disk on the flash simulator, backed by a host file, so its contents
outlive the simulator process. A flash-disk update is a read-modify-erase-write of a larger block
behind a write cache, which makes a kill during an update destructive for neighbouring sectors.
The scenarios therefore separate what can be asserted from what is only recorded. A kill that
lands inside an update is not controlled, so the recorded result is an observation of this
medium model, not a measurement:

* restart       asserted: files acknowledged in one process are intact in the next process
* never-format  asserted: mounting blank media fails and changes no byte
* kill-idle     asserted: killed between updates, every acknowledged file is intact
* kill-anywhere recorded: killed at a random point, including inside a flush
* mutant        the idle kill must detect an acknowledgement sent before the update is durable
                (before both sync and close); it does not show that sync alone works

This is software evidence only. It covers process exit and SIGKILL on a host file. It is not
power-loss, reboot or physical-media evidence.
"""
import argparse
import hashlib
import os
import pty
import random
import select
import signal
import subprocess
import sys
import tempfile
import time

LINE_TIMEOUT = 30.0
IDLE_MS = 200
KILL_LATENCY = 0.08  # a kill later than this after the IDLE report may have missed the rest
RESAMPLES = 5


def command(exe, flash, *options, realtime=False):
    base = [exe, f"--flash={flash}"]
    return base + ([] if realtime else ["-no-rt"]) + list(options)


def run(exe, flash, *options, timeout=60):
    """Run one phase to completion and return (exit code, stdout lines)."""
    proc = subprocess.run(command(exe, flash, *options), capture_output=True, text=True,
                          timeout=timeout)
    return proc.returncode, proc.stdout.splitlines()


def digest(path):
    with open(path, "rb") as handle:
        return hashlib.sha256(handle.read()).hexdigest()


def expect(condition, message):
    if not condition:
        raise AssertionError(message)


def scenario_restart(exe, flash):
    rc, out = run(exe, flash, "--crux_mode=format")
    expect(rc == 0 and "FORMATTED" in out, f"format failed: {rc} {out}")
    rc, out = run(exe, flash, "--crux_mode=write", "--crux_run=1", "--crux_count=8")
    expect(rc == 0 and "WROTE 8" in out, f"write failed: {rc} {out}")
    rc, out = run(exe, flash, "--crux_mode=verify", "--crux_run=1", "--crux_count=8")
    expect(rc == 0 and "VERIFIED 8" in out, f"verify after restart failed: {rc} {out}")
    rc, out = run(exe, flash, "--crux_mode=write", "--crux_run=2", "--crux_count=8")
    expect(rc == 0, f"replace failed: {rc} {out}")
    rc, out = run(exe, flash, "--crux_mode=verify", "--crux_run=2", "--crux_count=8")
    expect(rc == 0 and "VERIFIED 8" in out, f"verify of replaced files failed: {rc} {out}")
    rc, out = run(exe, flash, "--crux_mode=verify", "--crux_run=1", "--crux_count=8")
    expect(rc == 1 and any(line.startswith("FAIL verify slot") for line in out),
           f"the first run's content must be reported gone after replacement: {rc} {out}")


def scenario_never_formats(exe, flash):
    rc, out = run(exe, flash, "--crux_mode=verify", "--crux_run=1", "--crux_count=1")
    expect(rc == 2 and any(line.startswith("MOUNT_FAIL") for line in out),
           f"blank media must not mount: {rc} {out}")
    first = digest(flash)
    rc, out = run(exe, flash, "--crux_mode=verify", "--crux_run=1", "--crux_count=1")
    expect(rc == 2, f"second mount attempt: {rc} {out}")
    expect(digest(flash) == first, "a failed mount changed the media")
    with open(flash, "rb") as handle:
        data = handle.read()
    expect(set(data) == {0xFF}, "blank media must still be fully erased (never formatted)")


def parse(data, tag):
    values = []
    for line in data.decode(errors="replace").splitlines():
        parts = line.strip().split()
        if len(parts) == 3 and parts[0] == tag:
            values.append(int(parts[2]))
    return values


def read_until(master, predicate, deadline):
    """Return (data, time the predicate became true or None)."""
    data = b""
    while not predicate(data) and time.monotonic() < deadline:
        ready, _, _ = select.select([master], [], [], 0.02)
        if not ready:
            continue
        try:
            chunk = os.read(master, 4096)
        except OSError:
            break
        if not chunk:
            break
        data += chunk
    return data, (time.monotonic() if predicate(data) else None)


def drain(master):
    data = b""
    while True:
        ready, _, _ = select.select([master], [], [], 0.05)
        if not ready:
            break
        try:
            chunk = os.read(master, 4096)
        except OSError:
            break
        if not chunk:
            break
        data += chunk
    return data


def kill_once(exe, flash, run_id, rng, idle, mutant):
    """Format, stress, SIGKILL, then check. Returns (last acknowledged update, outcome).

    idle=True kills right after the child reports it is resting between updates; otherwise the
    kill lands at a random time. Outcome is "ok:<in-flight file state>", "acked_lost" or
    "unmountable".
    """
    rc, out = run(exe, flash, "--crux_mode=format")
    expect(rc == 0 and "FORMATTED" in out, f"format failed: {rc} {out}")
    master, slave = pty.openpty()
    options = ["--crux_mode=stress", f"--crux_run={run_id}", "--crux_count=0"]
    if idle:
        options.append(f"--crux_idle_ms={IDLE_MS}")
    if mutant:
        options.append(f"--crux_mutant={mutant}")
    child = subprocess.Popen(command(exe, flash, *options, realtime=idle), stdout=slave,
                             stderr=slave, close_fds=True)
    os.close(slave)
    try:
        target = rng.randint(2, 9)
        late = False
        if idle:
            data, seen = read_until(master, lambda d: any(k >= target for k in parse(d, "IDLE")),
                                    time.monotonic() + LINE_TIMEOUT)
            expect(child.poll() is None,
                   f"the stress child exited early (code {child.returncode}): "
                   f"{data.decode(errors='replace')[-200:]!r}")
            expect(seen is not None, "the stress child reported no resting state before the timeout")
            child.send_signal(signal.SIGKILL)
            # Late if the runner read the line late, or already holds a second resting report
            # (the child has moved on to another update).
            late = (time.monotonic() - seen > KILL_LATENCY or
                    len([k for k in parse(data, "IDLE") if k >= target]) > 1)
        else:
            data, _ = read_until(master, lambda d: len(parse(d, "ACK")) >= target * 4,
                                 time.monotonic() + LINE_TIMEOUT)
            time.sleep(rng.uniform(0.0, 0.02))  # land the kill at varying points inside an update
            child.send_signal(signal.SIGKILL)
        child.wait()
        data += drain(master)
    finally:
        os.close(master)
    if late:
        return 0, "resample"  # the kill may not have landed between updates: not a valid sample
    acks = parse(data, "ACK")
    expect(acks, "the stress run acknowledged nothing before it was killed")
    last = max(acks)
    rc, out = run(exe, flash, "--crux_mode=check", f"--crux_run={run_id}", f"--crux_count={last}")
    if rc == 0:
        state = next((line for line in out if line.startswith("CHECK_OK")), "")
        inflight = state.split("inflight=")[-1]
        if idle and not mutant and inflight in ("new", "torn"):
            return last, "resample"  # the next update had begun: the kill was not between updates
        return last, "ok:" + inflight
    if any(line.startswith("MOUNT_FAIL") for line in out):
        return last, "unmountable"
    return last, "acked_lost"


def scenario_kill(exe, flash, iterations, seed, idle, mutant=""):
    rng = random.Random(seed)
    outcomes = {}
    resampled = 0
    for index in range(iterations):
        for _ in range(RESAMPLES):
            _, outcome = kill_once(exe, flash, index + 1, rng, idle, mutant)
            if outcome != "resample":
                break
            resampled += 1
        expect(outcome != "resample", f"iteration {index + 1}: {RESAMPLES} kills in a row were late")
        outcomes[outcome] = outcomes.get(outcome, 0) + 1
    if resampled:
        outcomes["(late kills retried)"] = resampled
    return outcomes


def all_intact(outcomes):
    return all(key.startswith("ok:") or key.startswith("(") for key in outcomes)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", required=True, help="built native_sim executable")
    parser.add_argument("--iterations", type=int, default=25, help="idle-kill iterations")
    parser.add_argument("--anywhere-iterations", type=int, default=20,
                        help="random-time kill iterations (recorded only)")
    parser.add_argument("--mutant-iterations", type=int, default=10,
                        help="idle-kill iterations of the negative control")
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--mutant", default="",
                        help="run only this negative control; succeeds only if every iteration "
                        "reports a lost acknowledged file")
    args = parser.parse_args()

    with tempfile.TemporaryDirectory() as workdir:
        flash = os.path.join(workdir, "flash.bin")

        def fresh():
            if os.path.exists(flash):
                os.remove(flash)

        def mutant_scenario(name):
            fresh()
            outcomes = scenario_kill(args.exe, flash, args.mutant_iterations, args.seed, True, name)
            counted = {k: n for k, n in outcomes.items() if not k.startswith("(")}
            detected = counted.get("acked_lost", 0) == sum(counted.values()) > 0
            print(f"mutant {name!r}: outcomes {outcomes}")
            return detected

        if args.mutant:
            sys.exit(0 if mutant_scenario(args.mutant) else 1)

        scenario_restart(args.exe, flash)
        print("PASS restart: acknowledged files survive process exit and replacement")
        fresh()
        scenario_never_formats(args.exe, flash)
        print("PASS never-format: blank media is not mounted and not modified")
        fresh()

        idle = scenario_kill(args.exe, flash, args.iterations, args.seed, True)
        print(f"kill-idle: {args.iterations} SIGKILL iterations, outcomes {idle}")
        if not all_intact(idle):
            print("FAIL kill-idle: an acknowledged file was lost or the volume did not mount")
            sys.exit(1)
        print("PASS kill-idle: every acknowledged file was intact after every kill")
        if not mutant_scenario("ack_before_durable"):
            print("FAIL mutant: the acknowledge-before-durable defect was not reported as a lost "
                  "acknowledged file in every iteration")
            sys.exit(1)
        print("PASS mutant: acknowledging before the update is durable is caught every time")
        fresh()

        anywhere = scenario_kill(args.exe, flash, args.anywhere_iterations, args.seed, False)
        print(f"RECORDED kill-anywhere: {args.anywhere_iterations} SIGKILL iterations at random "
              f"times, outcomes {anywhere}. Not asserted: where the kill lands is not controlled, "
              "and on this erase-then-program medium an update in flight can destroy neighbouring "
              "sectors")


if __name__ == "__main__":
    main()
