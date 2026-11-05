#!/usr/bin/env python3
"""Run the composition's image three times on one simulated disk, as separate processes, and compare.

first boot, then the same disk with another device ID, then the right ID again. Each run has its own
seed, because the default repeats every nonce, and the disk is a file on the host, so this shows that
the disk outlives a process. It is no evidence about power loss, a card or a device.
"""
import argparse
import re
import subprocess
import sys
from pathlib import Path

RIGHT_ID = "0x12345678"
OTHER_ID = "0x12345679"


def run(executable, disk, device_id, seed, log):
    result = subprocess.run([executable, f"--flash={disk}", f"--device_id={device_id}", f"--seed={seed}"],
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace",
                            timeout=300, check=False)
    log.write_text(result.stdout, encoding="utf-8")
    return result.returncode, result.stdout


def keys_in(output):
    return re.findall(r"^COMPOSITION (\w+_key) type=(\d) public=([0-9a-f]+)$", output, re.MULTILINE)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable")
    parser.add_argument("--profile", required=True, choices=("mbed", "wolf"))
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    disk = args.output / "disk.bin"
    if disk.exists():
        disk.unlink()

    failures = []
    expected_keys = 2 if args.profile == "wolf" else 1
    outputs = {}
    for name, device_id, seed, suite, marker in (
            ("first", RIGHT_ID, 11, "first_boot", "COMPOSITION first boot complete"),
            ("other_device", OTHER_ID, 12, "other_device", "COMPOSITION other device complete"),
            ("later", RIGHT_ID, 13, "later_boot", "COMPOSITION later boot complete")):
        code, output = run(args.executable, disk, device_id, seed, args.output / f"{name}.log")
        outputs[name] = output
        passed = (code == 0 and "PROJECT EXECUTION SUCCESSFUL" in output and
                  f"TESTSUITE {suite} succeeded" in output and marker in output)
        print(f"{name}: {'pass' if passed else 'FAIL'}, exit {code}")
        if not passed:
            failures.append(name)
            print("\n".join(line for line in output.splitlines()
                            if re.search(r"Assertion failed|FAIL|COMPOSITION started", line)))

    first, later = keys_in(outputs["first"]), keys_in(outputs["later"])
    if len(first) != expected_keys or first != later:
        failures.append("device keys")
        print(f"FAIL: the keys of the first boot, {first}, and the later boot, {later}, differ")
    else:
        print(f"device keys: {len(first)} kept across the restart, of types {[key[1] for key in first]}")
    status = re.search(r"^COMPOSITION status (.*)$", outputs["first"], re.MULTILINE)
    events = re.search(r"^COMPOSITION events (.*)$", outputs["first"], re.MULTILINE)
    (args.output / "status.json").write_text((status.group(1) if status else "") + "\n", encoding="utf-8")
    print(f"events on the first boot: {events.group(1) if events else 'none'}")
    if failures:
        print(f"{args.profile}: failed: {failures}")
        return 1
    print(f"{args.profile}: composition passes on three boots of one disk")
    return 0


if __name__ == "__main__":
    sys.exit(main())
