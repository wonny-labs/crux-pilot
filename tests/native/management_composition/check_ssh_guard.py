#!/usr/bin/env python3
"""The SSH composition's test host key must be refused outside a declared native_sim test image (Issue #197).

The composition holds the host key in test form, as the services composition holds test entropy, and its
guards refuse it the same way: ssh.cmake at configuration and ssh_test_host_key_guard.h at compilation. This
runs the CMake function in script mode, so it needs no Zephyr, and compiles the guard header and the source
that includes it with each setting. A composition that passes the guard goes on to need a target, which
script mode lacks, and that is the control: the guard let it through. Run it on any host with cmake and cc.
"""
import subprocess
import sys
import tempfile
from pathlib import Path

COMPOSITION = Path(__file__).resolve().parent / "composition"
COMPONENTS = COMPOSITION.parents[3] / "components"
GUARD = COMPOSITION / "ssh_test_host_key_guard.h"
SOURCE = COMPOSITION / "crux_native_management_ssh.c"
GUARD_MESSAGE = "The test host key reaches a build that is not a declared native_sim test image"
TEST_IMAGE = {"CONFIG_BOARD_NATIVE_SIM": "y", "CRUX_NATIVE_MANAGEMENT_TEST_IMAGE": "ON"}
SELECTED = dict(TEST_IMAGE, CONFIG_CRUX_SSH="y", CONFIG_CRUX_SSH_CLI="y", CONFIG_CRUX_SSH_SFTP="y")
CMAKE_CASES = (
    ("a board", {"CONFIG_CRUX_SSH": "y"}, "restricted to native_sim"),
    ("native_sim, not a declared test image", {"CONFIG_BOARD_NATIVE_SIM": "y", "CONFIG_CRUX_SSH": "y"},
     "not a declared test image"),
    ("a test random generator", dict(TEST_IMAGE, CONFIG_TEST_RANDOM_GENERATOR="y"),
     "not from CONFIG_TEST_RANDOM_GENERATOR"),
    ("the Mbed profile", dict(TEST_IMAGE, CONFIG_MBEDTLS="y"), "need the Wolf profile"),
    ("SSH not selected", TEST_IMAGE, "CONFIG_CRUX_SSH is not selected"),
    ("no command line or no SFTP", dict(TEST_IMAGE, CONFIG_CRUX_SSH="y"), "selects both"),
)
# What the control reaches once every guard of ssh.cmake has passed: the settings function, which needs wolfSSL.
CONTROL_REASON = "needs the crux-secure-management west group"
# Definitions for the guard header, and whether it must compile.
C_CASES = (
    ("native_sim test image", ["CONFIG_BOARD_NATIVE_SIM", "CRUX_NATIVE_MANAGEMENT_TEST_IMAGE"], True),
    ("native_sim, not a test image", ["CONFIG_BOARD_NATIVE_SIM"], False),
    ("a board marked as a test image", ["CRUX_NATIVE_MANAGEMENT_TEST_IMAGE"], False),
    ("a board", [], False),
    ("a test random generator", ["CONFIG_BOARD_NATIVE_SIM", "CRUX_NATIVE_MANAGEMENT_TEST_IMAGE",
                                 "CONFIG_TEST_RANDOM_GENERATOR"], False),
)


def squeeze(text):
    """CMake wraps its messages, so a reason is looked for with the line breaks removed."""
    return " ".join(text.split())


def run_cmake(settings):
    with tempfile.NamedTemporaryFile("w", suffix=".cmake", delete=False) as script:
        for name, value in settings.items():
            script.write(f"set({name} {value})\n")
        script.write(f'include("{COMPOSITION / "composition.cmake"}")\n')
        script.write(f'include("{COMPOSITION / "services.cmake"}")\n')
        script.write(f'include("{COMPOSITION / "ssh.cmake"}")\n')
        script.write("crux_native_management_compose_ssh()\n")
    result = subprocess.run(["cmake", "-P", script.name], capture_output=True, text=True, check=False)
    Path(script.name).unlink()
    return result


def compile_c(path, definitions):
    command = ["cc", "-std=c17", "-fsyntax-only", "-x", "c", str(path), f"-I{COMPOSITION}",
               f"-I{COMPONENTS / 'access' / 'include'}", f"-I{COMPONENTS / 'base' / 'include'}"]
    command += [f"-D{definition}=1" for definition in definitions]
    return subprocess.run(command, capture_output=True, text=True, check=False)


def main():
    failures = []
    for name, settings, reason in CMAKE_CASES:
        result = run_cmake(settings)
        if result.returncode == 0 or reason not in squeeze(result.stderr):
            failures.append(f"ssh.cmake, {name}: not refused for its reason: {result.stderr}")
    passed = run_cmake(SELECTED)
    if CONTROL_REASON not in squeeze(passed.stderr) or any(
            reason in squeeze(passed.stderr) for _, _, reason in CMAKE_CASES):
        failures.append(f"ssh.cmake: a declared native_sim test image was refused by a guard: {passed.stderr}")
    for name, definitions, compiles in C_CASES:
        result = compile_c(GUARD, definitions)
        if compiles and result.returncode != 0:
            failures.append(f"ssh_test_host_key_guard.h, {name}: refused: {result.stderr}")
        if not compiles and (result.returncode == 0 or "#error" not in result.stderr):
            failures.append(f"ssh_test_host_key_guard.h, {name}: not refused: {result.stderr}")
    # The source itself includes the guard: a board is refused for the guard's reason, and a test image is
    # not, and then stops at a Zephyr header that this host lacks.
    refused = compile_c(SOURCE, [])
    if GUARD_MESSAGE not in refused.stderr:
        failures.append(f"crux_native_management_ssh.c on a board: not refused by the guard: {refused.stderr}")
    allowed = compile_c(SOURCE, ["CONFIG_BOARD_NATIVE_SIM", "CRUX_NATIVE_MANAGEMENT_TEST_IMAGE"])
    if GUARD_MESSAGE in allowed.stderr:
        failures.append(f"crux_native_management_ssh.c in a test image: refused by the guard: {allowed.stderr}")
    for failure in failures:
        print(failure, file=sys.stderr)
    if failures:
        return 1
    print("control ok: the SSH composition refuses the test host key outside a declared native_sim test image, "
          "a test random generator, the Mbed profile and a missing CONFIG_CRUX_SSH, in CMake and in C")
    return 0


if __name__ == "__main__":
    sys.exit(main())
