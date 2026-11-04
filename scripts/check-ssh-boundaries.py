#!/usr/bin/env python3
"""Check the SSH provider boundaries that its target layout cannot enforce alone.

- SSH has no public header or crux:: target, and only Crux adapters may reach the private interface.
- The private interface, the fake and the conformance cases name no SSH library, native or access type.
  The fake and the cases must build wherever the CLI and SFTP Host tests build.
- The fake exists only when Crux builds its own tests.
- Keeping wolfSSH out of ordinary workspaces: no default workflow selects the secure management stacks,
  and the settings arrangement never uses the option that the TLS socket backend rejects.

This is a bounded lexical check. Review remains necessary.
"""
import argparse
from pathlib import Path
import re
import runpy
import sys

PROVIDER_HEADER = "components/ssh/src/crux_ssh_provider.h"
ATTACHMENT_HEADER = "components/ssh/adapter/crux_ssh_attachment.h"
# Files that hold no type of the SSH library, the OS or, unless listed below, access control.
NATIVE_FREE_FILES = (
    PROVIDER_HEADER,
    ATTACHMENT_HEADER,
    "components/ssh/src/crux_ssh_callbacks.h",
    "components/ssh/src/crux_ssh_callbacks.c",
    "tests/ssh/fake_attachment.h",
    "tests/ssh/fake_attachment.c",
    "tests/ssh/ssh_test_client.h",
    "tests/ssh/fake_provider.h",
    "tests/ssh/fake_provider.c",
    "tests/ssh/conformance.h",
    "tests/ssh/conformance.c",
)
ALLOWED_HEADER_INCLUDES = {"stdbool.h", "stddef.h", "stdint.h", "crux/status.h"}
ALLOWED_ATTACHMENT_INCLUDES = {"crux_ssh_provider.h", "crux/access.h"}
# The accessor names the access session, which is its purpose, and the provider interface must not.
ACCESS_ALLOWED = {ATTACHMENT_HEADER, "tests/ssh/fake_attachment.h", "tests/ssh/fake_attachment.c"}
# Forbidden in code, comments excluded. The socket address types are here because the native policy
# allows them for the networking profile. The access types stay out so that #192 alone connects them.
FORBIDDEN_TOKEN = re.compile(
    r"\b(?:wolf\w*|WOLF\w*|WS_\w+|WFD|WFILE|byte|word32|word64|mbedtls\w*|MBEDTLS\w*|psa_\w+|PSA_\w+|"
    r"crux_cli\w*|sockaddr\w*|net_sockaddr\w*|socklen_t|in_addr\w*|in6_addr\w*)\b")
ACCESS_TOKEN = re.compile(r"\b(?:crux_access\w*|CRUX_ACCESS\w*)\b")
INCLUDE = re.compile(r'^\s*#\s*include\s*[<"]([^>"\n]+)[>"]', re.MULTILINE)
# Places that a project or a portable consumer builds from. A project never includes the interface.
CONSUMER_ROOTS = ("samples", "applications", "runtime", "tests/external_application")
SOURCE_SUFFIXES = {".c", ".h", ".cc", ".cpp", ".cxx", ".hpp", ".hh", ".hxx", ".inc"}
SSH_WOLF_MARKERS = ("crux-secure-management", "ssh_wolf_probe", "test-ssh-wolf-settings")


def relative(root, path):
    return path.relative_to(root).as_posix()


def native_free_errors(root):
    native = runpy.run_path(str(Path(__file__).with_name("check-native-interfaces.py")))
    errors = []
    for name in NATIVE_FREE_FILES:
        path = root / name
        if not path.is_file():
            errors.append(f"{name}: missing")
            continue
        for line, reason in native["violations"](path, public=name == PROVIDER_HEADER, networking=False):
            errors.append(f"{name}:{line}: {reason}")
        source = path.read_text(encoding="utf-8")
        code = native["LEXEME"].sub(native["mask"], source)
        for match in FORBIDDEN_TOKEN.finditer(code):
            errors.append(f"{name}: forbidden type or name {match.group()}")
        if name not in ACCESS_ALLOWED:
            for match in ACCESS_TOKEN.finditer(code):
                errors.append(f"{name}: forbidden type or name {match.group()}")
    for name, allowed in ((PROVIDER_HEADER, ALLOWED_HEADER_INCLUDES),
                          (ATTACHMENT_HEADER, ALLOWED_ATTACHMENT_INCLUDES)):
        source = (root / name).read_text(encoding="utf-8") if (root / name).is_file() else ""
        for include in INCLUDE.findall(source):
            if include not in allowed:
                errors.append(f"{name}: unexpected include {include}")
    return errors


