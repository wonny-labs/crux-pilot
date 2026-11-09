#!/usr/bin/env python3
"""The symbols that left or entered the image, from two `nm -S --size-sort -r` listings. Host script, reads text only.

usage: symbol_diff.py BEFORE-nm.txt AFTER-nm.txt
"""
import re
import sys

RSA = re.compile(r"Rsa(?![a-z])|(?<![A-Z])RSA(?![A-Z])|(?<![A-Za-z])rsa(?![a-z])|^sp_(?:ModExp|RsaPrivate|RsaPublic)_|^sp_(?:2048|3072|4096)_")


def read(path):
    symbols = {}
    for line in open(path):
        fields = line.split()
        if len(fields) < 4:
            continue
        symbols[fields[3]] = (int(fields[1], 16), fields[2])
    return symbols


before, after = read(sys.argv[1]), read(sys.argv[2])
gone = {n: v for n, v in before.items() if n not in after}
new = {n: v for n, v in after.items() if n not in before}
changed = {n: (before[n][0], after[n][0]) for n in before if n in after and before[n][0] != after[n][0]}
section = lambda kind: "code" if kind in "Tt" else ("read-only data" if kind in "RrVv" else ("data" if kind in "DdGg" else ("bss" if kind in "BbSs" else "other")))
print(f"symbols: {len(before)} before, {len(after)} after; gone {len(gone)} ({sum(v[0] for v in gone.values())} B), "
      f"new {len(new)} ({sum(v[0] for v in new.values())} B), changed in size {len(changed)}")
print()
by = {}
for n, (size, kind) in gone.items():
    by.setdefault(section(kind), []).append(size)
for k, v in sorted(by.items()):
    print(f"gone, {k}: {len(v)} symbols, {sum(v)} B")
rsa_gone = {n: v for n, v in gone.items() if RSA.search(n)}
print(f"gone with an RSA name (see check_bench_rsa_symbols.py): {len(rsa_gone)} symbols, {sum(v[0] for v in rsa_gone.values())} B")
print(f"gone without an RSA name: {len(gone) - len(rsa_gone)} symbols, {sum(v[0] for n, v in gone.items() if n not in rsa_gone)} B")
print()
print("== still in the image with an RSA name")
for n, (size, kind) in sorted(after.items()):
    if RSA.search(n):
        print(f"  {n} ({kind}, {size} B)")
print()
print("== new symbols")
for n, (size, kind) in sorted(new.items(), key=lambda x: -x[1][0]):
    print(f"  {n} ({kind}, {size} B)")
print()
print("== changed in size (before -> after), largest change first")
for n, (b, a) in sorted(changed.items(), key=lambda x: -abs(x[1][1] - x[1][0]))[:25]:
    print(f"  {n}: {b} -> {a} ({a - b:+d} B)")
print()
print("== gone with an RSA name, largest first")
for n, (size, kind) in sorted(rsa_gone.items(), key=lambda x: -x[1][0]):
    print(f"  {n} ({kind}, {size} B)")
print()
print("== gone without an RSA name, largest first")
for n, (size, kind) in sorted(((n, v) for n, v in gone.items() if n not in rsa_gone), key=lambda x: -x[1][0]):
    print(f"  {n} ({kind}, {size} B)")
