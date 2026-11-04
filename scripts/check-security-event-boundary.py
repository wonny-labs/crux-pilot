#!/usr/bin/env python3
"""Check the Security event boundaries that its target layout cannot enforce alone.

- The public header contains no native types, and includes only what its contract names.
- A consumer compiles as C17 and C++17, and cannot reach private headers or the Access adapter.
- A final link needs the provider, and links with the provider as objects.
- The fake and the instrumented core exist only when Crux builds its own tests.
- The emit path reaches no storage and no log: only the drain calls Logging, and no source names a
  storage component.
- A build-time reserve that leaves routine events no room is rejected when it compiles.
"""
import argparse
import os
from pathlib import Path
import re
import runpy
import shutil
import subprocess
import sys

REPO = Path(__file__).resolve().parents[1]
COMPONENT = REPO / "components/security_event"
HEADER = COMPONENT / "include/crux/security_event.h"
ALLOWED_INCLUDES = {"stdbool.h", "stddef.h", "stdint.h", "crux/access.h", "crux/status.h"}
COMPILE_REJECTED = {
    "include_internal": "security_event_internal.h",
    "include_test": "security_event_test.h",
    "include_adapter": "crux_access_adapter.h",
}
STORAGE_TOKEN = re.compile(r"\b(crux_file_\w+|crux_config\w*|crux_credential\w*|fs_\w+|settings_\w+)\b"
                           r"|crux/(?:file|configuration|config_service|credential_store)\.h")
LOG_CALL = re.compile(r"\bcrux_log_write\s*\(")
DIRECT_OUTPUT = re.compile(r"\b(?:printf|fprintf|puts|LOG_\w+)\s*\(")


C_COMPILER = os.environ.get("CC") or shutil.which("cc") or "cc"


def run(command, cwd=REPO):
    return subprocess.run(command, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                          text=True, check=False)


def configure(build, probe):
    return run(["cmake", "-S", "tests/security_event_consumer", "-B", str(build), "-G", "Ninja",
                "-DCMAKE_BUILD_TYPE=Debug", f"-DSECURITY_EVENT_PROBE={probe}"])


def header_errors():
    native = runpy.run_path(str(Path(__file__).with_name("check-native-interfaces.py")))
    errors = []
    for line, reason in native["violations"](HEADER, public=True, networking=False):
        errors.append(f"{HEADER.relative_to(REPO)}:{line}: {reason}")
    for match in re.finditer(r'^\s*#\s*include\s*[<"]([^>"\n]+)[>"]',
                             HEADER.read_text(encoding="utf-8"), re.MULTILINE):
        if match.group(1) not in ALLOWED_INCLUDES:
            errors.append(f"{HEADER.relative_to(REPO)}: unexpected include {match.group(1)}")
    return errors


def source_errors():
    errors = []
    for path in sorted((COMPONENT / "src").rglob("*")):
        if path.suffix not in {".c", ".h", ".inc"}:
            continue
        name = path.relative_to(REPO)
        text = path.read_text(encoding="utf-8")
        for match in STORAGE_TOKEN.finditer(text):
            errors.append(f"{name}: names storage: {match.group()}")
        if DIRECT_OUTPUT.search(text):
            errors.append(f"{name}: writes directly; lines go through Logging")
        if LOG_CALL.search(text) and path.name != "security_event_drain.c":
            errors.append(f"{name}: calls Logging; only the drain may")
        if "fake" in text.lower():
            errors.append(f"{name}: names a fake")
    return errors


