#!/usr/bin/env python3
"""Explicitly program, verify and capture the authorised EVKB M2 images on macOS."""
import argparse
from datetime import datetime, timezone
import fcntl
import hashlib
import json
from pathlib import Path
import platform
import re
import subprocess
import termios
import time

REPO = Path(__file__).resolve().parents[2]
MIN_UNUSED = 1024  # Predeclared headroom objective for these short unloaded runs only.
STACK_OBJECT_BYTES = 4160  # 4096 usable + 64-byte MPU guard in the verified ARM configuration.


def validate(output, mode):
    assert output.count("*** Booting Zephyr") == 1, "Expected one fresh boot"
    assert output.lstrip().startswith("*** Booting Zephyr"), "Stale pre-reset output"
    assert not re.search(r"CONTROLLER_FAILED|FAIL -|PROJECT EXECUTION FAILED|FATAL ERROR", output)
    if mode == "native":
        names = ("lifecycle_periodic_and_stack", "mutex_contention_timeout_and_recovery",
                 "semaphore_expiry_wakeup_and_capacity", "bounded_messages")
        for name in names:
            assert len(re.findall(r"PASS - test_" + name + r"\b", output)) == 1
        assert "PROJECT EXECUTION SUCCESSFUL" in output
        cycles = re.findall(r"PROBE native cycle=(\d+) lateness_ms=(\d+)", output)
        assert [int(c) for c, _ in cycles] == list(range(6))
        periodic = re.search(r"PROBE native periodic elapsed_ms=(\d+) skipped=(\d+) "
                             r"priority=5 stack_bytes=4160", output)
        assert periodic and int(periodic[2]) >= 2
        stack = re.search(r"PROBE native stack_unused_bytes=(?P<unused>\d+)", output)
        assert stack
        result = dict(cycles=6, lateness_ms=[int(v) for _, v in cycles],
                      elapsed_ms=int(periodic[1]), skipped=int(periodic[2]), native_cases=4)
    else:
        assert "composition=zephyr init=0" in output
        assert output.count("CONTROLLER_COMPLETE cycles=20") == 1
        rows = re.findall(r"cycle=(\d+) status=(-?\d+) valid=(\d) utc=(\S+) "
                          r"monotonic_ms=(\d+) lateness_ms=(\d+)", output)
        assert len(rows) == 20
        skipped = [int(v) for v in re.findall(r"schedule skipped=(\d+)", output)]
        assert len(skipped) == 20
        times, deadlines = [], []
        for index, (cycle, status, valid, utc, monotonic, late) in enumerate(rows):
            assert int(cycle) == index
            now = int(monotonic)
            assert not times or now > times[-1]
            times.append(now)
            deadlines.append(now - int(late))
            if index < 2:
                assert (status, valid, utc) == ("-4", "0", "unavailable")
            else:
                base = 1792238400 if index < 6 else 1792234800
                assert (status, valid) == ("0", "1") and utc.isdigit()
                assert base <= int(utc) <= base + 16  # 15 s capture + RTC tick allowance.
            if index:
                assert deadlines[index] == deadlines[index - 1] + 100 * (skipped[index - 1] + 1)
        assert int(rows[6][3]) < int(rows[5][3])
        stack = re.search(r"controller stack_bytes=4160 unused_bytes=(?P<unused>\d+) priority=5 heap_bytes=0",
                          output)
        assert stack
        result = dict(cycles=20, monotonic_ms=times,
                      intervals_ms=[b - a for a, b in zip(times, times[1:])],
                      lateness_ms=[int(row[5]) for row in rows], skipped=skipped)
    unused = int(stack["unused"])
    assert MIN_UNUSED <= unused <= 4096, "Scoped worker-stack headroom objective not met"
    result.update(stack_bytes=4096, allocated_stack_bytes=STACK_OBJECT_BYTES,
                  reserved_guard_bytes=64, unused_bytes=unused, used_bytes=4096 - unused)
    return result


