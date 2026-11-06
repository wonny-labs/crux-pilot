#!/usr/bin/env python3
"""The composition's guards must refuse a board, and a change of entropy source, for their own reason.

It runs the functions in CMake's script mode, so it needs no Zephyr. A composition that passes the guard goes
on to need a target, which script mode lacks, and that is the control: the guard let it through. The services
composition of Issue #242 adds two refusals, test entropy in a build that is not a declared test image and a
test random generator, and its C guard, entropy_guard.h, is compiled with each setting.
"""
import subprocess
import sys
import tempfile
from pathlib import Path

COMPOSITION = Path(__file__).resolve().parent / "composition" / "composition.cmake"
SERVICES = COMPOSITION.parent / "services.cmake"
ENTROPY_GUARD = COMPOSITION.parent / "entropy_guard.h"
SERVICE_BASE = {"CONFIG_BOARD_NATIVE_SIM": "y", "CONFIG_FAKE_ENTROPY_NATIVE_SIM": "y", "CONFIG_CRUX_WEB": "y"}
SERVICE_CASES = (
    ("a test random generator", dict(SERVICE_BASE, CONFIG_TEST_RANDOM_GENERATOR="y",
                                     CRUX_NATIVE_MANAGEMENT_TEST_IMAGE="ON"), "not from CONFIG_TEST_RANDOM_GENERATOR"),
    ("test entropy in a build that is not a test image", SERVICE_BASE, "not a declared test image"),
    ("plain HTTP", dict(SERVICE_BASE, CRUX_NATIVE_MANAGEMENT_TEST_IMAGE="ON",
                        CONFIG_CRUX_WEB_PLAIN_HTTP_FOR_TESTS="y"), "HTTPS only"),
)
# Definitions for entropy_guard.h, and whether it must compile.
C_CASES = (
    ("native_sim test image", ["CONFIG_BOARD_NATIVE_SIM", "CONFIG_FAKE_ENTROPY_NATIVE_SIM",
                               "CRUX_NATIVE_MANAGEMENT_TEST_IMAGE"], True),
    ("native_sim, not a test image", ["CONFIG_BOARD_NATIVE_SIM", "CONFIG_FAKE_ENTROPY_NATIVE_SIM"], False),
    ("a board marked as a test image", ["CONFIG_FAKE_ENTROPY_NATIVE_SIM", "CRUX_NATIVE_MANAGEMENT_TEST_IMAGE"], False),
    ("a test random generator", ["CONFIG_BOARD_NATIVE_SIM", "CONFIG_FAKE_ENTROPY_NATIVE_SIM",
                                 "CRUX_NATIVE_MANAGEMENT_TEST_IMAGE", "CONFIG_TEST_RANDOM_GENERATOR"], False),
    ("a board with its own entropy", [], True),
)
CASES = (
    ("a board", {}, "restricted to native_sim"),
    ("another entropy source", {"CONFIG_BOARD_NATIVE_SIM": "y"}, "entropy source changed"),
)


def run(settings, services=False):
    with tempfile.NamedTemporaryFile("w", suffix=".cmake", delete=False) as script:
        for name, value in settings.items():
            script.write(f"set({name} {value})\n")
        script.write(f'include("{COMPOSITION}")\n')
        if services:
            script.write(f'include("{SERVICES}")\ncrux_native_management_compose_services()\n')
        else:
            script.write("crux_native_management_compose()\n")
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
    failures += services_failures()
    for failure in failures:
        print(failure, file=sys.stderr)
    if failures:
        return 1
    print("control ok: the composition's CMake refuses a board and another entropy source")
    print("control ok: the services composition refuses test entropy outside a declared native_sim test "
          "image, a test random generator and plain HTTP, in CMake and in C")
    return 0


def services_failures():
    failures = []
    reasons = [reason for _, _, reason in SERVICE_CASES]
    for name, settings, reason in SERVICE_CASES:
        result = run(settings, services=True)
        if result.returncode == 0 or reason not in result.stderr:
            failures.append(f"services, {name}: not refused for its reason: {result.stderr}")
    passed = run(dict(SERVICE_BASE, CRUX_NATIVE_MANAGEMENT_TEST_IMAGE="ON"), services=True)
    if any(reason in passed.stderr for reason in reasons):
        failures.append(f"services: a declared native_sim test image was refused: {passed.stderr}")
    for name, definitions, compiles in C_CASES:
        command = ["cc", "-std=c17", "-fsyntax-only", "-x", "c", str(ENTROPY_GUARD)]
        command += [f"-D{definition}=1" for definition in definitions]
        result = subprocess.run(command, capture_output=True, text=True, check=False)
        if compiles and result.returncode != 0:
            failures.append(f"entropy_guard.h, {name}: refused: {result.stderr}")
        if not compiles and (result.returncode == 0 or "#error" not in result.stderr):
            failures.append(f"entropy_guard.h, {name}: not refused: {result.stderr}")
    return failures


if __name__ == "__main__":
    sys.exit(main())