def probe_errors(build_root):
    errors = []
    build = build_root / "consumer"
    result = configure(build, "consumer")
    if result.returncode != 0:
        errors.append(f"consumer: must configure\n{result.stdout}")
    else:
        result = run(["cmake", "--build", str(build)])
        if result.returncode != 0:
            errors.append(f"consumer: must build\n{result.stdout}")

    for probe, header in COMPILE_REJECTED.items():
        build = build_root / probe
        result = configure(build, probe)
        if result.returncode != 0:
            errors.append(f"{probe}: must configure\n{result.stdout}")
            continue
        result = run(["cmake", "--build", str(build)])
        if result.returncode == 0 or header not in result.stdout:
            errors.append(f"{probe}: must fail to compile on {header}\n{result.stdout}")

    build = build_root / "no_provider"
    result = configure(build, "no_provider")
    if result.returncode != 0:
        errors.append(f"no_provider: must configure\n{result.stdout}")
    else:
        result = run(["cmake", "--build", str(build)])
        if result.returncode == 0 or "crux_access_port_event" not in result.stdout:
            errors.append(f"no_provider: must fail to link on the event port\n{result.stdout}")

    build = build_root / "composition"
    result = configure(build, "composition")
    if result.returncode != 0:
        errors.append(f"composition: must configure\n{result.stdout}")
    else:
        result = run(["cmake", "--build", str(build)])
        if result.returncode != 0:
            errors.append(f"composition: must link with the provider as objects\n{result.stdout}")
        else:
            program = next(build.rglob("security_event_composition"), None)
            result = run([str(program)]) if program else None
            if result is None or result.returncode != 0:
                errors.append(f"composition: must run\n{result.stdout if result else 'not built'}")
    return errors


def composition_errors(build_root):
    errors = []
    core = next((build_root / "consumer").rglob("libcrux_security_event_core.a"), None)
    if core is None:
        return ["the product core library was not built"]
    symbols = run(["nm", "--defined-only", str(core)]).stdout
    for forbidden in ("crux_security_event_test_", "security_event_fake_", "crux_access_port_"):
        if forbidden in symbols:
            errors.append(f"product core defines {forbidden}*")
    # The fake refuses to compile outside the test composition.
    result = run([C_COMPILER, "-fsyntax-only", "-Icomponents/security_event/include",
                  "-Icomponents/security_event/src", "-Icomponents/base/include",
                  "-Icomponents/access/include", "-Icomponents/logging/include",
                  "-Icomponents/sync/include", "-Itests/security_event/fake",
                  "tests/security_event/fake/security_event_fake.c"])
    if result.returncode == 0 or "test composition" not in result.stdout:
        errors.append(f"the fake must refuse a build without the test definition\n{result.stdout}")
    root = (REPO / "CMakeLists.txt").read_text(encoding="utf-8")
    if not re.search(r"if\(CRUX_BUILD_TESTS\).*add_subdirectory\(tests/security_event\)", root,
                     re.DOTALL):
        errors.append("the root must add tests/security_event only under CRUX_BUILD_TESTS")
    return errors


def setting_errors():
    """A reserve that leaves routine events no room must not compile."""
    errors = []
    base = [C_COMPILER, "-fsyntax-only", "-Icomponents/security_event/include",
            "-Icomponents/security_event/src", "-Icomponents/base/include",
            "-Icomponents/access/include", "-Icomponents/logging/include",
            "-Icomponents/sync/include", "-Icomponents/interrupt/src",
            "components/security_event/src/security_event_fields.c"]
    bad = [("-DCRUX_SECURITY_EVENT_RING=8", "-DCRUX_SECURITY_EVENT_RESERVE=8"),
           ("-DCRUX_SECURITY_EVENT_RING=8", "-DCRUX_SECURITY_EVENT_RESERVE=0"),
           ("-DCRUX_SECURITY_EVENT_RING=1", "-DCRUX_SECURITY_EVENT_RESERVE=1")]
    for settings in bad:
        result = run(base[:1] + list(settings) + base[1:])
        if result.returncode == 0:
            errors.append(f"settings {settings} must not compile")
    result = run(base[:1] + ["-DCRUX_SECURITY_EVENT_RING=8", "-DCRUX_SECURITY_EVENT_RESERVE=7"]
                 + base[1:])
    if result.returncode != 0:
        errors.append(f"a reserve one below the ring must compile\n{result.stdout}")
    return errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=REPO / "build/security-event-boundary")
    args = parser.parse_args()
    shutil.rmtree(args.build_dir, ignore_errors=True)
    errors = header_errors()
    errors += source_errors()
    errors += probe_errors(args.build_dir)
    errors += composition_errors(args.build_dir)
    errors += setting_errors()
    for error in errors:
        print(error, file=sys.stderr)
    if errors:
        print(f"Security event boundary check failed: {len(errors)} problem(s).", file=sys.stderr)
        return 1
    print("Security event boundary check passed: public header free of native types, private "
          "headers unreachable, provider required and sufficient, fake not selectable, no storage "
          "or direct log call on the emit path.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
