#!/usr/bin/env python3
"""List every session-only command in Crux code, and fail if the reviewed list differs.

A session-only command passes no permission check in the registry, so its handler or the service it
calls must check further. The registry cannot see whether it does, so each one is reviewed by name.
Add a new one to components/cli/session-only-commands.txt in the change that introduces it.

So that the list means what it says, this reads the source rather than matching a pattern. It fails on:

- a CRUX_CLI_COMMAND( whose name is not one string literal, whose access argument is not
  CRUX_CLI_SESSION_ONLY or CRUX_CLI_PERMISSION(...), or that it cannot otherwise read
- CRUX_CLI_SESSION_ONLY anywhere other than as that access argument, such as in an alias
- CRUX_CLI_ACCESS_SESSION outside the contract's own files, such as in a raw table entry

It does not run the preprocessor. It sees Crux's own components, samples and templates, and not a project's code,
which keeps its own review. A raw entry whose class is a bare number or a cast is not found, and
scripts/check-cli-boundary.py does not look for one either.
"""
import argparse
from pathlib import Path
import re
import sys

REPO = Path(__file__).resolve().parents[1]
ROOTS = ("components", "samples", "templates")
SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".h", ".hpp", ".inc"}
CONTRACT = "components/cli/include/crux/cli.h"
# The registry's validator has to name the class to accept it.
ACCESS_SESSION_ALLOWED = {CONTRACT, "components/cli/src/cli_registry.c"}
COMMAND_CALL = re.compile(r"\bCRUX_CLI_COMMAND\s*\(")
SESSION_ONLY = re.compile(r"\bCRUX_CLI_SESSION_ONLY\b")
ACCESS_SESSION = re.compile(r"\bCRUX_CLI_ACCESS_SESSION\b")
STRING_LITERAL = re.compile(r'^"(?:[^"\\\n]|\\.)*"$')
PERMISSION_CALL = re.compile(r"^CRUX_CLI_PERMISSION\s*\(.*\)$", re.DOTALL)
COMMAND_ARGUMENTS = 8


def blank_comments_and_strings(text):
    """Two copies of the source, each as long as it: one without comments, and one also without the
    contents of string and character literals, so that syntax can be read from the second."""
    code = list(text)
    syntax = list(text)
    index = 0
    while index < len(text):
        if text.startswith("//", index) or text.startswith("/*", index):
            end = text.find("\n", index) if text[index + 1] == "/" else text.find("*/", index + 2)
            end = len(text) if end < 0 else (end if text[index + 1] == "/" else end + 2)
            for position in range(index, end):
                if text[position] != "\n":
                    code[position] = " "
                    syntax[position] = " "
            index = end
        elif text[index] in "\"'":
            quote = text[index]
            index += 1
            while index < len(text) and text[index] != quote and text[index] != "\n":
                if text[index] == "\\":
                    syntax[index] = " "
                    index += 1
                    if index >= len(text):
                        break
                syntax[index] = " "
                index += 1
            index += 1
        else:
            index += 1
    return "".join(code), "".join(syntax)


def split_call(syntax, opening):
    """The (start, end) spans of a call's arguments, or None if the call does not end."""
    depth = 0
    spans = []
    start = opening + 1
    for position in range(opening, len(syntax)):
        character = syntax[position]
        if character in "([{":
            depth += 1
        elif character in ")]}":
            depth -= 1
            if depth == 0:
                spans.append((start, position))
                return spans
        elif character == "," and depth == 1:
            spans.append((start, position))
            start = position + 1
    return None


def line_of(text, position):
    return text.count("\n", 0, position) + 1


def read_source(relative, text, commands, errors):
    code, syntax = blank_comments_and_strings(text)
    accepted_session_only = set()
    for match in COMMAND_CALL.finditer(syntax):
        where = f"{relative}:{line_of(text, match.start())}"
        spans = split_call(syntax, match.end() - 1)
        if spans is None or len(spans) != COMMAND_ARGUMENTS:
            errors.append(f"{where}: cannot parse this CRUX_CLI_COMMAND: expected "
                          f"{COMMAND_ARGUMENTS} arguments")
            continue
        name = code[spans[0][0]:spans[0][1]].strip()
        access = syntax[spans[2][0]:spans[2][1]].strip()
        if not STRING_LITERAL.match(name):
            errors.append(f"{where}: cannot parse this CRUX_CLI_COMMAND: its name is not one string "
                          "literal")
            continue
        if access == "CRUX_CLI_SESSION_ONLY":
            commands[name[1:-1]] = relative
            accepted_session_only.add(SESSION_ONLY.search(syntax, spans[2][0]).start())
        elif not PERMISSION_CALL.match(access):
            errors.append(f"{where}: cannot parse this CRUX_CLI_COMMAND: its access argument is "
                          "not CRUX_CLI_SESSION_ONLY or CRUX_CLI_PERMISSION(...)")
    if relative != CONTRACT:
        for match in SESSION_ONLY.finditer(syntax):
            if match.start() not in accepted_session_only:
                errors.append(f"{relative}:{line_of(text, match.start())}: CRUX_CLI_SESSION_ONLY "
                              "outside a command it can read, which would hide a session-only command")
    if relative not in ACCESS_SESSION_ALLOWED:
        for match in ACCESS_SESSION.finditer(syntax):
            errors.append(f"{relative}:{line_of(text, match.start())}: CRUX_CLI_ACCESS_SESSION "
                          "outside the contract, which would hide a session-only command")


def read_sources(root):
    """Every session-only command as a name to file map, and what could not be read."""
    commands = {}
    errors = []
    for top in ROOTS:
        for path in sorted((root / top).rglob("*")):
            if not path.is_file() or path.suffix not in SOURCE_SUFFIXES:
                continue
            relative = path.relative_to(root).as_posix()
            if relative == CONTRACT:
                continue
            read_source(relative, path.read_text(encoding="utf-8"), commands, errors)
    return commands, errors


def reviewed_commands(path):
    lines = path.read_text(encoding="utf-8").splitlines()
    return {line.strip() for line in lines if line.strip() and not line.startswith("#")}


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", type=Path, default=REPO)
    args = parser.parse_args()
    found, errors = read_sources(args.root)
    reviewed = reviewed_commands(args.root / "components/cli/session-only-commands.txt")
    for name in sorted(found):
        print(f"{name}: {found[name]}")
    errors += [f"session-only command not reviewed: {name}" for name in sorted(set(found) - reviewed)]
    errors += [f"reviewed command does not exist: {name}" for name in sorted(reviewed - set(found))]
    for error in errors:
        print(error, file=sys.stderr)
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
