#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Fail when the product's domain includes an operating-system header.

The domain is the plain C directory ``domain/`` beside ``app/``. It owns no thread and takes
no lock, so it must not include:

* ``<crux/thread.h>``, ``<crux/sync.h>``, ``<crux/queue.h>`` or ``<crux/serial.h>``, the four
  interfaces that are frozen for new product code, or
* any ``<zephyr/...>`` header (or ``<zephyr.h>``), or
* a Crux composition interface, ``<crux/NAME_zephyr.h>`` or ``<crux/NAME_linux.h>``, which names
  a provider.

Use Zephyr in the product's shell (``app/src``) and let the domain reach what it needs from
outside through the product-owned port header. The other public ``<crux/NAME.h>`` headers and
the ISO C headers stay allowed. A quoted include must name a file inside the domain directory.
A header name with a ``.`` or ``..`` component is rejected, so no spelling hides a name.

Other native headers (SoC, CMSIS, vendor HAL, POSIX under ``CONFIG_POSIX_API``) are not listed
here. The Host build of the domain catches them, because they do not exist there.

The check reads every file under the directory, whatever its suffix, and every ``#include``,
``#include_next`` and ``#import`` directive, after joining lines that end in a backslash,
reading CRLF line ends, the digraph ``%:`` and the trigraph ``??=`` as ``#``, and removing
comments, after dropping a leading byte order mark. An operand that is not a literal ``<name>`` or ``"name"`` (a macro, for example)
fails, because it cannot be checked. The check does not see include paths that CMake adds, so
the template's build adds none.

Product mode runs the same reading over a whole product repository, so that its adapters are
checked too::

    check_domain_includes.py --product ROOT [--domain DIR]... [--exclude PATH]...

In product mode every source file under ROOT (suffix .c .h .cc .cpp .cxx .hh .hpp .hxx .inc or .S)
must not include one of the four frozen headers. A directory named by ``--domain`` (default:
``domain`` when it exists) keeps every rule above. Elsewhere, the adapters, ``<zephyr/...>`` headers,
composition interfaces and other public Crux headers stay allowed, and an operand that is not a
literal is not a violation, because adapters use Zephyr macros. ``--exclude`` skips a legacy path
under ROOT, so that code that already uses a frozen header can be listed and migrated. The
directories ``.git``, ``.west`` and ``build`` and symbolic links to directories are never scanned.

Usage: check_domain_includes.py [DOMAIN_DIR]    (default: ``domain/`` beside this file)
       check_domain_includes.py --product ROOT [--domain DIR]... [--exclude PATH]...
