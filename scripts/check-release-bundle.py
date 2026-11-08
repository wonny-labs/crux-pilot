#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Check a release bundle before anything is published. PROVISIONAL: a first shape, see docs/release-bundle-checker.md.

Adapted from release/check_bundle.py of wonny-labs/crux-controller at commit f03dc79 (Apache-2.0).
Changes: a ``--schema`` option and the platform schema docs/schemas/release-manifest.schema.json; the
schema keywords const, enum and items, and a refusal of any keyword the checker does not apply; the
hashes-only mode as the only mode; a refusal of symbolic links, of other items that are not regular files
and of a NUL byte; a message in place of a traceback for a missing bundle folder or an unreadable schema;
and a compiler flag in the path scan that must start a word, so that product-x/build/app is not refused.

The bundle is hashes and identities only. The check stops publication when the bundle

* has no valid ``release-manifest.json`` (schema in docs/schemas/release-manifest.schema.json);
* holds an image, ``.elf``, ``.map`` or similar file, or any file that starts like an ELF;
* holds a file with the bytes of the image (``image_sha256`` of the manifest);
* lists hashes that do not match its items, or holds an item the manifest does not list;
* has an item that is not plain UTF-8 text (a NUL byte is refused too), an Intel HEX or S-record dump,
  an archive, or a symbolic link
  (every bundle item is text, so anything else is refused);
* has an item with an absolute path of a build machine, or one of the private strings.

The path scan is a regular expression, not a proof: it looks for system roots at the start of a path-like
word (after URLs are removed), for a path after a compiler flag such as ``-I``, for Windows drive and UNC
paths, and for ``file://`` URIs. A form it does not know passes.

The private strings come from a file outside the repository, passed by path, one per line. Nothing in this
repository names them. The check warns when no list is given.

The check reads the bundle and writes nothing. It does not recompute ``firmware_identity``: the image is not
in the bundle, so it only checks the form of the value.

