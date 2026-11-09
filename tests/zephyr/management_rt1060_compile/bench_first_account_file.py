#!/usr/bin/env python3
"""The first account's password file of the RT1060-EVKB bench image, and the scan for its plaintext (row C4, D3).

The bench image gets its first account from a password in a file that is NOT in the repository. This script does
the three jobs around that file, and prints a hash, a verdict or a reason, and never the password:

  check     the rules of the file. bench_first_account.cmake runs it at configuration, and a build with a missing,
            empty or too short file stops there. Prints {"sha256": ...} on success.
  generate  makes such a file, mode 0600, from the system's random source. Prints where it is and its hash.
  scan      searches build outputs, logs, UART captures and evidence for the password and for the encodings that a
            tool or a person leaves behind, and fails on any hit that is not the image itself.

The file: one line of printable ASCII (0x21 to 0x7e, no space), 16 to 128 bytes, with one optional newline at the
end. It is read by the bench build as the bytes it is, so the hash in the manifest is the hash of this file.
It must be private to its owner and outside every git work tree. docs/rt1060-management-image-feasibility.md
(row C4) says why the plaintext is in the linked image, and what the scan then promises.

Exit status: 0 good, 1 a refusal or a leak, 2 a usage or setup error. Python 3.9 or later.
"""
import argparse
import base64
import hashlib
import json
import os
import secrets
import stat
import sys
import urllib.parse
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
ENV_NAME = "CRUX_BENCH_PASSWORD_FILE"
# The same limits as bench_declaration.json ("first_account") and first_account/bench_first_account_provision.h.
# tests/build/test_bench_first_account_file.py and test_management_bench_declaration.py keep the three equal.
LENGTH_MIN = 16
LENGTH_MAX = 128
GENERATED_LENGTH = 24
FILE_SIZE_MAX = 4096
DISTINCT_MIN = 8
# The password of the native test images, which is public in this repository (crux_native_management.h). Only its
# hash is held here, so that this file does not repeat it.
PUBLIC_TEST_PASSWORD_SHA256 = "bbee7c04248dd55aaaa81085f5ae20c0841203c75a1c82bfad9ab866e4a7eb57"
GENERATED_ALPHABET = "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnpqrstuvwxyz23456789"
# Characters a path may not have: CMake writes it into an assembler string, and a semicolon splits a CMake list.
PATH_REFUSED = ('"', "\\", "\n", "\r", "\0", ";")


class FileRefused(Exception):
    """The file or the place is not acceptable. The message says what is wrong, never what the file holds."""


# ---- The rules of the file -------------------------------------------------------------------------------------


def _inside_work_tree(path):
    """The first folder above path that holds a .git entry, a folder in a clone and a file in a linked worktree."""
    for folder in [path.parent, *path.parent.parents]:
        if (folder / ".git").exists():
            return folder
    return None


def _check_place(path, repo_roots):
    resolved = Path(os.path.realpath(path))
    if _inside_work_tree(resolved) is not None:
        raise FileRefused("the file is inside a git work tree: keep it in a folder that is not part of any "
                          "repository, so that it cannot be committed")
    for root in repo_roots:
        root = Path(os.path.realpath(root))
        if resolved == root or root in resolved.parents:
            raise FileRefused("the file is inside the repository: keep it outside")
    return resolved


def validate(path, repo_roots=None):
    """Return {"sha256": ...} for a good file, or raise FileRefused. Reads at most FILE_SIZE_MAX bytes."""
    if not path or str(path) == "":
        raise FileRefused(f"no password file named: set {ENV_NAME} to the path of the file")
    path = Path(path)
    text = str(path)
    if any(character in text for character in PATH_REFUSED):
        raise FileRefused("the path has characters that this check does not accept (a quote, a backslash or a line "
                          "break)")
    if not os.path.lexists(path):
        raise FileRefused("the password file does not exist")
    try:
        info = os.stat(path)
    except OSError:
        raise FileRefused("the password file cannot be read") from None
    if not stat.S_ISREG(info.st_mode):
        raise FileRefused("the password path is not a regular file")
    roots = [REPO_ROOT] if repo_roots is None else list(repo_roots)
    _check_place(path, roots)
    if info.st_mode & 0o077:
        raise FileRefused("the password file is readable by its group or by others: make it private "
                          "(mode 600) before a build uses it")
    if info.st_size == 0:
        raise FileRefused("the password file is empty")
    if info.st_size > FILE_SIZE_MAX:
        raise FileRefused(f"the password file is too large: at most {FILE_SIZE_MAX} bytes")
    try:
        # At most one byte more than the limit, so that a file swapped for a large one after the size check costs nothing.
        with open(path, "rb") as handle:
            data = handle.read(FILE_SIZE_MAX + 1)
    except OSError:
        raise FileRefused("the password file cannot be read") from None
    if len(data) > FILE_SIZE_MAX:
        raise FileRefused(f"the password file is too large: at most {FILE_SIZE_MAX} bytes")
    body = data[:-1] if data.endswith(b"\n") else data
    if hashlib.sha256(body).hexdigest() == PUBLIC_TEST_PASSWORD_SHA256:
        raise FileRefused("that password is the native test images' password, which is public in this repository")
    if b"\n" in body or b"\r" in body:
        raise FileRefused("the password file must hold one line")
    if any(byte < 0x21 or byte > 0x7E for byte in body):
        raise FileRefused("the password must be printable ASCII with no space, tab or control character")
    if len(body) < LENGTH_MIN:
        raise FileRefused(f"the password is too short: at least {LENGTH_MIN} characters")
    if len(body) > LENGTH_MAX:
        raise FileRefused(f"the password is too long: at most {LENGTH_MAX} characters")
    if len(set(body)) < DISTINCT_MIN:
        raise FileRefused(f"the password needs at least {DISTINCT_MIN} distinct characters")
    return {"sha256": hashlib.sha256(data).hexdigest()}