Exit status: 0 passed, 1 violations, 2 usage error or no file to check.
"""

import argparse
import dataclasses
import os
import pathlib
import posixpath
import re
import sys

SOURCE_SUFFIXES = frozenset({".c", ".h", ".cc", ".cpp", ".cxx", ".hh", ".hpp", ".hxx", ".inc", ".S"})
SKIPPED_DIRECTORIES = frozenset({".git", ".west", "build"})

FROZEN_CRUX_HEADERS = frozenset({"crux/thread.h", "crux/sync.h", "crux/queue.h", "crux/serial.h"})

COMPOSITION_INTERFACE = re.compile(r"^crux/[A-Za-z0-9_]+_(zephyr|linux)\.h$")

DIRECTIVE = re.compile(r"^[ \t\f\v]*#[ \t\f\v]*(include_next|include|import)\b(.*)$", re.DOTALL)
OPERAND = re.compile(r'^\s*(?:<([^>\n]+)>|"([^"\n]+)")')


@dataclasses.dataclass(frozen=True)
class Violation:
    path: str
    line: int
    include: str
    reason: str


INCLUDE_PREFIX = re.compile(r"^[ \t\f\v]*#[ \t\f\v]*(?:include_next|include|import)[ \t\f\v]*$")


def strip_comments(text):
    """Blank out comments, keep string, character and header-name literals, and keep every newline.

    The ``<...>`` after an include directive is a header name, not code: ``//`` inside it does not
    start a comment, so ``<crux//thread.h>`` is read whole.
    """
    out = []
    line = []  # the text of the current line so far, with comments blanked
    i, n = 0, len(text)

    def emit(chunk):
        out.append(chunk)
        tail = chunk.rsplit("\n", 1)
        line[:] = [tail[-1]] if len(tail) > 1 else line + [chunk]

    while i < n:
        c = text[i]
        pair = text[i : i + 2]
        if pair == "//":
            while i < n and text[i] != "\n":
                emit(" ")
                i += 1
        elif pair == "/*":
            end = text.find("*/", i + 2)
            end = n if end < 0 else end + 2
            emit("".join("\n" if ch == "\n" else " " for ch in text[i:end]))
            i = end
        elif c == "<" and INCLUDE_PREFIX.match("".join(line)):
            j = i + 1
            while j < n and text[j] not in ">\n":
                j += 1
            emit(text[i : j + 1] if j < n and text[j] == ">" else text[i:j])
            i = j + 1 if j < n and text[j] == ">" else j
        elif c in "\"'":
            j = i + 1
            while j < n and text[j] != c and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            emit(text[i : j + 1])
            i = j + 1
        else:
            emit(c)
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
    name = posixpath.normpath(include)
    if name in FROZEN_CRUX_HEADERS:
        return "frozen Crux header: use Zephyr in the shell and reach it through a port"
    if name.startswith("zephyr/") or name == "zephyr.h":
        return "Zephyr header: the domain is plain C, use a port"
    if COMPOSITION_INTERFACE.match(name):
        return "Crux composition interface: it names a provider, use a port"
    if angle and {".", ".."} & set(include.split("/")):
        return "header name has a . or .. component, so it cannot be checked"
    if not angle and not resolves_inside(include, source, root):
        return "quoted header is not a file inside the domain directory"
    return None


def read_includes(source):
    """Yield (line, operand text, include or None, angle) for each include directive of a file.

    The include is None when the operand is not a literal <name> or "name".
    """
    text = strip_comments(normalise(source.read_bytes().decode("utf-8-sig", errors="replace")))
    for line, logical in logical_lines(text):
        directive = DIRECTIVE.match(logical)
        if not directive:
            continue
        operand = OPERAND.match(directive.group(2))
        if not operand:
            yield line, directive.group(2).strip() or "(empty)", None, False
            continue
        angle = operand.group(1) is not None
        yield line, None, operand.group(1) if angle else operand.group(2), angle


def check_file(source, relative, root):
    """Return the violations of one file under the full domain rules, with root as the domain."""
    violations = []
    for line, unreadable, include, angle in read_includes(source):
        if include is None:
            violations.append(
                Violation(
                    relative,
                    line,
                    unreadable,
                    'include operand is not a literal <name> or "name", so it cannot be checked',
                )
            )
            continue
        reason = classify(include, angle, source, root)
        if reason:
            violations.append(Violation(relative, line, include, reason))
    return violations


def check(root):
    """Return the violations under the domain directory.

    Raise ValueError when there is no file to check, so an empty directory cannot pass.
    """
    root = pathlib.Path(root)
    sources = sorted(p for p in root.rglob("*") if p.is_file())
    if not sources:
        raise ValueError(f"{root} holds no file to check")
    violations = []
    for source in sources:
        violations.extend(check_file(source, source.relative_to(root.parent).as_posix(), root))
    return violations


ADAPTER_FROZEN_REASON = (
    "frozen Crux header: use Zephyr threads, locks, queues and drivers (docs/choosing-an-api.md)"
)


UNREADABLE_LITERAL_REASON = "include operand opens a header name that does not close, so it cannot be checked"


def check_adapter_file(source, relative):
    """Return the violations of a file outside the domain: the four frozen headers only."""
    violations = []
    for line, unreadable, include, _angle in read_includes(source):
        if include is None:
            # A macro operand is not checked here, but a broken literal could hide a name.
            if unreadable[:1] in ('<', '"'):
                violations.append(Violation(relative, line, unreadable, UNREADABLE_LITERAL_REASON))
        elif posixpath.normpath(include) in FROZEN_CRUX_HEADERS:
            violations.append(Violation(relative, line, include, ADAPTER_FROZEN_REASON))
    return violations


def contained_path(root, name, what):
    """Return an existing path inside root named relative to it, or raise ValueError."""
    candidate = pathlib.PurePosixPath(name)
    if not name or candidate.is_absolute() or ".." in candidate.parts:
        raise ValueError(f"{what} {name!r} must be a path inside the product root")
    path = (root / candidate).resolve()
    if path == root.resolve() or not path.is_relative_to(root.resolve()):
        raise ValueError(f"{what} {name!r} must be a path inside the product root")
    if not path.exists():
        raise ValueError(f"{what} {name!r} does not exist under {root}")
    return path


def check_product(root, domain_dirs=None, excludes=()):
    """Return the violations of every source file under a product repository.

    Files under a domain directory follow the domain rules, all others the frozen-header rule.
    Raise ValueError on a bad path option or when there is no source file, so a mistake cannot pass.
    """
    root = pathlib.Path(root)
    if not root.is_dir():
        raise ValueError(f"{root} is not a directory")
    if domain_dirs is None:
        domain_dirs = ("domain",) if (root / "domain").is_dir() else ()
    domains = [contained_path(root, name, "domain directory") for name in domain_dirs]
    for domain in domains:
        if not domain.is_dir():
            raise ValueError(f"domain directory {domain} is not a directory")
    skipped = [contained_path(root, name, "excluded path") for name in excludes]
    for path in skipped:
        for domain in domains:
            if path == domain or path.is_relative_to(domain) or domain.is_relative_to(path):
                raise ValueError(f"excluded path {path} overlaps the domain directory {domain}")
    resolved_root = root.resolve()

    def domain_of(path):
        return next((d for d in domains if path.is_relative_to(d)), None)

    sources = []
    for current, names, files in os.walk(root, followlinks=False):
        here = pathlib.Path(current)
        names[:] = sorted(
            n
            for n in names
            if n not in SKIPPED_DIRECTORIES
            and not (here / n).is_symlink()
            and (here / n).resolve() not in skipped
        )
        for name in sorted(files):
            path = here / name
            if (
                pathlib.Path(name).suffix in SOURCE_SUFFIXES
                and path.is_file()
                and path.resolve() not in skipped
            ):
                sources.append(path)
    if not sources:
        raise ValueError(f"{root} holds no source file to check")
    violations = []
    for source in sources:
        relative = source.resolve().relative_to(resolved_root).as_posix()
        domain = domain_of(source.resolve())
        if domain is None:
            violations.extend(check_adapter_file(source, relative))
        else:
            violations.extend(check_file(source, relative, domain))
    return violations


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument(
        "domain",
        nargs="?",
        default=None,
        help="the domain directory (default: domain/ beside this file)",
    )
    parser.add_argument("--product", metavar="ROOT", help="check every source file of a product repository")
    parser.add_argument(
        "--domain",
        dest="domain_dirs",
        action="append",
        metavar="DIR",
        help="with --product: a domain directory under ROOT (repeatable; default: domain when it exists)",
    )
    parser.add_argument(
        "--exclude",
        action="append",
        default=[],
        metavar="PATH",
        help="with --product: skip a legacy path under ROOT (repeatable)",
    )
    args = parser.parse_args(argv)
    if args.product is not None:
        if args.domain is not None:
            parser.error("--product takes no positional domain directory; use --domain")
        return run_product(args)
    if args.domain_dirs or args.exclude:
        parser.error("--domain and --exclude need --product")
    return run_domain(
        pathlib.Path(args.domain or pathlib.Path(__file__).resolve().parent / "domain")
    )


def report(violations, label):
    for v in violations:
        print(f"{v.path}:{v.line}: {v.include}: {v.reason}")
    if violations:
        print(f"{len(violations)} {label} include violation(s)")
        return 1
    print(f"{label} include check passed")
    return 0


def run_domain(root):
    if not root.is_dir():
        print(f"error: {root} is not a directory", file=sys.stderr)
        return 2
    try:
        violations = check(root)
    except ValueError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2
    return report(violations, "domain")


def run_product(args):
    try:
        violations = check_product(args.product, args.domain_dirs, args.exclude)
    except ValueError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2
    status = report(violations, "product")
    if args.exclude:
        print("excluded: " + ", ".join(args.exclude))
    return status


if __name__ == "__main__":
    sys.exit(main())