def layout_errors(root):
    errors = []
    if (root / "components/ssh/include").exists():
        errors.append("components/ssh/include: SSH must have no public header")
    build = root / "components/ssh/CMakeLists.txt"
    if build.is_file() and re.search(r"crux::ssh\b", build.read_text(encoding="utf-8")):
        errors.append("components/ssh/CMakeLists.txt: SSH must have no crux:: target")
    top = root / "CMakeLists.txt"
    if top.is_file():
        text = top.read_text(encoding="utf-8")
        match = re.search(r"set\(_crux_components ([^)]*)\)", text)
        if match and re.search(r"\bssh\b", match.group(1)):
            errors.append("CMakeLists.txt: ssh must not join the public component list")
        tests = re.search(r"if\(CRUX_BUILD_TESTS\)\n(?:.*\n)*?endif\(\)\n\ninclude\(cmake", text)
        if "add_subdirectory(tests/ssh)" not in (tests.group(0) if tests else ""):
            errors.append("CMakeLists.txt: tests/ssh must be added only under CRUX_BUILD_TESTS")
    return errors


def consumer_errors(root):
    errors = []
    for name in CONSUMER_ROOTS:
        base = root / name
        if not base.is_dir():
            continue
        for path in sorted(base.rglob("*")):
            if path.suffix in SOURCE_SUFFIXES and path.is_file():
                text = path.read_text(encoding="utf-8", errors="replace")
                if "crux_ssh_provider.h" in text or "crux_ssh_attachment.h" in text:
                    errors.append(f"{relative(root, path)}: a consumer must not include the interface")
            elif path.name == "CMakeLists.txt" and "crux_ssh_" in path.read_text(encoding="utf-8"):
                errors.append(f"{relative(root, path)}: a consumer must not link an SSH target")
    # A usage target that passes the interface on would let its include directory reach a consumer.
    for build in sorted((root / "components").glob("*/CMakeLists.txt")):
        if build.parent.name == "ssh":
            continue
        for match in re.finditer(r"target_link_libraries\([^)]*\)", build.read_text(encoding="utf-8")):
            if "crux_ssh_" in match.group() and re.search(r"\b(?:PUBLIC|INTERFACE)\b", match.group()):
                errors.append(f"{relative(root, build)}: link SSH targets PRIVATE")
    return errors


def stack_errors(root):
    errors = []
    conf = root / "components/ssh/ports/zephyr/wolf-ssh.conf"
    if conf.is_file() and re.search(r"^\s*CONFIG_WOLFSSL_SETTINGS_FILE\s*=", conf.read_text(encoding="utf-8"),
                                    re.MULTILINE):
        errors.append(f"{relative(root, conf)}: the TLS socket backend rejects a wolfSSL settings file")
    manifest = root / "west.yml"
    if manifest.is_file() and "-crux-secure-management" not in manifest.read_text(encoding="utf-8"):
        errors.append("west.yml: the secure management group must stay off by default")
    workflows = root / ".github/workflows"
    if workflows.is_dir():
        for path in sorted(workflows.iterdir()):
            if path.is_file():
                text = path.read_text(encoding="utf-8")
                for marker in SSH_WOLF_MARKERS:
                    if marker in text:
                        errors.append(f"{relative(root, path)}: default workflow names {marker}")
    return errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    root = parser.parse_args().root.resolve()
    errors = native_free_errors(root) + layout_errors(root) + consumer_errors(root) + stack_errors(root)
    for error in errors:
        print(error, file=sys.stderr)
    if errors:
        return 1
    print("SSH provider boundaries hold.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