def generate(path, repo_roots=None):
    """Create a new file with a random password, mode 0600, and return what validate returns for it."""
    path = Path(path)
    if any(character in str(path) for character in PATH_REFUSED):
        raise FileRefused("the path has characters that this check does not accept")
    roots = [REPO_ROOT] if repo_roots is None else list(repo_roots)
    _check_place(path, roots)
    password = "".join(secrets.choice(GENERATED_ALPHABET) for _ in range(GENERATED_LENGTH))
    try:
        descriptor = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    except FileExistsError:
        raise FileRefused("the file already exists: it is never overwritten") from None
    except OSError as error:
        raise FileRefused(f"the file cannot be created: {error.strerror}") from None
    try:
        with os.fdopen(descriptor, "w", encoding="ascii") as handle:
            handle.write(password + "\n")
    finally:
        password = None
    os.chmod(path, 0o600)
    return validate(path, repo_roots=roots)


# ---- The scan --------------------------------------------------------------------------------------------------

# Files of a build that are the image or a step towards it. They hold the plaintext in a bench build, because the
# Access API takes a plaintext password and the bytes of the file are embedded in the image (the report says why).
IMAGE_SUFFIXES = (".o", ".obj", ".a", ".elf")
IMAGE_NAMES = ("zephyr.bin", "zephyr.hex", "zephyr.s19", "zephyr.exe")
# Text made of hex digits: a run of the hex form of the password can occur in it by construction.
HEX_TEXT_SUFFIXES = (".hex", ".s19")
FILE_SCAN_MAX = 512 * 1024 * 1024


def is_image_file(path):
    name = path.name
    return name.endswith(IMAGE_SUFFIXES) or name in IMAGE_NAMES or (
        name.startswith("zephyr") and name.endswith((".bin", ".hex", ".s19")))


