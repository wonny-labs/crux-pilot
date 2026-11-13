#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Fail when the product's domain includes an operating-system header.

The domain is the plain C directory ``domain/`` beside ``app/``. It owns no thread and takes
no lock, so it must not include:

* ``<crux/thread.h>``, ``<crux/sync.h>``, ``<crux/queue.h>`` or ``<crux/serial.h>``, the four
  interfaces that are frozen for new product code, or
* any Zephyr header (``<zephyr/...>``).

Use Zephyr in the product's shell (``app/src``) and let the domain reach the outside only
through the product-owned port header. The other public ``<crux/NAME.h>`` headers and the
ISO C headers stay allowed. A quoted include must name a file inside the domain directory.

The check reads every file under the directory, whatever its suffix, and every ``#include``,
``#include_next`` and ``#import`` directive, after joining lines that end in a backslash,
reading CRLF line ends, the digraph ``%:`` and the trigraph ``??=`` as ``#``, and removing
comments. An operand that is not a literal ``<name>`` or ``"name"`` (a macro, for example)
fails, because it cannot be checked. The check does not see include paths that CMake adds, so
the template's build adds none.

Usage: check_domain_includes.py [DOMAIN_DIR]    (default: ``domain/`` beside this file)
Exit status: 0 passed, 1 violations, 2 usage error.
"""

import argparse
import dataclasses
import pathlib
import re
import sys

FROZEN_CRUX_HEADERS = frozenset({"crux/thread.h", "crux/sync.h", "crux/queue.h", "crux/serial.h"})

DIRECTIVE = re.compile(r"^[ \t\f\v]*#[ \t\f\v]*(include_next|include|import)\b(.*)$", re.DOTALL)
OPERAND = re.compile(r'^\s*(?:<([^>\n]+)>|"([^"\n]+)")')


@dataclasses.dataclass(frozen=True)
class Violation:
    path: str
    line: int
    include: str
    reason: str


def strip_comments(text):
    """Blank out comments, keep string and character literals, and keep every newline."""
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        pair = text[i : i + 2]
        if pair == "//":
            while i < n and text[i] != "\n":
                out.append(" ")
                i += 1
        elif pair == "/*":
            end = text.find("*/", i + 2)
            end = n if end < 0 else end + 2
            out.append("".join("\n" if ch == "\n" else " " for ch in text[i:end]))
            i = end
        elif c in "\"'":
            j = i + 1
            while j < n and text[j] != c and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            out.append(text[i : j + 1])
            i = j + 1
        else:
            out.append(c)
            i += 1
    return "".join(out)


def normalise(text):
    """Line ends as LF, and the digraph and trigraph for # as #, before any other reading."""
    return text.replace("\r\n", "\n").replace("\r", "\n").replace("??=", "#").replace("%:", "#")


def logical_lines(text):
    """Yield (first line number, text) with backslash-continued lines joined, as C joins them."""
    lines = text.split("\n")
    index = 0
    while index < len(lines):
        first = index
        joined = lines[index]
        while joined.endswith("\\") and index + 1 < len(lines):
            index += 1
            joined = joined[:-1] + lines[index]
        yield first + 1, joined
        index += 1


def resolves_inside(include, source, root):
    """True when the quoted header is a file inside the domain directory."""
    root = root.resolve()
    for base in (source.parent, root):
        candidate = (base / include).resolve()
        if candidate.is_file() and candidate.is_relative_to(root):
            return True
    return False


def classify(include, angle, source, root):
    """Return a reason when the include is not allowed in the domain, otherwise None."""
    if include in FROZEN_CRUX_HEADERS:
        return "frozen Crux header: use Zephyr in the shell and reach it through a port"
    if include.startswith("zephyr/") or include == "zephyr.h":
        return "Zephyr header: the domain is plain C, use a port"
    if not angle and not resolves_inside(include, source, root):
        return "quoted header is not a file inside the domain directory"
    return None


def check(root):
    """Return the violations under the domain directory."""
    root = pathlib.Path(root)
    violations = []
    for source in sorted(p for p in root.rglob("*") if p.is_file()):
        relative = source.relative_to(root.parent).as_posix()
        text = strip_comments(normalise(source.read_bytes().decode("utf-8", errors="replace")))
        for line, logical in logical_lines(text):
            directive = DIRECTIVE.match(logical)
            if not directive:
                continue
            operand = OPERAND.match(directive.group(2))
            if not operand:
                violations.append(
                    Violation(
                        relative,
                        line,
                        directive.group(2).strip() or "(empty)",
                        'include operand is not a literal <name> or "name", so it cannot be checked',
                    )
                )
                continue
            angle = operand.group(1) is not None
            include = operand.group(1) if angle else operand.group(2)
            reason = classify(include, angle, source, root)
            if reason:
                violations.append(Violation(relative, line, include, reason))
    return violations


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument(
        "domain",
        nargs="?",
        default=str(pathlib.Path(__file__).resolve().parent / "domain"),
        help="the domain directory (default: domain/ beside this file)",
    )
    args = parser.parse_args(argv)
    root = pathlib.Path(args.domain)
    if not root.is_dir():
        print(f"error: {root} is not a directory", file=sys.stderr)
        return 2
    violations = check(root)
    for v in violations:
        print(f"{v.path}:{v.line}: {v.include}: {v.reason}")
    if violations:
        print(f"{len(violations)} domain include violation(s)")
        return 1
    print("domain include check passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
