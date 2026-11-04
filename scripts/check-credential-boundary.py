#!/usr/bin/env python3
"""Check the credential store boundaries that its target layout cannot enforce alone.

- The adapter header cannot be reached from a consumer target, while a service that links the adapter
  target privately stays accepted. The rule in cmake/CruxCredentialBoundary.cmake follows include
  directories, and these probes test it both ways.
- The public and adapter headers contain no native, crypto, TLS or SSH types.
- The fake ports are test only: they refuse to compile outside a test composition, a product core
  neither defines them nor holds a test hook, and the product core refuses them at start. The
  credential_production_refuses suite shows that last point when it runs.
- The component does not log directly, so a secret cannot reach a log except through its event port.
"""
import argparse
from pathlib import Path
import re
import runpy
import shutil
import subprocess
import sys

REPO = Path(__file__).resolve().parents[1]
COMPONENT = REPO / "components/credential"
PUBLIC_HEADERS = (COMPONENT / "include/crux/credential_store.h",
                  COMPONENT / "adapter/crux_credential_adapter.h")
ALLOWED_INCLUDES = {"stdbool.h", "stddef.h", "stdint.h", "crux/credential_store.h", "crux/status.h",
                    "crux/file.h"}
PROVIDER_TOKEN = re.compile(
    r"\b(?:wolf\w*|WOLF\w*|mbedtls\w*|MBEDTLS\w*|psa_\w+|PSA_\w+|ssl_\w+|SSL_\w+|WS_\w+|tls_\w+|"
    r"hwinfo_\w+)\b")
# Reachable through crux::credential_store alone in a project: nothing here may build.
CONFIGURE_REJECTED = ("public_service", "interface_directory", "parent_directory", "compile_option",
                      "direct_interface", "direct_directory", "direct_link")
COMPILE_REJECTED = {
    "include_adapter": "crux_credential_adapter.h",
    "include_port": "credential_port.h",
    "private_service_include": "crux_credential_adapter.h",
}
ACCEPTED = ("consumer", "private_service")


def run(command):
    return subprocess.run(command, cwd=REPO, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                          text=True, check=False)


def configure(build, probe):
    return run(["cmake", "-S", "tests/credential_consumer", "-B", str(build), "-G", "Ninja",
                "-DCMAKE_BUILD_TYPE=Debug", f"-DCREDENTIAL_PROBE={probe}"])


def header_errors():
    native = runpy.run_path(str(Path(__file__).with_name("check-native-interfaces.py")))
    errors = []
    for path in PUBLIC_HEADERS:
        for line, reason in native["violations"](path, public=True, networking=False):
            errors.append(f"{path.relative_to(REPO)}:{line}: {reason}")
        original = path.read_text(encoding="utf-8")
        text = native["LEXEME"].sub(native["mask"], original)
        for match in re.finditer(r'^\s*#\s*include\s*[<"]([^>"\n]+)[>"]', original, re.MULTILINE):
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
        if result.returncode == 0 or "Credential adapter header reachable" not in result.stdout:
            errors.append(f"{probe}: must be rejected by the boundary rule\n{result.stdout}")
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
        if result.returncode == 0 or "crux_credential_port_" not in result.stdout:
            errors.append(f"no_provider: must fail to link on the ports\n{result.stdout}")
    return errors


def composition_errors(build_root):
    errors = []
    build = build_root / "consumer"
    # The product core holds neither the test hooks nor any port implementation.
    core = next(build.rglob("libcrux_credential_core.a"), None)
    if core is None:
        return ["the product core library was not built"]
    symbols = run(["nm", "--defined-only", str(core)]).stdout
    for forbidden in ("crux_credential_test_", "crux_credential_port_", "credential_fake_"):
        if forbidden in symbols:
            errors.append(f"product core defines {forbidden}*")
    # The fake refuses to compile outside the test composition.
    result = run([shutil.which("cc") or "cc", "-fsyntax-only", "-Icomponents/credential/include",
                  "-Icomponents/credential/adapter", "-Icomponents/credential/src",
                  "-Icomponents/file/include", "-Icomponents/base/include", "-Itests/credential",
                  "tests/credential/fake_ports.c"])
    if result.returncode == 0 or "test composition" not in result.stdout:
        errors.append(f"the fake ports must refuse a build without the test definition\n{result.stdout}")
    # The component names its test definition only where a product build refuses the fake.
    named = []
    for path in sorted(COMPONENT.rglob("*")):
        if path.is_file() and path.suffix in {".c", ".h", ".txt", ".cmake"}:
            text = path.read_text(encoding="utf-8")
            if "fake_ports" in text:
                errors.append(f"{path.relative_to(REPO)} names the fake ports")
            named += [path] * len(re.findall(r"CRUX_CREDENTIAL_TEST_COMPOSITION", text))
            if re.search(r"\b(?:printf|fprintf|puts|LOG_\w+|crux_log\w*)\s*\(", text):
                errors.append(f"{path.relative_to(REPO)} logs directly; events use the port")
    store = COMPONENT / "src/credential_store.c"
    if named != [store] or not re.search(r"#ifndef CRUX_CREDENTIAL_TEST_COMPOSITION\n",
                                         store.read_text(encoding="utf-8")):
        errors.append("only the product refusal in credential_store.c may name the test composition")
    root = (REPO / "CMakeLists.txt").read_text(encoding="utf-8")
    if not re.search(r"if\(CRUX_BUILD_TESTS\).*add_subdirectory\(tests/credential\)", root, re.DOTALL):
        errors.append("the root must add tests/credential only under CRUX_BUILD_TESTS")
    return errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=REPO / "build/credential-boundary")
    args = parser.parse_args()
    shutil.rmtree(args.build_dir, ignore_errors=True)
    errors = header_errors()
    errors += probe_errors(args.build_dir)
    errors += composition_errors(args.build_dir)
    for error in errors:
        print(error, file=sys.stderr)
    if errors:
        print(f"Credential boundary check failed: {len(errors)} problem(s).", file=sys.stderr)
        return 1
    print("Credential boundary check passed: adapter header unreachable from consumers, "
          "public headers free of native and provider types, fake ports not selectable.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