def _base64_runs(raw, alphabet_urlsafe):
    """The characters of the base64 text of raw that do not depend on the bytes around it, at each alignment."""
    runs = []
    for lead in range(3):
        padded = b"\x00" * lead + raw
        if alphabet_urlsafe:
            encoded = base64.urlsafe_b64encode(padded).decode().rstrip("=")
        else:
            encoded = base64.b64encode(padded).decode().rstrip("=")
        first = -(-lead * 8 // 6)
        last = (lead + len(raw)) * 8 // 6
        runs.append(encoded[first:last].encode())
    return runs


def needles(password):
    """(kind, bytes) pairs to search for. password is the bytes of the password, without the trailing newline."""
    text = password.decode("ascii")
    found = [("raw", password),
             ("reversed", password[::-1]),
             ("hex", password.hex().encode()),
             ("hex", password.hex().upper().encode()),
             ("c-escape", "".join(f"\\x{b:02x}" for b in password).encode()),
             ("c-array", ", ".join(f"0x{b:02x}" for b in password).encode()),
             ("c-array", ",".join(f"0x{b:02x}" for b in password).encode()),
             ("c-array", ", ".join(f"0x{b:02X}" for b in password).encode()),
             ("utf-16", text.encode("utf-16-le")),
             ("percent", urllib.parse.quote(text, safe="").encode()),
             ("json", json.dumps(text)[1:-1].encode())]
    found += [("base64", run) for run in _base64_runs(password, False)]
    found += [("base64url", run) for run in _base64_runs(password, True)]
    result = []
    seen = set()
    for kind, needle in found:
        if kind != "raw" and needle == password:
            continue  # an encoding that is the same text is the raw form
        if len(needle) >= 8 and (kind, needle) not in seen:
            seen.add((kind, needle))
            result.append((kind, needle))
    return result


def _read_password(path):
    try:
        data = Path(path).read_bytes()
    except OSError:
        raise FileRefused("the password file cannot be read") from None
    return data[:-1] if data.endswith(b"\n") else data


def _walk(root):
    """Regular files under root. Links are neither followed nor read, so the scan stays inside the tree."""
    for folder, directories, names in os.walk(root, followlinks=False):
        directories[:] = [d for d in directories if d != ".git"]
        for name in names:
            path = Path(folder) / name
            if not path.is_symlink() and path.is_file():
                yield path


def scan_tree(root, password, allow_in_image, skip=(), include_path=None):
    """Return a list of (kind, path) hits under root. Image files hold the raw form when allow_in_image says so."""
    hits = []
    all_needles = needles(password)
    skip = [Path(os.path.realpath(s)) for s in skip]
    for path in _walk(root):
        real = Path(os.path.realpath(path))
        if any(real == s or s in real.parents for s in skip):
            continue
        try:
            if path.stat().st_size > FILE_SCAN_MAX:
                hits.append(("unread-too-large", path))
                continue
            data = path.read_bytes()
        except OSError:
            hits.append(("unread", path))
            continue
        image = allow_in_image and is_image_file(path)
        hex_text = path.name.endswith(HEX_TEXT_SUFFIXES)
        for kind, needle in all_needles:
            # The raw bytes are what the image holds. A hex text file is not an image of bytes: it can hold a run of
            # hex digits by construction, and never the raw text.
            if kind == "raw" and image and not hex_text:
                continue
            if kind == "hex" and image and hex_text:
                continue
            if needle in data:
                hits.append((kind, path))
        if include_path is not None and include_path in data:
            hits.append(("path", path))
    return hits


def scan(password_file, builds, repos, extras, allow_in_image, expect, manifest, out):
    password = _read_password(password_file)
    if not password:
        raise FileRefused("the password file is empty")
    path_bytes = str(password_file).encode()
    leaks = []
    for build in builds:
        if not Path(build).is_dir():
            raise FileRefused(f"the build directory does not exist: {build}")
        leaks += scan_tree(build, password, allow_in_image)
    for repo in list(repos) + list(extras):
        if not Path(repo).is_dir():
            raise FileRefused(f"the folder to scan does not exist: {repo}")
        leaks += scan_tree(repo, password, False, skip=builds, include_path=path_bytes)
    for kind, path in leaks:
        print(f"LEAK {kind} {path}", file=out)
    failed = bool(leaks)
    for image in expect:
        try:
            count = Path(image).read_bytes().count(password)
        except OSError:
            print(f"EXPECTED the password once in {image}: the file cannot be read", file=out)
            failed = True
            continue
        if count != 1:
            print(f"EXPECTED the password once in {image}, found {count} times", file=out)
            failed = True
    if manifest is not None:
        try:
            recorded = json.loads(Path(manifest).read_text())["first_account"]["file_sha256"]
        except (OSError, ValueError, KeyError, TypeError):
            print(f"MANIFEST {manifest} has no first_account.file_sha256", file=out)
            failed = True
        else:
            actual = hashlib.sha256(Path(password_file).read_bytes()).hexdigest()
            if recorded != actual:
                print(f"MANIFEST {manifest} records another hash than the password file has", file=out)
                failed = True
    if not failed:
        places = len(builds) + len(repos) + len(extras)
        where = "image files may hold it" if allow_in_image else "no file may hold it, the image included"
        print(f"no plaintext of the password, or an encoding of it, in {places} scanned tree(s) ({where})",
              file=out)
    return 1 if failed else 0


# ---- The command line ------------------------------------------------------------------------------------------


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = parser.add_subparsers(dest="command", required=True)
    check = sub.add_parser("check", help="validate the file; print its hash as JSON")
    check.add_argument("--file", default=os.environ.get(ENV_NAME, ""))
    check.add_argument("--repo", action="append", default=None, help="a repository root the file may not be in")
    make = sub.add_parser("generate", help="create a random password file, mode 0600")
    make.add_argument("--file", required=True)
    make.add_argument("--repo", action="append", default=None)
    look = sub.add_parser("scan", help="search for the password and its encodings")
    look.add_argument("--password-file", default=os.environ.get(ENV_NAME, ""))
    look.add_argument("--build", action="append", default=[], help="a build directory (repeat)")
    look.add_argument("--repo", action="append", default=[], help="the repository, logs or evidence to search (repeat)")
    look.add_argument("--also", action="append", default=[], help="another folder: logs, UART captures")
    look.add_argument("--allow-in-image", action="store_true",
                      help="a bench build: objects, archives, ELF and binaries may hold the raw bytes")
    look.add_argument("--expect-in-image", action="append", default=[],
                      help="a linked image that must hold the raw bytes exactly once (proves the scan can see it)")
    look.add_argument("--manifest", help="bench-manifest.json: its first_account.file_sha256 must match the file")
    args = parser.parse_args(argv)
    try:
        if args.command == "check":
            print(json.dumps(validate(args.file, repo_roots=args.repo)))
            return 0
        if args.command == "generate":
            found = generate(args.file, repo_roots=args.repo)
            print(f"created {args.file} (mode 600, {GENERATED_LENGTH} characters), sha256 {found['sha256']}")
            return 0
        if not args.build and not args.repo and not args.also:
            parser.error("name something to scan: --build, --repo or --also")
        repos = args.repo
        if not args.password_file:
            raise FileRefused(f"no password file named: use --password-file or set {ENV_NAME}")
        return scan(args.password_file, args.build, repos, args.also, args.allow_in_image, args.expect_in_image,
                    args.manifest, sys.stdout)
    except FileRefused as refusal:
        if args.command == "scan":
            print(f"error: {refusal}", file=sys.stderr)
            return 2
        print(f"refused: {refusal}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
