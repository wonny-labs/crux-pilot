#!/usr/bin/env python3
"""Every thread that Crux's services create must be placed by its per-service priority (ADR-0002 P3).

The native_sim suite in tests/service_priority/zephyr reads the priorities back from the kernel, from a
worker that calls the placement as a service worker does. It cannot start the services themselves,
which need their whole composition, and the SSH provider needs wolfSSH. This check covers that part
statically: which entry calls which identifier, the Kconfig defaults, the map from identifier to
symbol, the SSH provider's two thread priorities, that no service creates a thread outside this
list, and that every Zephyr CMake file that compiles a worker links the placement's interface. It is a lexical check of the sources as they are written, not a C parser.
"""
import argparse
import re
import sys
from pathlib import Path

# Source, placement identifier, Kconfig symbol. Each source creates exactly one Crux Thread worker.
WORKERS = (
    ("components/security_event/src/security_event_drain.c",
     "CRUX_SERVICE_THREAD_SECURITY_EVENT_DRAIN", "CRUX_SECURITY_EVENT_DRAIN_PRIORITY"),
    ("components/reboot/src/reboot.c",
     "CRUX_SERVICE_THREAD_REBOOT_WORKER", "CRUX_REBOOT_WORKER_PRIORITY"),
    ("components/network_settings/src/network_settings.c",
     "CRUX_SERVICE_THREAD_NETWORK_SETTINGS_WORKER", "CRUX_NETWORK_SETTINGS_WORKER_PRIORITY"),
    ("components/web/session/web_session_attempt.c",
     "CRUX_SERVICE_THREAD_WEB_SESSION_WORKER", "CRUX_WEB_SESSION_WORKER_PRIORITY"),
    ("components/web/management/web_management_save.c",
     "CRUX_SERVICE_THREAD_WEB_MANAGEMENT_WORKER", "CRUX_WEB_MANAGEMENT_WORKER_PRIORITY"),
)
# Today's priority of a LOW-class Crux Thread worker on Zephyr: 6 - CRUX_THREAD_PRIORITY_LOW.
WORKER_DEFAULT = "6"
PLACEMENT = "components/service_priority/ports/zephyr/service_priority_zephyr.c"
SSH_PROVIDER = "components/ssh/ports/zephyr/crux_ssh_wolf_provider.c"
SSH_SETTINGS = "components/ssh/ports/zephyr/wolf_ssh_settings.cmake"
# Provider macro, Kconfig symbol, the thread's stack in the k_thread_create call that uses it.
SSH_THREADS = (
    ("CRUX_SSH_WOLF_LISTENER_PRIORITY", "CRUX_SSH_LISTENER_PRIORITY", "listener_stack"),
    ("CRUX_SSH_WOLF_WORKER_PRIORITY", "CRUX_SSH_WORKER_PRIORITY", "worker_stacks"),
)
# -1 keeps today's K_LOWEST_APPLICATION_THREAD_PRIO, whatever CONFIG_NUM_PREEMPT_PRIORITIES is.
SSH_DEFAULT = "-1"
# Where Crux creates threads on purpose: the Thread port itself, and the SSH provider.
THREAD_CREATORS = ("components/thread/", SSH_PROVIDER)

COMMENT = re.compile(r"/\*.*?\*/|//[^\n]*", re.S)


def strip_comments(text):
    return COMMENT.sub(lambda match: re.sub(r"[^\n]", " ", match.group()), text)


