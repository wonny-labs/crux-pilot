#!/usr/bin/env python3
"""Flash by object file from two GNU ld link maps, and the difference. Host script, reads maps only.

usage: map_diff.py BEFORE.map AFTER.map [--top N]

An input section is counted when it sits in the QSPI Flash range (0x60000000 to 0x6fffffff) in the map's own
"Linker script and memory map" part. The object is the archive member or the object file named on its line. The totals
are a reading of the map and not the linker's own report: they differ from `size` by padding and by sections the map
lists without an owner (see docs/rt1060-management-image-feasibility.md, Flash by source).
"""
import re
import sys
from collections import defaultdict

SECTION = re.compile(r"^ (\.\S+)\s*$")
FULL = re.compile(r"^\s+(?:(\.\S+)\s+)?0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S.*)$")


def read(path):
    """{object: bytes in Flash}, {object: [(section, bytes)]}, total."""
    text = open(path, errors="replace").read()
    start = text.find("Linker script and memory map")
    lines = text[start:].splitlines()
    pending = None
    totals = defaultdict(int)
    sections = defaultdict(list)
    for line in lines:
        m = SECTION.match(line)
        if m:
            pending = m.group(1)
            continue
        m = FULL.match(line)
        if not m:
            pending = None
            continue
        name, address, size, owner = m.group(1) or pending, int(m.group(2), 16), int(m.group(3), 16), m.group(4)
        pending = None
        if name is None or size == 0 or not (0x60000000 <= address < 0x70000000):
            continue
        owner = owner.strip()
        if owner.startswith("*") or owner.startswith("0x"):
            continue
        totals[owner] += size
        sections[owner].append((name, size))
    return totals, sections


def short(owner):
    # modules/.../libwolfSSL.a(sp_cortexm.c.obj) -> libwolfSSL.a(sp_cortexm.c.obj)
    m = re.search(r"([^/\\]+\.a\([^)]*\))$", owner)
    if m:
        return m.group(1)
    return owner.rsplit("/", 2)[-2] + "/" + owner.rsplit("/", 1)[-1] if "/" in owner else owner


def main():
    args = sys.argv[1:]
    top = 40
    if "--top" in args:
        i = args.index("--top")
        top = int(args[i + 1])
        del args[i:i + 2]
    before, after = args
    tb, sb = read(before)
    ta, sa = read(after)
    print(f"Flash bytes in input sections: before {sum(tb.values())}, after {sum(ta.values())}, "
          f"difference {sum(ta.values()) - sum(tb.values())}")
    keys = sorted(set(tb) | set(ta), key=lambda k: (ta.get(k, 0) - tb.get(k, 0), k))
    changed = [k for k in keys if ta.get(k, 0) != tb.get(k, 0)]
    gone = [k for k in changed if k not in ta]
    new = [k for k in changed if k not in tb]
    print(f"objects: {len(tb)} before, {len(ta)} after; changed {len(changed)}, gone {len(gone)}, new {len(new)}")
    print()
    print(f"{'before':>8} {'after':>8} {'diff':>8}  object")
    for k in changed[:top]:
        print(f"{tb.get(k, 0):8d} {ta.get(k, 0):8d} {ta.get(k, 0) - tb.get(k, 0):8d}  {short(k)}"
              + ("  (gone)" if k in gone else "") + ("  (new)" if k in new else ""))
    if len(changed) > top:
        print(f"... {len(changed) - top} more with smaller changes, total {sum(ta.get(k, 0) - tb.get(k, 0) for k in changed[top:])}")
    # Per library group
    def group(k):
        return short(k).split("(")[0].replace("lib..__modules__crypto__", "").replace("__zephyr.a", ".a")
    gb, ga = defaultdict(int), defaultdict(int)
    for k, v in tb.items():
        gb[group(k)] += v
    for k, v in ta.items():
        ga[group(k)] += v
    print()
    print(f"{'before':>8} {'after':>8} {'diff':>8}  library or folder (groups that changed)")
    for g in sorted(set(gb) | set(ga), key=lambda g: ga.get(g, 0) - gb.get(g, 0)):
        if ga.get(g, 0) != gb.get(g, 0):
            print(f"{gb.get(g, 0):8d} {ga.get(g, 0):8d} {ga.get(g, 0) - gb.get(g, 0):8d}  {g}")


if __name__ == "__main__":
    main()