Exit status: 0 for a bundle that may be published, 1 for a refused bundle, 2 for a usage error.
"""

import argparse
import hashlib
import json
import os
import pathlib
import re
import stat
import sys

MANIFEST = "release-manifest.json"
DEFAULT_SCHEMA = pathlib.Path(__file__).resolve().parents[1] / "docs/schemas/release-manifest.schema.json"
FORBIDDEN_SUFFIXES = {".elf", ".map", ".bin", ".hex", ".srec", ".s19", ".uf2", ".img"}
ELF_MAGIC = b"\x7fELF"
ARCHIVE_MAGICS = (
    b"PK\x03\x04", b"\x1f\x8b", b"BZh", b"\xfd7zXZ\x00", b"7z\xbc\xaf\x27\x1c", b"\x28\xb5\x2f\xfd",
)
# Keywords that carry no rule, and the keywords that validate() applies. Any other keyword is refused.
ANNOTATION_KEYWORDS = {"$schema", "$id", "title", "description"}
APPLIED_KEYWORDS = {
    "type", "required", "properties", "additionalProperties", "pattern", "minimum", "const", "enum", "items",
}
SUPPORTED_KEYWORDS = ANNOTATION_KEYWORDS | APPLIED_KEYWORDS
SUPPORTED_TYPES = {"object", "string", "integer", "array"}
# Web and git URLs are removed before the path scan: their slashes are not file paths.
REMOTE_URL = re.compile(r"\b(?:https?|git|ssh)://\S+", re.IGNORECASE)
SYSTEM_ROOTS = (
    "Users|home|private|tmp|var|opt|usr|etc|root|mnt|srv|nix|Volumes|github|workspaces|__w|build|data"
)
# A root at the start of a path-like word, or right after a compiler flag such as -I or -L. A flag starts a
# word: the end of a name such as product-x is not one (the source pattern took it for one).
UNIX_ABSOLUTE = re.compile(
    rf"(?:(?<![A-Za-z0-9_.~/-])|(?<=-[A-Za-z])(?<![A-Za-z0-9_.~/]-[A-Za-z]))/(?:{SYSTEM_ROOTS})/\S+"
)
WINDOWS_ABSOLUTE = re.compile(r"(?<![A-Za-z0-9])[A-Za-z]:[\\/]\S")
UNC_PATH = re.compile(r"\\\\[^\\\s]+\\[^\\\s]")
FILE_URI = re.compile(r"file://\S*", re.IGNORECASE)
HEX_DUMP = re.compile(r"^(?::[0-9A-Fa-f]{10,}|S[0-9][0-9A-Fa-f]{6,})\s*$", re.MULTILINE)


def schema_problems(schema, where="schema"):
    """Name each keyword that validate() would silently skip, so that a schema cannot promise more."""
    problems = []
    if not isinstance(schema, dict):
        return [f"{where}: must be an object"]
    for keyword in schema:
        if keyword not in SUPPORTED_KEYWORDS:
            problems.append(f"{where}: keyword {keyword!r} is not supported by this checker")
    if "type" in schema and schema["type"] not in SUPPORTED_TYPES:
        problems.append(f"{where}: type {schema['type']!r} is not supported by this checker")
    for name, child in schema.get("properties", {}).items():
        problems.extend(schema_problems(child, f"{where}.{name}"))
    if isinstance(schema.get("items"), dict):
        problems.extend(schema_problems(schema["items"], f"{where}[]"))
    if isinstance(schema.get("additionalProperties"), dict):
        problems.extend(schema_problems(schema["additionalProperties"], f"{where}.*"))
    return problems


def validate(value, schema, where, problems):
    """Apply the subset of JSON Schema keywords that the manifest schema uses."""
    kind = schema.get("type")
    if kind == "object":
        if not isinstance(value, dict):
            problems.append(f"{where}: must be an object")
            return
        for name in schema.get("required", []):
            if name not in value:
                problems.append(f"{where}: {name} is required")
        properties = schema.get("properties", {})
        extra = schema.get("additionalProperties", True)
        for name, item in value.items():
            if name in properties:
                validate(item, properties[name], f"{where}.{name}", problems)
            elif extra is False:
                problems.append(f"{where}: {name} is not allowed")
            elif isinstance(extra, dict):
                validate(item, extra, f"{where}.{name}", problems)
    elif kind == "array":
        if not isinstance(value, list):
            problems.append(f"{where}: must be an array")
            return
        if isinstance(schema.get("items"), dict):
            for index, item in enumerate(value):
                validate(item, schema["items"], f"{where}[{index}]", problems)
    elif kind == "string":
        if not isinstance(value, str):
            problems.append(f"{where}: must be a string")
            return
        if "pattern" in schema and not re.search(schema["pattern"], value):
            problems.append(f"{where}: does not match {schema['pattern']}")
    elif kind == "integer":
        if isinstance(value, bool) or not isinstance(value, int):
            problems.append(f"{where}: must be an integer")
            return
        if "minimum" in schema and value < schema["minimum"]:
            problems.append(f"{where}: must be at least {schema['minimum']}")
    else:
        return
    if "const" in schema and value != schema["const"]:
        problems.append(f"{where}: must be {schema['const']!r}")
    if "enum" in schema and value not in schema["enum"]:
        problems.append(f"{where}: must be one of {schema['enum']!r}")


def path_matches(text):
    """The absolute paths found in a text, by the forms that the module docstring lists."""
    found = set(FILE_URI.findall(text))
    stripped = FILE_URI.sub(" ", REMOTE_URL.sub(" ", text))
    for pattern in (UNIX_ABSOLUTE, WINDOWS_ABSOLUTE, UNC_PATH):
        found.update(match.group(0) for match in pattern.finditer(stripped))
    return found


def read_schema(schema_path):
    """Return the schema and the problems found while loading it."""
    try:
        schema = json.loads(pathlib.Path(schema_path).read_text())
    except (OSError, ValueError) as error:
        return None, [f"schema {schema_path}: cannot be read ({error})"]
    return schema, schema_problems(schema)


def walk(bundle):
    """The regular files of the bundle by name, and the problems with anything else in it."""
    files, problems = {}, []
    for root, folders, names in os.walk(bundle, followlinks=False):
        for name in sorted(folders + names):
            path = pathlib.Path(root) / name
            relative = path.relative_to(bundle).as_posix()
            if path.is_symlink():
                problems.append(f"{relative}: a symbolic link must not be in a bundle")
            elif name in names:
                if stat.S_ISREG(path.lstat().st_mode):
                    files[relative] = path
                else:
                    problems.append(f"{relative}: not a regular file")
    return files, problems


def check(bundle_dir, private_strings=(), schema_path=DEFAULT_SCHEMA):
    """Return one message per problem, or an empty list when the bundle may be published."""
    bundle = pathlib.Path(bundle_dir)
    if not bundle.is_dir():
        return [f"{bundle}: not a folder"]
    files, problems = walk(bundle)

    manifest = None
    if MANIFEST not in files:
        problems.append(f"{MANIFEST}: missing")
    else:
        try:
            manifest = json.loads(files[MANIFEST].read_text())
        except ValueError as error:
            problems.append(f"{MANIFEST}: not valid JSON ({error})")
    if manifest is not None:
        schema, schema_errors = read_schema(schema_path)
        problems.extend(schema_errors)
        if schema is not None and not schema_errors:
            validate(manifest, schema, MANIFEST, problems)

    image_sha256 = manifest.get("image_sha256") if isinstance(manifest, dict) else None
    contents = {}
    for name, path in sorted(files.items()):
        data = contents[name] = path.read_bytes()
        if path.suffix.lower() in FORBIDDEN_SUFFIXES or data.startswith(ELF_MAGIC):
            problems.append(f"{name}: an image, ELF or map file must not be in a bundle")
            continue
        if image_sha256 and hashlib.sha256(data).hexdigest() == image_sha256:
            problems.append(f"{name}: holds the bytes of the image (image_sha256)")
            continue
        if data.startswith(ARCHIVE_MAGICS):
            problems.append(f"{name}: an archive must not be in a bundle")
            continue
        try:
            text = data.decode("utf-8")
        except UnicodeDecodeError:
            problems.append(f"{name}: not plain UTF-8 text, so it may be an image or other binary")
            continue
        if "\x00" in text:
            problems.append(f"{name}: holds a NUL byte, so it is not text")
            continue
        if HEX_DUMP.search(text):
            problems.append(f"{name}: looks like an Intel HEX or S-record image dump")
            continue
        for found in sorted(path_matches(text)):
            problems.append(f"{name}: absolute path {found!r}")
        lowered = text.lower()
        for private in private_strings:
            if private.lower() in lowered:
                problems.append(f"{name}: holds a private string from the list")

    if isinstance(manifest, dict) and isinstance(manifest.get("bundle_hashes"), dict):
        listed = manifest["bundle_hashes"]
        names = set(files) - {MANIFEST}
        for name in sorted(names - set(listed)):
            problems.append(f"{name}: not listed in bundle_hashes")
        for name in sorted(set(listed) - names):
            problems.append(f"{name}: listed in bundle_hashes but not in the bundle")
        for name in sorted(names & set(listed)):
            if hashlib.sha256(contents[name]).hexdigest() != listed[name]:
                problems.append(f"{name}: hash differs from bundle_hashes")
    return problems


def read_private_strings(path):
    stripped = (line.strip() for line in pathlib.Path(path).read_text().splitlines())
    return [line for line in stripped if line and not line.startswith("#")]


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("bundle")
    parser.add_argument("--schema", default=str(DEFAULT_SCHEMA), help="manifest schema (default: the platform schema)")
    parser.add_argument("--private-strings", help="file with one private string per line")
    args = parser.parse_args(argv)
    if args.private_strings:
        try:
            private = read_private_strings(args.private_strings)
        except (OSError, ValueError) as error:
            print(f"check-release-bundle: cannot read the private-string list ({error})", file=sys.stderr)
            return 2
    else:
        private = []
        print(
            "warning: no private-string list given, so private strings were not checked",
            file=sys.stderr,
        )
    problems = check(args.bundle, private, args.schema)
    for problem in problems:
        print(problem, file=sys.stderr)
    if problems:
        return 1
    print("bundle check passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
