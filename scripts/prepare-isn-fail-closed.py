#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Applies patches/zephyr/zephyr-isn-fail-closed.patch to a BUILD-LOCAL copy of the pinned Zephyr tree (row C11).

The patch changes one file, subsys/net/ip/tcp.c: the TCP initial sequence number (tcp_init_isn) refuses when it has no key
(sys_csrand_get() failed) or cannot hash, and the hash buffer has the size of a SHA-256 digest. See
docs/rt1060-isn-fail-closed.md. The tree is changed in place, so it must be a COPY: this refuses a Git checkout (a
directory with .git), and the west workspace's Zephyr is one. It never writes the tree unless every hash is the expected one.

  scripts/prepare-isn-fail-closed.py ZEPHYR_COPY

Order for the bench image: first scripts/prepare-wolf-tls-zephyr.sh ZEPHYR_COPY (the wolfSSL TLS socket patch, D8, which
makes the copy and writes .crux-wolf-tls as "<pin> <tls patch sha256>"), then this script on the same folder. The copy has
ONE marker, the one the freeze of docs/rt1060-bench-freeze-and-capture-design.md reads: this script requires it to be exactly
"<pin> <tls patch sha256>" and rewrites it as "<pin> <tls patch sha256> <isn patch sha256>", so the order of the two patches is
the order of the hashes and a copy made in another order or with a missing step has another marker. It writes no marker of its own.
The two patches touch different files (sockets_tls.c and the random subsystem, and tcp.c): this script checks that tcp.c is
still the pinned one, so a TLS patch that touched it would be refused here. The bench guard (bench_guard.cmake) checks the one
marker and every patched file's hash at configuration.

Without the TLS marker the script is in its standalone mode, which is what scripts/test-isn-fail-closed.sh uses on a bare copy
of the pinned tree for native_sim: it applies the patch and writes NO marker (no marker format is invented for it), so the bench
guard refuses such a copy, and a second run on it is refused. A copy with the marker of the earlier version of this script
(.crux-isn-fail-closed) is refused: make the copy again with the two commands above.
"""
import argparse
import hashlib
import os
import subprocess
import sys
import tempfile
from pathlib import Path

RELATIVE_SOURCE = Path("subsys/net/ip/tcp.c")
# The one marker of the copy, written by scripts/prepare-wolf-tls-zephyr.sh and extended here, and the marker that this script
# wrote before the freeze design (T11): a copy that has it is refused.
TLS_MARKER = ".crux-wolf-tls"
LEGACY_MARKER = ".crux-isn-fail-closed"
# The sha256 of the TLS patch, which scripts/prepare-wolf-tls-zephyr.sh writes into the marker: change both together with
# bench_declaration.json.
TLS_PATCH_SHA256 = "3d1cc0307bbeea5f000698bca5be717f73fdb8c47e90083b003336364e8fdbda"
# tcp.c at the pinned Zephyr (v4.4.2), the patch, and tcp.c after it. A change of any of them means a base or a patch
# moved: review the result and change the three hashes here, in bench_declaration.json and in docs/rt1060-isn-fail-closed.md together.
BASE_SHA256 = "393b940e0d96adba9a0f7667c061b9e0e5d8d0ea2f49642e60f29ce1efdb7ad6"
PATCH_SHA256 = "831fecdbc6cfb23f47ace4dda662a95222b977977a167ef786ee02f33712de87"
PATCHED_SHA256 = "94642fe2257f80dd91b9e84d8c29459ca84144cb56af0cdfdd58039473e0e0cf"
PATCH = Path(__file__).resolve().parents[1] / "patches/zephyr/zephyr-isn-fail-closed.patch"


def digest(data):
    return hashlib.sha256(data).hexdigest()


def west_pin():
    """The Zephyr revision of west.yml, as scripts/prepare-wolf-tls-zephyr.sh reads it."""
    inside = False
    for line in (Path(__file__).resolve().parents[1] / "west.yml").read_text().splitlines():
        words = line.split()
        if words[:2] == ["-", "name:"]:
            inside = words[2:3] == ["zephyr"]
        if inside and words[:1] == ["revision:"]:
            return words[1]
    raise ValueError("west.yml has no pin for zephyr")


def prepare(tree, pin, patch=PATCH):
    tree = Path(tree).resolve()
    if (tree / ".git").exists():
        raise ValueError("The tree is a Git checkout: the patch is applied to a build-local COPY of the pinned Zephyr only")
    source = tree / RELATIVE_SOURCE
    if not source.is_file():
        raise ValueError(f"{RELATIVE_SOURCE} is not in the tree")
    patch_bytes = Path(patch).read_bytes()
    if digest(patch_bytes) != PATCH_SHA256:
        raise ValueError("The patch file is not the one whose hash this script knows; nothing written")
    if (tree / LEGACY_MARKER).exists() or (tree / LEGACY_MARKER).is_symlink():
        raise ValueError(f"{LEGACY_MARKER} is the marker of an earlier version of this script: make the copy again; nothing written")
    marker = tree / TLS_MARKER
    marker_present = marker.exists() or marker.is_symlink()
    tls_text = f"{pin} {TLS_PATCH_SHA256}"
    done_text = f"{tls_text} {PATCH_SHA256}"
    original = source.read_bytes()
    if digest(original) == PATCHED_SHA256:
        if marker.is_file() and not marker.is_symlink() and marker.read_text() == done_text + "\n":
            return "already prepared"
        raise ValueError(f"{RELATIVE_SOURCE} is patched but {TLS_MARKER} is missing or is not '<pin> <tls sha256> <isn sha256>'; nothing written")
    if digest(original) != BASE_SHA256:
        raise ValueError(f"{RELATIVE_SOURCE} is not the pinned Zephyr's; nothing written")
    if marker_present and (marker.is_symlink() or not marker.is_file() or marker.read_text() != tls_text + "\n"):
        raise ValueError(f"{TLS_MARKER} is not exactly '<pin> <tls sha256>' on this pin (the TLS patch comes first, and only once); nothing written")
    # --no-index isolates application from any enclosing Git worktree.
    with tempfile.TemporaryDirectory(prefix="crux-isn-fail-closed-") as directory:
        work = Path(directory)
        copied = work / RELATIVE_SOURCE
        copied.parent.mkdir(parents=True)
        copied.write_bytes(original)
        frozen = work / "isn.patch"
        frozen.write_bytes(patch_bytes)
        subprocess.run(["git", "apply", "--no-index", "--check", "--whitespace=error-all", str(frozen)], cwd=work, check=True)
        subprocess.run(["git", "apply", "--no-index", "--whitespace=error-all", str(frozen)], cwd=work, check=True)
        patched = copied.read_bytes()
    if digest(patched) != PATCHED_SHA256:
        raise ValueError("The patched file is not the expected one; nothing written")
    temporary = source.with_name(source.name + ".new")
    temporary.write_bytes(patched)
    os.replace(temporary, source)
    if not marker_present:
        return "prepared (standalone: no TLS marker, so no marker written)"
    temporary = marker.with_name(marker.name + ".new")
    temporary.write_text(done_text + "\n")
    os.replace(temporary, marker)
    return "prepared"


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("tree", type=Path, help="a build-local copy of the pinned Zephyr tree")
    args = parser.parse_args()
    try:
        state = prepare(args.tree, west_pin())
    except (ValueError, subprocess.CalledProcessError) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 1
    print(f"{state}: {args.tree} ({RELATIVE_SOURCE} sha256 {PATCHED_SHA256})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
