#!/usr/bin/env python3
"""Check the Web boundaries that its target layout cannot enforce alone.

- The public and adapter headers hold no native or TLS type, and include only what they name.
- A route with no access class fails to compile, in every way of leaving it out.
- A project compiles routes as C17 and C++17 with no native header, under the consumer boundary
  checks, and cannot reach the adapter or the core's private headers.
- No fake exists in a build that a product could use, which that project checks when it configures.
- The Zephyr server is named only in the adapter, and the service has no fallback resource.
"""
from pathlib import Path
import re
import runpy
import os
import shutil
import subprocess
import sys
import tempfile

REPO = Path(__file__).resolve().parents[1]
COMPONENT = REPO / "components/web"
CONSUMER = REPO / "tests/web_consumer"
ALLOWED_INCLUDES = {
    COMPONENT / "include/crux/web.h": {"stdbool.h", "stddef.h", "crux/access.h"},
    COMPONENT / "adapter/crux_web_adapter.h": {"stdint.h", "crux/web.h"},
}
PROVIDER_TOKEN = re.compile(
    r"\b(?:wolf\w*|WOLF\w*|mbedtls\w*|MBEDTLS\w*|psa_\w+|PSA_\w+|ssl_\w+|SSL_\w+|tls_\w+|TLS_\w+|"
    r"zsock_\w+|http_\w+|HTTP_\w+|k_\w+)\b")
COMMENT = re.compile(r"/\*.*?\*/|//[^\n]*", re.DOTALL)
EXPECTED_LITERAL = r"expected ['\u2018]\)['\u2019]"
REJECTED = {
    "no_access": "requires 6 arguments|too few arguments",
    "empty_access": "a route needs an access class",
    "empty_literal": "a route needs an access class",
    # The access argument between empty literals is a syntax error, unless it is a string literal.
    "null_access": EXPECTED_LITERAL,
    "number_access": EXPECTED_LITERAL,
    "variable_access": EXPECTED_LITERAL,
    "no_methods": "a route needs known methods",
    "big_body": "body limit",
    "include_adapter": "crux_web_adapter.h",
    "include_internal": "web_internal.h",
}
C_COMPILER = os.environ.get("CC") or shutil.which("cc") or "cc"


def run(command):
    return subprocess.run(command, cwd=REPO, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                          text=True, check=False)


def header_errors():
    native = runpy.run_path(str(Path(__file__).with_name("check-native-interfaces.py")))
    errors = []
    for path, allowed in ALLOWED_INCLUDES.items():
        relative = path.relative_to(REPO)
        for line, reason in native["violations"](path, public=True, networking=False):
            errors.append(f"{relative}:{line}: {reason}")
        source = path.read_text(encoding="utf-8")
        for match in re.finditer(r'^\s*#\s*include\s*[<"]([^>"\n]+)[>"]', source, re.MULTILINE):
            if match.group(1) not in allowed:
                errors.append(f"{relative}: unexpected include {match.group(1)}")
        for match in PROVIDER_TOKEN.finditer(COMMENT.sub("", source)):
            errors.append(f"{relative}: native token {match.group()}")
    return errors


def source_errors():
    errors = []
    for path in sorted((COMPONENT / "src").glob("*")):
        for match in PROVIDER_TOKEN.finditer(COMMENT.sub("", path.read_text(encoding="utf-8"))):
            errors.append(f"{path.relative_to(REPO)}: native token {match.group()}")
    adapter = (COMPONENT / "ports/zephyr/web_http_server.c").read_text(encoding="utf-8")
    code = COMMENT.sub("", adapter)
    # Arguments 5 to 7 are the detail, the fallback resource and the configuration.
    services = re.findall(r"HTTPS?_SERVICE_DEFINE\(crux_web_service,([^;]*)\);", code, re.DOTALL)
    if len(services) != 2:
        errors.append("expected one HTTPS service and one for native_sim tests")
    for service in services:
        arguments = [argument.strip() for argument in service.split(",")]
        if arguments[4:7] != ["NULL", "NULL", "NULL"]:
            errors.append(f"the service must have no fallback resource: {arguments[4:7]}")
    if re.search(r"\bEAGAIN\b", code):
        errors.append("a close must not use EAGAIN, with which the server keeps the connection")
    return errors


def compile_probe(source):
    with tempfile.TemporaryDirectory() as directory:
        return run([C_COMPILER, "-std=c17", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
                    "-fsyntax-only", f"-I{COMPONENT / 'include'}",
                    f"-I{REPO / 'components/access/include'}",
                    f"-DCRUX_WEB_PROBE_DIRECTORY=\"{directory}\"", str(source)])


def rejected_errors():
    errors = []
    positive = compile_probe(CONSUMER / "consumer.c")
    if positive.returncode != 0:
        errors.append(f"the control must compile:\n{positive.stdout}")
    for name, expected in REJECTED.items():
        result = compile_probe(CONSUMER / "rejected" / f"{name}.c")
        if result.returncode == 0:
            errors.append(f"{name} compiled, and it must not")
        elif not re.search(expected, result.stdout):
            errors.append(f"{name} failed for another reason:\n{result.stdout}")
    return errors


def consumer_errors():
    with tempfile.TemporaryDirectory() as build:
        configure = run(["cmake", "-S", str(CONSUMER), "-B", build, "-G", "Ninja",
                         "-DCMAKE_BUILD_TYPE=Debug", "-DFETCHCONTENT_FULLY_DISCONNECTED=ON"])
        if configure.returncode != 0:
            return [f"the consumer did not configure:\n{configure.stdout}"]
        built = run(["cmake", "--build", build, "--target", "web_consumer_c", "web_consumer_cpp"])
        if built.returncode != 0:
            return [f"the consumer did not build:\n{built.stdout}"]
    return []


def main():
    errors = header_errors() + source_errors() + rejected_errors() + consumer_errors()
    for error in errors:
        print(error, file=sys.stderr)
    if errors:
        return 1
    print(f"Web boundary: {len(REJECTED)} rejected probes, consumer built, no native token in the core")
    return 0


if __name__ == "__main__":
    sys.exit(main())
