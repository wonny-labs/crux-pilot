#!/usr/bin/env python3
"""Check the maintained portable source inventory for native dependency leakage.

This is a bounded lexical policy, not a C parser or a proof of portability. It
checks direct includes and representative native token families, including code
in inactive preprocessor branches. It cannot resolve macro-generated includes,
aliases, transitive includes, or arbitrary native APIs. Build consumer probes and
review remain necessary. Add roots/token families with their regression cases.
"""
import argparse
from pathlib import Path
import re
import runpy


# Public interfaces and shared implementation are portable by default, including
# future runtime components. Providers belong outside these roots. Current
# samples/controller/{host,zephyr} are legacy native compositions/qualification
# fixtures pending migration, not portable consumers. Future application/runtime
# consumers have no such exemption: classify a new provider deliberately.
PORTABLE_ROOTS = (
    "components/*/include", "components/*/src", "components/*/portable",
    "components/*/runtime_porting", "applications", "runtime", "runtime_porting",
    "samples/controller/core", "samples/controller/include", "samples/time/include",
    "samples/time/src/time_command.c",
    "samples/controller/configuration",
    "tests/configuration_consumer/consumer.c", "tests/configuration_consumer/consumer.cpp",

    "tests/file_consumer/consumer.c", "tests/file_consumer/consumer.cpp",
    "tests/file_consumer/consumer.h",

    "samples/serial", "tests/serial_consumer/src/runtime.c",
    "tests/serial_consumer/src/runtime.h", "tests/serial_consumer/src/consumer.cpp",
    "samples/logging", "tests/logging_consumer/src/runtime.c",
    "tests/logging_consumer/src/runtime.h", "tests/logging_consumer/src/consumer.cpp",
    "samples/messaging", "samples/controller/system",
    "tests/system_consumer/src/runtime.c", "tests/system_consumer/src/runtime.h",
    "tests/system_consumer/src/consumer.cpp",
    "tests/messaging_consumer/runtime.c", "tests/messaging_consumer/main.cpp",
    "tests/thread_sync_consumer/consumer.c", "tests/thread_sync_consumer/consumer.cpp",
    "tests/external_application/consumer.c", "tests/external_application/consumer.cpp",
)
# Issue #28's approved standard profile is attached to named consumer files,
# never an entire component/tree. Its stricter operation/include policy is reused.
NETWORKING_PROFILE = frozenset((
    "samples/networking/client.c", "samples/networking/client.h",
    "tests/networking/runtime_port.c", "tests/networking/runtime_port.h",
    "tests/networking_consumer/consumer.h", "tests/networking_consumer/cpp_probe.cpp",
))
NETWORKING_HEADERS = {"sys/socket.h", "netinet/in.h", "arpa/inet.h", "poll.h",
                      "fcntl.h", "unistd.h"}
SOURCE_SUFFIXES = {".c", ".h", ".cc", ".cpp", ".cxx", ".hpp", ".hh", ".hxx", ".inc"}
# Direct references into implementation/provider/test trees cannot become public
# contracts even if their basename looks portable. This does not follow includes.
PRIVATE_INCLUDE_SEGMENTS = {"ports", "host", "zephyr", "board", "boards", "nxp", "vendor",
                            "test", "tests", "private"}
NATIVE_HEADER = re.compile(
    r"^(?:zephyr/|boards?/|board\.h$|nxp/|vendor/|fsl_|MIMXRT|cmsis|stm32|"
    r"unistd\.h$|pthread\.h$|sys/|windows\.h$|arpa/|netinet/|"
    r"fcntl\.h$|semaphore\.h$|sched\.h$|poll\.h$)", re.IGNORECASE)
NATIVE_TOKEN = re.compile(
    r"\b(?:k_[A-Za-z0-9_]+|CONFIG_[A-Za-z0-9_]+|BOARD_[A-Za-z0-9_]+|"
    r"DT_[A-Za-z0-9_]+|SDK_[A-Za-z0-9_]+|"
    r"(?:FLEXSPI|GPIO|CLOCK|I2C|LPI2C|SPI|LPSPI|UART|LPUART|ENET|NVIC)_[A-Za-z0-9_]+|"
    r"(?:gpio|zsock|net_if|wdt|hwinfo|pthread)_[A-Za-z0-9_]+|"
    r"atomic_t|device_is_ready|clock_gettime|nanosleep|__ZEPHYR__|__linux__|_WIN32)\b"
    r"|\bstruct\s+device\b")
# Strings/chars must be recognised alongside comments: comment-looking text
# inside a literal is not a comment. Replace with spaces, retaining newlines so
# diagnostics identify the physical source line after line splicing.
LEXEME = re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|/\*.*?\*/|//[^\n]*', re.DOTALL)
INCLUDE = re.compile(r'^\s*#\s*include\s*[<"]([^>"\n]+)[>"]', re.MULTILINE)


def mask(match):
    return re.sub(r"[^\n]", " ", match.group())


def violations(path, public=False, networking=False):
    original = path.read_text(encoding="utf-8")
    # C translation removes backslash-newline before comments/tokenisation.
    offsets = []
    lines = original.splitlines(keepends=True)
    logical = ""
    for number, line in enumerate(lines, 1):
        part = re.sub(r"\\\r?\n$", "", line)
        logical += part
        offsets.extend([number] * len(part))
    comments_removed = LEXEME.sub(
        lambda match: mask(match) if match.group().startswith(("/*", "//")) else match.group(),
        logical)
    for match in INCLUDE.finditer(comments_removed):
        header = match.group(1)
        segments = set(header.replace("\\", "/").split("/"))
        if ((NATIVE_HEADER.match(header) and not (networking and header in NETWORKING_HEADERS))
                or segments & PRIVATE_INCLUDE_SEGMENTS
                or (public and "src" in segments)):
            yield offsets[match.start(1)], "native/private header " + header
    tokens = LEXEME.sub(mask, comments_removed)
    # Include paths are not C identifiers and have their own rule above.
    tokens = INCLUDE.sub(mask, tokens)
    if networking:
        # Mask only the approved call prefix, preserving positions/other arguments.
        tokens = re.sub(r"\bclock_gettime\s*\(\s*CLOCK_MONOTONIC\s*,", mask, tokens)
    for match in NATIVE_TOKEN.finditer(tokens):
        yield offsets[match.start()], "native token " + match.group()
    if networking:
        profile = runpy.run_path(str(Path(__file__).with_name("check-networking-dependencies.py")))
        for error in profile["source_errors"](comments_removed):
            yield 1, "qualified networking profile: " + error


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    if not args.root.is_dir():
        parser.error("source root is not a directory: " + str(args.root))
    files = set()
    for pattern in PORTABLE_ROOTS:
        for root in args.root.glob(pattern):
            candidates = root.rglob("*") if root.is_dir() else (root,)
            files.update(path for path in candidates
                         if path.is_file() and path.suffix in SOURCE_SUFFIXES)
    files.update(args.root / name for name in NETWORKING_PROFILE if (args.root / name).is_file())
    failed = False
    for path in sorted(files):
        for line, reason in violations(path, public="include" in path.relative_to(args.root).parts,
                                       networking=path.relative_to(args.root).as_posix() in NETWORKING_PROFILE):
            print(f"{path.relative_to(args.root)}:{line}: {reason} in portable source")
            failed = True
    if not failed:
        print(f"Native interface policy passed ({len(files)} portable source files).")
    return int(failed)


if __name__ == "__main__":
    raise SystemExit(main())
