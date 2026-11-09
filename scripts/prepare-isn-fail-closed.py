#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Applies patches/zephyr/zephyr-isn-fail-closed.patch to a BUILD-LOCAL copy of the pinned Zephyr tree (row C11).

The patch changes one file, subsys/net/ip/tcp.c: the TCP initial sequence number (tcp_init_isn) refuses when it has no key
(sys_csrand_get() failed) or cannot hash, and the hash buffer has the size of a SHA-256 digest. See
docs/rt1060-isn-fail-closed.md. The tree is changed in place, so it must be a COPY: this refuses a Git checkout (a
directory with .git), and the west workspace's Zephyr is one. It never writes the tree unless every hash is the expected one.

  scripts/prepare-isn-fail-closed.py ZEPHYR_COPY

Order for the bench image: first scripts/prepare-wolf-tls-zephyr.sh ZEPHYR_COPY (the wolfSSL TLS socket patch, D8, which
makes the copy and writes .crux-wolf-tls), then this script on the same folder (which writes .crux-isn-fail-closed). The
two patches touch different files (sockets_tls.c and the random subsystem, and tcp.c): this script checks that tcp.c is
still the pinned one, so a TLS patch that touched it would be refused here. The bench guard (bench_guard.cmake) checks
both markers and both patched files' hashes at configuration. Without the wolf marker the script still works, because
native_sim tests of this patch alone use a copy of the pinned tree.
"""
import argparse
import hashlib
import os
import subprocess
import sys
import tempfile
from pathlib import Path

RELATIVE_SOURCE = Path("subsys/net/ip/tcp.c")
MARKER = ".crux-isn-fail-closed"
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
    marker = tree / MARKER
    marker_text = f"{pin} {PATCH_SHA256}"
    original = source.read_bytes()
    if digest(original) == PATCHED_SHA256:
        if marker.is_file() and marker.read_text().strip() == marker_text:
            return "already prepared"
        raise ValueError(f"{RELATIVE_SOURCE} is patched but {MARKER} is missing or names another patch; nothing written")
    if digest(original) != BASE_SHA256:
        raise ValueError(f"{RELATIVE_SOURCE} is not the pinned Zephyr's; nothing written")
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
    marker.write_text(marker_text + "\n")
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
