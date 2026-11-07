#!/usr/bin/env python3
"""Issue #192: SSH library types stay in the provider and in the file port.

Provider and port are the only places under components/ that may name wolfSSH or include its headers.
wolfCrypt is not listed here: the TLS provider and the PSA ports have their own rules. Comments do not
count, since a file may say what it replaces. This is a bounded lexical check. Review remains necessary.
"""
import argparse
from pathlib import Path
import re
import sys

ALLOWED = {
    "components/ssh/ports/zephyr/crux_ssh_wolf_provider.c",
    "components/ssh/ports/zephyr/crux_wolfssh_settings.h",
    "components/ssh/sftp/port/crux_ssh_sftp_port.c",
    "components/ssh/sftp/port/myFilesystem.h",
}
TOKEN = re.compile(
    r"\bWOLFSSH\w*|\bwolfSSH\w*|\bWS_[A-Z]\w+|<wolfssh/|\bWOLFSSH_CHANNEL\b|\bWS_UserAuthData\w*")
COMMENT = re.compile(r"/\*.*?\*/|//[^\n]*", re.DOTALL)
SUFFIXES = {".c", ".h", ".cc", ".cpp", ".hpp"}


def errors_in(root):
    errors = []
    for path in sorted((root / "components").rglob("*")):
        name = path.relative_to(root).as_posix()
        if path.suffix in SUFFIXES and path.is_file() and name not in ALLOWED:
            code = COMMENT.sub(" ", path.read_text(encoding="utf-8", errors="replace"))
            for match in TOKEN.finditer(code):
                errors.append(f"{name}: SSH library name {match.group()}")
    for name in ALLOWED:
        if not (root / name).is_file():
            errors.append(f"{name}: listed as allowed, and missing")
    return errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[3])
    errors = errors_in(parser.parse_args().root.resolve())
    for error in errors:
        print(error, file=sys.stderr)
    if errors:
        return 1
    print("SSH library names stay in the provider and the file port.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
