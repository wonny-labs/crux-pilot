#!/usr/bin/env python3
"""The composition's CMake guard must refuse a board, and a change of entropy source, for its own reason.

It runs the function in CMake's script mode, so it needs no Zephyr. A composition that passes the guard goes
on to need a target, which script mode lacks, and that is the control: the guard let it through.
"""
import subprocess
import sys
import tempfile
from pathlib import Path

COMPOSITION = Path(__file__).resolve().parent / "composition" / "composition.cmake"
CASES = (
    ("a board", {}, "restricted to native_sim"),
    ("another entropy source", {"CONFIG_BOARD_NATIVE_SIM": "y"}, "entropy source changed"),
)


def run(settings):
    with tempfile.NamedTemporaryFile("w", suffix=".cmake", delete=False) as script:
        for name, value in settings.items():
            script.write(f"set({name} {value})\n")
        script.write(f'include("{COMPOSITION}")\ncrux_native_management_compose()\n')
    result = subprocess.run(["cmake", "-P", script.name], capture_output=True, text=True, check=False)
    Path(script.name).unlink()
    return result


def main():
    failures = []
    for name, settings, reason in CASES:
        result = run(settings)
        if result.returncode == 0 or reason not in result.stderr:
            failures.append(f"{name}: not refused for its reason: {result.stderr}")
    passed = run({"CONFIG_BOARD_NATIVE_SIM": "y", "CONFIG_FAKE_ENTROPY_NATIVE_SIM": "y"})
    if "restricted to native_sim" in passed.stderr or "entropy source changed" in passed.stderr:
        failures.append(f"native_sim with its entropy was refused: {passed.stderr}")
    for failure in failures:
        print(failure, file=sys.stderr)
    if failures:
        return 1
    print("control ok: the composition's CMake refuses a board and another entropy source")
    return 0


if __name__ == "__main__":
    sys.exit(main())