def commander(probe, commands, output, timeout=120):
    command_file = output.with_suffix(".jlink")
    command_file.write_text(commands)
    command = ["/usr/local/bin/JLinkExe", "-USB", probe, "-Device", "MIMXRT1062xxx6A",
               "-If", "SWD", "-Speed", "400", "-NoGui", "1", "-ExitOnError", "1",
               "-AutoConnect", "1", "-CommandFile", str(command_file)]
    with output.open("wb") as log:
        subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=timeout)
    return output.read_text(errors="replace")


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("mode", "uart", "probe", "board", "manifest", "evidence"):
        parser.add_argument("--" + name, required=True)
    parser.add_argument("--program", action="store_true", required=True,
                        help="Explicit current-task Flash/Reset/UART authorisation is required")
    args = parser.parse_args()
    assert platform.system() == "Darwin"
    assert args.mode in ("native", "controller")
    assert args.board == "mimxrt1060_evk@B//qspi" and args.probe == "602003106"
    assert args.uart == "/dev/cu.usbmodem11102"
    manifest_path = Path(args.manifest).resolve()
    manifest = json.loads(manifest_path.read_text())
    assert manifest["target"] == args.board and manifest["application"] == args.mode
    assert manifest["source"]["recoverable_from_git"] and not manifest["source"]["dirty"]
    for record in manifest["artifacts"]:
        assert sha(Path(record["path"])) == record["sha256"], record["path"]
    binary = Path(next(r["path"] for r in manifest["artifacts"] if r["path"].endswith("/zephyr.bin")))
    assert manifest["bin_address"] == 0x60000000
    assert manifest["elf_segments_match_bin"] and manifest["rom_start_at_bin_base"]
    assert manifest["worker_stack_usable_bytes"] == 4096
    assert manifest["worker_stack_object_bytes"] == STACK_OBJECT_BYTES
    assert manifest["worker_stack_reserved_bytes"] == 64
    assert 0 < binary.stat().st_size < 8 * 1024 * 1024
    assert '"' not in str(binary) and "\n" not in str(binary)
    out = Path(args.evidence).resolve()
    out.mkdir(parents=True, exist_ok=False)
    runner_status = subprocess.check_output(["git", "status", "--porcelain"], cwd=REPO, text=True)
    assert not runner_status, "Capture source must be committed and clean"
    runner_commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=REPO, text=True).strip()
    metadata = dict(executed_at_utc=datetime.now(timezone.utc).isoformat(),
                    source=manifest["source"], manifest_sha256=sha(manifest_path),
                    runner_source=dict(git_commit=runner_commit, dirty=False,
                                       script_sha256=sha(Path(__file__)),
                                       reset_script_sha256=sha(REPO / "tests/target/reset.jlink")),
                    board=args.board, probe=args.probe, uart=args.uart, baud=115200,
                    swd_khz=400, worker_unused_min_bytes=MIN_UNUSED, runs=3,
                    capture_timeout_seconds=20 if args.mode == "native" else 15,
                    python=platform.python_version(), host=platform.platform())
    (out / "inputs.json").write_text(json.dumps(metadata, indent=2) + "\n")
    import serial  # Hardware-only dependency; software evidence checks do not open a port.
    results = []
    with serial.Serial(args.uart, 115200, timeout=0.05, exclusive=True) as port:
        fcntl.ioctl(port.fileno(), termios.TIOCEXCL)
        preflight = commander(args.probe, "connect\nq\n", out / "probe-preflight.log", 20)
        volts = re.search(r"VTref\s*=\s*([\d.]+)\s*V", preflight)
        assert volts and 3.0 <= float(volts[1]) <= 3.6, "Unexpected target voltage"
        assert "Cortex-M7" in preflight and args.probe in preflight
        program = commander(args.probe, f'r\nh\nloadfile "{binary}", 0x60000000\n'
                            f'verifybin "{binary}", 0x60000000\nq\n', out / "program.log")
        assert "Verify successful" in program, "Full BIN readback verification did not succeed"
        errors = [line for line in program.splitlines() if line.startswith("****** Error:")]
        assert all("Could not start CPU core. (ErrorCode: -1)" in line for line in errors)
        if errors:
            assert program.index(errors[0]) > program.index("J-Link>q")
        metadata["program_disconnect_diagnostics"] = errors
        metadata["program_log_sha256"] = sha(out / "program.log")
        metadata["vtref_volts"] = float(volts[1])
        (out / "inputs.json").write_text(json.dumps(metadata, indent=2) + "\n")
        for number in range(1, 4):
            run = out / f"run-{number:02d}"
            run.mkdir()
            # Disconnect can resume the old image before the explicit qualifying reset.
            # Preserve and drain that output; never trim bytes from the qualifying capture.
            quiet = time.monotonic()
            drain_end = quiet + 5
            with (run / "pre-reset-uart.bin").open("wb") as before:
                while time.monotonic() < drain_end and time.monotonic() - quiet < 0.2:
                    chunk = port.read(4096)
                    if chunk:
                        before.write(chunk)
                        quiet = time.monotonic()
            assert time.monotonic() - quiet >= 0.2, "Pre-reset UART did not settle"
            port.reset_input_buffer()
            started = time.monotonic()
            reset = commander(args.probe, (REPO / "tests/target/reset.jlink").read_text(), run / "reset.log", 20)
            assert "****** Error:" not in reset, "Reset/start diagnostic requires investigation"
            raw = bytearray()
            marker = b"PROJECT EXECUTION SUCCESSFUL" if args.mode == "native" else b"CONTROLLER_COMPLETE cycles=20"
            with (run / "uart-rx.bin").open("wb") as stream, (run / "receive.jsonl").open("w") as events:
                end = started + metadata["capture_timeout_seconds"]
                while time.monotonic() < end:
                    chunk = port.read(4096)
                    if chunk:
                        raw.extend(chunk)
                        stream.write(chunk)
                        stream.flush()
                        events.write(json.dumps(dict(monotonic=time.monotonic(), hex=chunk.hex())) + "\n")
                        events.flush()
                    if marker in raw:
                        break
            text = raw.decode("utf-8", errors="replace")
            (run / "uart.txt").write_text(text)
            try:
                assert time.monotonic() <= end and marker in raw, "Capture liveness bound exceeded"
                result = validate(text, args.mode)
            except AssertionError:
                (run / "result.json").write_text(json.dumps(dict(status="FAIL")) + "\n")
                raise
            result.update(status="PASS", capture_seconds=time.monotonic() - started)
            (run / "result.json").write_text(json.dumps(result, indent=2) + "\n")
            results.append(result)
            print(f"{args.mode} run{number}: PASS; worker unused={result['unused_bytes']} bytes", flush=True)
    (out / "summary.json").write_text(json.dumps(dict(status="PASS", runs=results), indent=2) + "\n")


if __name__ == "__main__":
    main()
