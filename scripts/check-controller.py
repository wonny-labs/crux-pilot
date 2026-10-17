#!/usr/bin/env python3
"""Run the finite controller scenario; stop native_sim after its result marker."""
import argparse
import os
from pathlib import Path
import re
import selectors
import subprocess
import time


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("binary", type=Path)
    parser.add_argument("--log", type=Path, required=True)
    parser.add_argument("--expect-init-failure", action="store_true")
    parser.add_argument("--native-sim", action="store_true")
    args = parser.parse_args()
    raw = bytearray()
    with subprocess.Popen([str(args.binary.resolve())], stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT) as process:
        try:
            with selectors.DefaultSelector() as selector:
                selector.register(process.stdout, selectors.EVENT_READ)
                end = time.monotonic() + 10
                while time.monotonic() < end:
                    ready = selector.select(timeout=min(0.2, max(0, end - time.monotonic())))
                    if not ready:
                        if process.poll() is not None:
                            break
                        continue
                    chunk = os.read(process.stdout.fileno(), 4096)
                    if not chunk:
                        break
                    raw.extend(chunk)
                    if args.native_sim and (b"CONTROLLER_COMPLETE cycles=20" in raw or
                                            b"CONTROLLER_FAILED" in raw):
                        break
        finally:
            # native_sim keeps its idle kernel running after main returns. No target is accessed.
            if args.native_sim and process.poll() is None:
                process.terminate()
            try:
                process.wait(timeout=2)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
    output = raw.decode("utf-8", errors="replace")
    args.log.parent.mkdir(parents=True, exist_ok=True)
    args.log.write_text(output)
    if not args.native_sim:
        assert process.returncode == 0, f"Controller process exited with status {process.returncode}"
    if args.expect_init_failure:
        assert "init=-3" in output and "CONTROLLER_FAILED" in output, output
        assert "cycle=" not in output and "CONTROLLER_COMPLETE" not in output, output
        print("Controller missing-RTC initialisation failure: passed")
        return
    assert "CONTROLLER_COMPLETE cycles=20" in output, output
    assert "CONTROLLER_FAILED" not in output and "init=0" in output, output
    rows = re.findall(r"cycle=(\d+) status=(-?\d+) valid=(\d) utc=(\S+) "
                      r"monotonic_ms=(\d+) lateness_ms=(\d+)", output)
    assert len(rows) == 20, output
    last = -1
    for index, (cycle, status, valid, utc, monotonic, _) in enumerate(rows):
        assert int(cycle) == index and int(monotonic) > last, output
        last = int(monotonic)
        if index < 2:
            assert (status, valid, utc) == ("-4", "0", "unavailable"), output
        else:
            assert status == "0" and valid == "1" and utc.isdigit(), output
            if "composition=host-controlled" in output:
                assert int(utc) == (1792238400 if index < 6 else 1792234800), output
    assert int(rows[6][3]) < int(rows[5][3]), output
    print("Controller 20-cycle scenario, unset/valid UTC and backward UTC step: passed")


if __name__ == "__main__":
    main()
