#!/usr/bin/env python3
"""Check the command line's boundaries that its target layout cannot show alone.

- A project reaches its tables and handlers, and cannot reach the adapter header or the event port.
- A channel adapter that links crux_cli_adapter privately stays accepted, and exposes nothing.
- A command with no access class does not compile, and a line limit too short does not build.
- The public, adapter and console headers contain no native, TLS or SSH type, and the adapter header
  names no serial type, so serial and SSH are two users of one transport.
- The fakes are a test composition that a product image cannot select, and the product core defines
  no port, so an image links only once a composition supplies one.
"""
import argparse
from pathlib import Path
import re
import runpy
import shutil
import subprocess
import sys

REPO = Path(__file__).resolve().parents[1]
CLI = REPO / "components/cli"
PUBLIC_HEADERS = {
    CLI / "include/crux/cli.h": {"stddef.h", "stdint.h", "crux/access.h"},
    CLI / "adapter/crux_cli_adapter.h": {"stdbool.h", "stddef.h", "stdint.h", "crux/cli.h",
                                         "crux_access_adapter.h"},
    CLI / "console/crux_cli_console.h": {"crux/serial.h", "crux_cli_adapter.h"},
}
PROVIDER_TOKEN = re.compile(
    r"\b(?:wolf\w*|WOLF\w*|mbedtls\w*|MBEDTLS\w*|psa_\w+|PSA_\w+|ssl_\w+|SSL_\w+|WS_\w+|tls_\w+|TLS_\w+)\b")
SERIAL_OR_SSH_TOKEN = re.compile(r"\bcrux_(?:serial|ssh)\w*")
ACCEPTED = ("consumer", "private_adapter")
COMPILE_REJECTED = {
    "include_adapter": "crux_cli_adapter.h",
    "include_port": "cli_port.h",
    "private_adapter_include": "crux_cli_adapter.h",
    "no_access_class": "CRUX_CLI_COMMAND",
}


def run(command):
    return subprocess.run(command, cwd=REPO, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                          text=True, check=False)


def configure(build, probe, *extra):
    return run(["cmake", "-S", "tests/cli_consumer", "-B", str(build), "-G", "Ninja",
                "-DCMAKE_BUILD_TYPE=Debug", f"-DCLI_PROBE={probe}", *extra])


def header_errors():
    native = runpy.run_path(str(Path(__file__).with_name("check-native-interfaces.py")))
    errors = []
    for path, allowed in PUBLIC_HEADERS.items():
        name = path.relative_to(REPO)
        for line, reason in native["violations"](path, public=True, networking=False):
            errors.append(f"{name}:{line}: {reason}")
        source = path.read_text(encoding="utf-8")
        text = native["LEXEME"].sub(native["mask"], source)
        for match in re.finditer(r'^\s*#\s*include\s*[<"]([^>"\n]+)[>"]', source, re.MULTILINE):
            if match.group(1) not in allowed:
                errors.append(f"{name}: unexpected include {match.group(1)}")
        for match in PROVIDER_TOKEN.finditer(text):
            errors.append(f"{name}: provider token {match.group()}")
    adapter = (CLI / "adapter/crux_cli_adapter.h").read_text(encoding="utf-8")
    text = native["LEXEME"].sub(native["mask"], adapter)
    for match in SERIAL_OR_SSH_TOKEN.finditer(text):
        errors.append(f"adapter header names a transport type: {match.group()}")
    return errors


def probe_errors(build_root):
    errors = []
    for probe in ACCEPTED:
        build = build_root / probe
        result = configure(build, probe)
        if result.returncode != 0:
            errors.append(f"{probe}: must configure\n{result.stdout}")
            continue
        result = run(["cmake", "--build", str(build)])
        if result.returncode != 0:
            errors.append(f"{probe}: must build\n{result.stdout}")
    for probe, needle in COMPILE_REJECTED.items():
        build = build_root / probe
        result = configure(build, probe)
        if result.returncode != 0:
            errors.append(f"{probe}: must configure\n{result.stdout}")
            continue
        result = run(["cmake", "--build", str(build)])
        if result.returncode == 0 or needle not in result.stdout:
            errors.append(f"{probe}: must fail to compile on {needle}\n{result.stdout}")
    build = build_root / "no_provider"
    result = configure(build, "no_provider")
    if result.returncode != 0:
        errors.append(f"no_provider: must configure\n{result.stdout}")
    else:
        result = run(["cmake", "--build", str(build)])
        if result.returncode == 0 or "crux_cli_port_event" not in result.stdout:
            errors.append(f"no_provider: must fail to link on the event port\n{result.stdout}")
    build = build_root / "line_too_short"
    result = configure(build, "line_too_short", "-DCRUX_CLI_LINE_MAX=100")
    if result.returncode != 0:
        errors.append(f"line_too_short: must configure\n{result.stdout}")
    else:
        result = run(["cmake", "--build", str(build)])
        if result.returncode == 0 or "CRUX_CLI_LINE_MAX cannot hold" not in result.stdout:
            errors.append(f"line_too_short: must fail on the build check\n{result.stdout}")
    return errors


def composition_errors(build_root):
    errors = []
    core = next((build_root / "consumer").rglob("libcrux_cli_core.a"), None)
    if core is None:
        return ["the product core library was not built"]
    symbols = run(["nm", "--defined-only", str(core)]).stdout
    for forbidden in ("crux_cli_test_", "crux_cli_port_", "cli_fake_", "crux_access_login"):
        if forbidden in symbols:
            errors.append(f"product core defines {forbidden}*")
    for fake in ("fake_events.c", "fake_access.c", "fake_serial.c"):
        result = run([shutil.which("cc") or "cc", "-fsyntax-only", "-Icomponents/cli/include",
                      "-Icomponents/cli/adapter", "-Icomponents/cli/src", "-Icomponents/access/include",
                      "-Icomponents/access/adapter", "-Icomponents/serial/include",
                      "-Icomponents/base/include", "-Itests/cli", f"tests/cli/{fake}"])
        if result.returncode == 0 or "test composition" not in result.stdout:
            errors.append(f"{fake} must refuse a build without the test definition\n{result.stdout}")
    for path in sorted(CLI.rglob("*")):
        if path.is_file() and path.suffix in {".c", ".h", ".txt", ".cmake"}:
            text = path.read_text(encoding="utf-8")
            if "CRUX_CLI_TEST_COMPOSITION" in text or "fake_access" in text or "fake_events" in text:
                errors.append(f"{path.relative_to(REPO)} names the test composition")
            if re.search(r"\b(?:printf|fprintf|puts|LOG_\w+|crux_log\w*)\s*\(", text):
                errors.append(f"{path.relative_to(REPO)} logs directly; events use the port")
    root = (REPO / "CMakeLists.txt").read_text(encoding="utf-8")
    if not re.search(r"if\(CRUX_BUILD_TESTS\).*add_subdirectory\(tests/cli\)", root, re.DOTALL):
        errors.append("the root must add tests/cli only under CRUX_BUILD_TESTS")
    return errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=REPO / "build/cli-boundary")
    args = parser.parse_args()
    shutil.rmtree(args.build_dir, ignore_errors=True)
    errors = header_errors()
    errors += probe_errors(args.build_dir)
    errors += composition_errors(args.build_dir)
    for error in errors:
        print(error, file=sys.stderr)
    if errors:
        print(f"CLI boundary check failed: {len(errors)} problem(s).", file=sys.stderr)
        return 1
    print("CLI boundary check passed: adapter and port unreachable from a project, access class "
          "mandatory, transport free of serial and SSH types, fakes not selectable.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
