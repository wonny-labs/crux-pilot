#!/usr/bin/env python3
"""Which object files of a bench image build reference a format function (row C3, decision D2).

The bench image formats nothing but its RAM disk, and only src/bench_medium_fs.c names fs_mkfs. This tool shows
it on the linked objects of a real build, which is what the image holds, and not on the sources:

  check_mkfs_callers.py NM BUILD_DIR ram|card

NM is the arm-zephyr-eabi-nm of the toolchain (or the host nm, in the unit test). Every *.obj and *.o under
BUILD_DIR is read with `nm -A -u`, and the objects that reference fs_mkfs, f_mkfs or fatfs_mkfs must be exactly:

  ram:  fs_mkfs from bench_medium_fs.c.obj only. f_mkfs from the FatFs glue of Zephyr only.
  card: no object references fs_mkfs, and none but the FatFs glue references f_mkfs.

It exits 0 when that holds and 1 with the reasons when not. It touches no board and no device.
"""
import re
import subprocess
import sys
from pathlib import Path

WATCHED = ("fs_mkfs", "f_mkfs", "fatfs_mkfs", "disk_access_ioctl_format")
# The FatFs backend of Zephyr's file system layer reaches f_mkfs through its mkfs entry in a function table.
FATFS_BACKEND = ("fat_fs.c.obj", "fat_fs.c.o")
GLUE = ("bench_medium_fs.c.obj", "bench_medium_fs.c.o")


def undefined_references(nm, path):
    """The undefined symbols of one object. GNU nm prints `file: U name`, and the macOS nm `file: _name`, with
    a leading underscore on every C symbol. The path is not parsed: only the last field is the symbol."""
    out = subprocess.run([nm, "-A", "-u", str(path)], capture_output=True, text=True, check=False)
    names = set()
    for line in out.stdout.splitlines():
        fields = line.split()
        if not fields or ":" not in fields[0]:
            continue
        name = fields[-1]
        if len(fields) == 2 and name.startswith("_"):  # the macOS form has no type letter
            name = name[1:]
        names.add(name)
    return names


def callers_by_symbol(nm, build_dir):
    found = {}
    for path in sorted(Path(build_dir).rglob("*")):
        if path.is_file() and path.suffix in (".obj", ".o"):
            for symbol in undefined_references(nm, path) & set(WATCHED):
                found.setdefault(symbol, []).append(path)
    return found


def check(nm, build_dir, medium):
    problems = []
    found = callers_by_symbol(nm, build_dir)
    for symbol, paths in found.items():
        names = [p.name for p in paths]
        if symbol == "fs_mkfs":
            if medium == "ram" and names != [GLUE[0]] and names != [GLUE[1]]:
                problems.append(f"fs_mkfs is referenced by {[str(p) for p in paths]}, expected only bench_medium_fs.c.obj")
            if medium == "card":
                problems.append(f"the card image references fs_mkfs from {[str(p) for p in paths]}")
        elif symbol in ("f_mkfs", "fatfs_mkfs"):
            stray = [str(p) for p in paths if p.name not in FATFS_BACKEND]
            if stray:
                problems.append(f"{symbol} is referenced by {stray}, and not only by the FatFs glue of Zephyr")
        else:
            problems.append(f"{symbol} is referenced by {[str(p) for p in paths]}")
    if medium == "ram" and "fs_mkfs" not in found:
        problems.append("the RAM disk image does not reference fs_mkfs at all: the control found nothing")
    return problems


def main(argv):
    if len(argv) != 4 or argv[3] not in ("ram", "card"):
        print(__doc__, file=sys.stderr)
        return 2
    problems = check(argv[1], argv[2], argv[3])
    for problem in problems:
        print(problem, file=sys.stderr)
    if problems:
        return 1
    found = callers_by_symbol(argv[1], argv[2])
    for symbol in sorted(found):
        print(f"{symbol}: {', '.join(p.name for p in found[symbol])}")
    print(f"ok: in the {argv[3]} image only the expected objects reference a format function")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
