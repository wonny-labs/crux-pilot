#!/usr/bin/env python3
"""List every session-only command in Crux code, and fail if the reviewed list differs.

A session-only command passes no permission check in the registry, so its handler or the service it
calls must check further. The registry cannot see whether it does, so each one is reviewed by name.
Add a new one to components/cli/session-only-commands.txt in the change that introduces it.
"""
import argparse
from pathlib import Path
import re
import sys

REPO = Path(__file__).resolve().parents[1]
ROOTS = ("components", "samples")
COMMAND = re.compile(r'CRUX_CLI_COMMAND\(\s*"([^"]+)"\s*,\s*"[^"]*"\s*,\s*(CRUX_CLI_\w+)', re.DOTALL)
# The macro's own definition and documentation are not uses.
SKIP = {"components/cli/include/crux/cli.h"}


def session_only_commands(root):
    commands = {}
    for top in ROOTS:
        for path in sorted((root / top).rglob("*")):
            if not path.is_file() or path.suffix not in {".c", ".cpp", ".h"}:
                continue
            relative = path.relative_to(root).as_posix()
            if relative in SKIP:
                continue
            for name, access in COMMAND.findall(path.read_text(encoding="utf-8")):
                if access == "CRUX_CLI_SESSION_ONLY":
                    commands[name] = relative
    return commands


def reviewed_commands(path):
    lines = path.read_text(encoding="utf-8").splitlines()
    return {line.strip() for line in lines if line.strip() and not line.startswith("#")}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=REPO)
    args = parser.parse_args()
    found = session_only_commands(args.root)
    reviewed = reviewed_commands(args.root / "components/cli/session-only-commands.txt")
    for name in sorted(found):
        print(f"{name}: {found[name]}")
    errors = [f"session-only command not reviewed: {name}" for name in sorted(set(found) - reviewed)]
    errors += [f"reviewed command does not exist: {name}" for name in sorted(reviewed - set(found))]
    for error in errors:
        print(error, file=sys.stderr)
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
