#!/usr/bin/env python3
"""Keep only the cached packages whose contents match the signed apt index.

apt accepts a file already in its archive directory by name and size alone, so a
restored cache must be checked before `apt-get install` reads it. Anything else in the
directory is removed, and apt then downloads and verifies that package itself.

Usage: verify-apt-cache.py URIS_FILE ARCHIVE_DIR

URIS_FILE is the output of `apt-get install --print-uris -qq -o Acquire::ForceHash=SHA256`
run against an empty archive directory, so that it lists every package to be installed.
"""
import hashlib
from pathlib import Path
import re
import shutil
import sys

LINE = re.compile(r"^'[^']+' (?P<name>[^/\s]+\.deb) \d+ (?P<algorithm>SHA256|SHA512):(?P<digest>[0-9a-f]+)$")


def read_expected(text):
    expected = {}
    for line in text.splitlines():
        match = LINE.match(line)
        if match is None:
            raise ValueError(f"unexpected line in package list: {line}")
        expected[match["name"]] = (match["algorithm"].lower(), match["digest"])
    return expected


def is_verified(path, expected):
    if path.is_symlink() or not path.is_file() or path.name not in expected:
        return False
    algorithm, digest = expected[path.name]
    return hashlib.new(algorithm, path.read_bytes()).hexdigest() == digest


def remove(path):
    if path.is_dir() and not path.is_symlink():
        shutil.rmtree(path)
    else:
        path.unlink()


def main(uris_file, archive_dir):
    try:
        expected = read_expected(Path(uris_file).read_text())
    except ValueError as error:
        print(error, file=sys.stderr)
        return 1
    directory = Path(archive_dir)
    kept = removed = 0
    for path in sorted(directory.iterdir()) if directory.is_dir() else []:
        if is_verified(path, expected):
            kept += 1
        else:
            print(f"Removing {path.name}: not a listed package with the expected hash")
            remove(path)
            removed += 1
    print(f"Cached packages: {kept} verified, {removed} removed, {len(expected)} needed")
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print(__doc__, file=sys.stderr)
        sys.exit(2)
    sys.exit(main(sys.argv[1], sys.argv[2]))