def function_body(text, name):
    match = re.search(r"\bstatic\s+void\s+" + re.escape(name) + r"\s*\(\s*void\s*\*\s*\w*\s*\)\s*\{", text)
    if not match:
        return None
    depth, start = 1, match.end()
    for index in range(start, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[start:index]
    return None


def first_statement(body):
    for line in body.splitlines():
        line = line.strip()
        if not line or line.startswith("#") or re.fullmatch(r"\(void\)\s*\w+\s*;", line):
            continue
        return line
    return None


def kconfig_default(kconfig, symbol):
    match = re.search(r"^config\s+" + symbol + r"\s*\n((?:[ \t]+[^\n]*\n|\s*\n)*)", kconfig, re.M)
    if not match:
        return None
    default = re.search(r"^\s+default\s+(\S+)\s*$", match.group(1), re.M)
    return default.group(1) if default else ""


def worker_errors(root, kconfig, placement):
    errors = []
    for source, identifier, symbol in WORKERS:
        path = root / source
        if not path.is_file():
            errors.append(f"{source}: missing")
            continue
        text = strip_comments(path.read_text())
        entries = re.findall(r"\bcrux_thread_create\s*\([^,]+,\s*(\w+)\s*,", text)
        if len(entries) != 1:
            errors.append(f"{source}: expected one crux_thread_create, found {len(entries)}")
            continue
        body = function_body(text, entries[0])
        if body is None:
            errors.append(f"{source}: entry {entries[0]} not found")
            continue
        statement = first_statement(body)
        wanted = f"crux_service_priority_place({identifier});"
        if statement != wanted:
            errors.append(f"{source}: {entries[0]} must begin with {wanted}, not {statement}")
        if not re.search(r"#ifdef\s+CRUX_SERVICE_PRIORITY_PLACEMENT\s*\n\s*" + re.escape(wanted), body):
            errors.append(f"{source}: the placement must be under #ifdef CRUX_SERVICE_PRIORITY_PLACEMENT")
        default = kconfig_default(kconfig, symbol)
        if default != WORKER_DEFAULT:
            errors.append(f"zephyr/Kconfig: {symbol} must default to {WORKER_DEFAULT}, today's priority, "
                          f"not {default}")
        if not re.search(r"\[\s*" + identifier + r"\s*\]\s*=\s*CONFIG_" + symbol + r"\b", placement):
            errors.append(f"{PLACEMENT}: {identifier} must map to CONFIG_{symbol}")
    return errors


def ssh_errors(root, kconfig):
    errors = []
    provider = strip_comments((root / SSH_PROVIDER).read_text())
    settings = (root / SSH_SETTINGS).read_text()
    for macro, symbol, stack in SSH_THREADS:
        calls = [call for call in re.findall(r"\bk_thread_create\s*\(([^;]*)\)\s*;", provider) if stack in call]
        if len(calls) != 1 or not re.search(r"\b" + macro + r"\b", calls[0]):
            errors.append(f"{SSH_PROVIDER}: the thread on {stack} must be created at {macro}")
        if not re.search(r"\b" + macro + r"=\$\{CONFIG_" + symbol + r"\}", settings):
            errors.append(f"{SSH_SETTINGS}: {macro} must come from CONFIG_{symbol}")
        default = kconfig_default(kconfig, symbol)
        if default != SSH_DEFAULT:
            errors.append(f"zephyr/Kconfig: {symbol} must default to {SSH_DEFAULT}, the lowest application "
                          f"priority as today, not {default}")
    return errors


def creator_errors(root):
    errors = []
    workers = {source for source, _, _ in WORKERS}
    paths = sorted((root / "components").rglob("*.c")) + sorted((root / "zephyr").rglob("*.c"))
    for path in paths:
        relative = path.relative_to(root).as_posix()
        if "/tests/" in relative or relative.startswith(THREAD_CREATORS[0]):
            continue
        text = strip_comments(path.read_text())
        if re.search(r"\bcrux_thread_create\s*\(", text) and relative not in workers:
            errors.append(f"{relative}: creates a Crux Thread worker that has no per-service priority: "
                          "add it to WORKERS, zephyr/Kconfig and the placement")
        if re.search(r"\b(?:k_thread_create|K_THREAD_DEFINE)\b", text) and relative != SSH_PROVIDER:
            errors.append(f"{relative}: creates a native thread that has no per-service priority")
    return errors


# How a CMake file names a worker source: the file's name, or a list variable that holds it.
WORKER_SOURCE_NAMES = tuple(Path(source).name for source, _, _ in WORKERS) + (
    "CRUX_SECURITY_EVENT_SOURCES", "CRUX_REBOOT_SOURCES", "CRUX_NETWORK_SETTINGS_SOURCES",
    "CRUX_WEB_SESSION_SOURCES", "CRUX_WEB_MANAGEMENT_SOURCES")
PLACEMENT_TARGET = "crux_service_priority_placement"
SKIPPED_DIRS = {"build", ".git", "twister-out"}


def is_zephyr_cmake(relative, text):
    """A Zephyr build: an application, a file under a Zephyr test or the module, or one that adds to app."""
    return (re.search(r"find_package\s*\(\s*Zephyr", text) is not None
            or relative.startswith(("tests/native/", "tests/zephyr/", "zephyr/"))
            or "/zephyr/" in relative
            or re.search(r"\btarget_sources\s*\(\s*app\b", text) is not None)


def wiring_errors(root):
    """Each Zephyr CMake file that compiles a worker source must link the placement's interface.

    The files that only define a source list (a component's sources.cmake, or a component's Host
    CMakeLists.txt that sets the list) compile nothing for Zephyr, and the Host builds call nothing.
    """
    errors = []
    for path in sorted(root.rglob("*")):
        if path.name != "CMakeLists.txt" and path.suffix != ".cmake":
            continue
        relative = path.relative_to(root).as_posix()
        if SKIPPED_DIRS.intersection(path.relative_to(root).parts) or relative.startswith("components/"):
            continue
        text = re.sub(r"#[^\n]*", "", path.read_text(errors="replace"))
        if not any(re.search(r"\b" + re.escape(name) + r"\b", text) for name in WORKER_SOURCE_NAMES):
            continue
        if not is_zephyr_cmake(relative, text):
            continue
        if not re.search(r"\btarget_link_libraries\s*\([^)]*\b" + PLACEMENT_TARGET + r"\b", text):
            errors.append(f"{relative}: compiles a service worker for Zephyr without linking {PLACEMENT_TARGET}, "
                          "so the worker would stay at its class's priority")
    return errors


def errors(root):
    kconfig = (root / "zephyr/Kconfig").read_text()
    placement_path = root / PLACEMENT
    placement = placement_path.read_text() if placement_path.is_file() else ""
    found = [] if placement else [f"{PLACEMENT}: missing"]
    return (found + worker_errors(root, kconfig, placement) + ssh_errors(root, kconfig) + creator_errors(root)
            + wiring_errors(root))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    args = parser.parse_args()
    found = errors(args.root)
    for error in found:
        print(error, file=sys.stderr)
    if not found:
        print(f"Per-service priorities: {len(WORKERS)} Crux Thread workers and {len(SSH_THREADS)} SSH threads placed.")
    return 1 if found else 0


if __name__ == "__main__":
    raise SystemExit(main())
