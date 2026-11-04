#!/usr/bin/env python3
"""Check the Access boundaries that its target layout cannot enforce alone.

- The adapter header cannot be reached from a consumer target, while a service that links the adapter
  target privately stays accepted. A rule by target name could not tell those apart, so the rule in
  cmake/CruxAccessBoundary.cmake follows include directories, and these probes test it both ways.
- The Access public and adapter headers contain no native, TLS or SSH types.
- The fake ports are a test composition that a product image cannot select.
"""
import argparse
from pathlib import Path
import re
import runpy
import shutil
import subprocess
import sys

REPO = Path(__file__).resolve().parents[1]
ACCESS = REPO / "components/access"
PUBLIC_HEADERS = (ACCESS / "include/crux/access.h", ACCESS / "adapter/crux_access_adapter.h")
ALLOWED_INCLUDES = {"stdbool.h", "stddef.h", "stdint.h", "crux/access.h"}
PROVIDER_TOKEN = re.compile(
    r"\b(?:wolf\w*|WOLF\w*|mbedtls\w*|MBEDTLS\w*|psa_\w+|PSA_\w+|ssl_\w+|SSL_\w+|WS_\w+|tls_\w+|TLS_\w+)\b")
# Reachable through crux::access alone in a project: nothing here may build.
CONFIGURE_REJECTED = ("public_service", "interface_directory", "parent_directory", "compile_option",
                      "direct_interface", "direct_directory", "direct_link")
COMPILE_REJECTED = {
    "include_adapter": "crux_access_adapter.h",
    "include_port": "access_port.h",
    "private_service_include": "crux_access_adapter.h",
}
ACCEPTED = ("consumer", "private_service")


def run(command):
    return subprocess.run(command, cwd=REPO, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                          text=True, check=False)


def configure(build, probe):
    return run(["cmake", "-S", "tests/access_consumer", "-B", str(build), "-G", "Ninja",
                "-DCMAKE_BUILD_TYPE=Debug", f"-DACCESS_PROBE={probe}"])


def header_errors():
    native = runpy.run_path(str(Path(__file__).with_name("check-native-interfaces.py")))
    errors = []
    for path in PUBLIC_HEADERS:
        for line, reason in native["violations"](path, public=True, networking=False):
            errors.append(f"{path.relative_to(REPO)}:{line}: {reason}")
        text = native["LEXEME"].sub(native["mask"], path.read_text(encoding="utf-8"))
        for match in re.finditer(r'^\s*#\s*include\s*[<"]([^>"\n]+)[>"]',
                                 path.read_text(encoding="utf-8"), re.MULTILINE):
            if match.group(1) not in ALLOWED_INCLUDES:
                errors.append(f"{path.relative_to(REPO)}: unexpected include {match.group(1)}")
        for match in PROVIDER_TOKEN.finditer(text):
            errors.append(f"{path.relative_to(REPO)}: provider token {match.group()}")
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
    for probe in CONFIGURE_REJECTED:
        result = configure(build_root / probe, probe)
        if result.returncode == 0 or "Access adapter header reachable" not in result.stdout:
            errors.append(f"{probe}: must be rejected by the boundary rule\n{result.stdout}")
    for probe, header in COMPILE_REJECTED.items():
        build = build_root / probe
        result = configure(build, probe)
        if result.returncode != 0:
            errors.append(f"{probe}: must configure\n{result.stdout}")
            continue
        result = run(["cmake", "--build", str(build)])
        if result.returncode == 0 or f"{header}" not in result.stdout:
            errors.append(f"{probe}: must fail to compile on {header}\n{result.stdout}")
    build = build_root / "no_provider"
    result = configure(build, "no_provider")
    if result.returncode != 0:
        errors.append(f"no_provider: must configure\n{result.stdout}")
    else:
        result = run(["cmake", "--build", str(build)])
        if result.returncode == 0 or "crux_access_port_" not in result.stdout:
            errors.append(f"no_provider: must fail to link on the ports\n{result.stdout}")
    return errors


def composition_errors(build_root):
    errors = []
    build = build_root / "consumer"
    # The product core holds neither the test hooks nor any port implementation.
    core = next(build.rglob("libcrux_access_core.a"), None)
    if core is None:
        return ["the product core library was not built"]
    symbols = run(["nm", "--defined-only", str(core)]).stdout
    for forbidden in ("crux_access_test_", "crux_access_port_", "access_fake_"):
        if forbidden in symbols:
            errors.append(f"product core defines {forbidden}*")
    # The fake refuses to compile outside the test composition.
    result = run([shutil.which("cc") or "cc", "-fsyntax-only", "-Icomponents/access/include",
                  "-Icomponents/access/adapter", "-Icomponents/access/src", "-Itests/access",
                  "tests/access/fake_ports.c"])
    if result.returncode == 0 or "test composition" not in result.stdout:
        errors.append(f"the fake ports must refuse a build without the test definition\n{result.stdout}")
    # Only tests/access names the test definitions or the fake, and only the tests option adds it.
    for path in sorted(ACCESS.rglob("*")):
        if path.is_file() and path.suffix in {".c", ".h", ".txt", ".cmake"}:
            text = path.read_text(encoding="utf-8")
            if "CRUX_ACCESS_TEST_COMPOSITION" in text or "fake_ports" in text:
                errors.append(f"{path.relative_to(REPO)} names the test composition")
            if re.search(r"\b(?:printf|fprintf|puts|LOG_\w+|crux_log\w*)\s*\(", text):
                errors.append(f"{path.relative_to(REPO)} logs directly; events use the port")
    root = (REPO / "CMakeLists.txt").read_text(encoding="utf-8")
    if not re.search(r"if\(CRUX_BUILD_TESTS\).*add_subdirectory\(tests/access\)", root, re.DOTALL):
        errors.append("the root must add tests/access only under CRUX_BUILD_TESTS")
    return errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=REPO / "build/access-boundary")
    args = parser.parse_args()
    shutil.rmtree(args.build_dir, ignore_errors=True)
    errors = header_errors()
    errors += probe_errors(args.build_dir)
    errors += composition_errors(args.build_dir)
    for error in errors:
        print(error, file=sys.stderr)
    if errors:
        print(f"Access boundary check failed: {len(errors)} problem(s).", file=sys.stderr)
        return 1
    print("Access boundary check passed: adapter header unreachable from consumers, "
          "public headers free of native and provider types, fake ports not selectable.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
