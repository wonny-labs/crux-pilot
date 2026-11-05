#!/usr/bin/env python3
"""Package a built web folder into firmware data for crux_web.

Reads a folder that any frontend tool built and writes:

- one C source with each file's bytes, compressed when that is smaller, a private asset table, and one
  CRUX_WEB_ROUTE per asset, so that the route generator makes one registered resource for each (W12);
- a JSON list of every asset's path, sizes and SHA-256, which is an input to the product's SBOM.

It fails the build, writing nothing, when the folder is missing or unusable, when the compressed size
or the file count is over budget, when an asset is too large for the response deadline, or when the
folder holds what looks like a secret or device data. The bundle is public, because a browser loads
it before login, so it must hold neither. It needs only Python's standard library, not Node.js.

The headers of each asset are in the generated source. The content policy is one of two fixed
values that components/web/assets/web_assets_internal.h holds, chosen with --style. No policy string
passes through this tool.
"""
import argparse
import gzip
import hashlib
import json
import os
from pathlib import Path
import re
import sys
import zlib

FORMAT = "crux-web-assets/1"
# Mirrors CRUX_WEB_RESPONSE_HEADERS_MAX in crux_web_adapter.h: three headers come from the policy macro.
RESPONSE_HEADERS_MAX = 11
SECURITY_HEADER_COUNT = 3
# The URL buffer of the HTTP server holds 255 bytes, and a request over it gets a plain 500.
PATH_MAX = 128
# The most that a gzip file may decode to, when it is checked.
DECODE_MAX = 16 * 1024 * 1024

# Declared defaults, for review in docs/web-assets.md.
DEFAULT_MAX_FILES = 32
DEFAULT_MAX_TOTAL_BYTES = 393216
DEFAULT_CLIENT_RATE = 32768

TYPES = {
    ".html": "text/html; charset=utf-8",
    ".js": "text/javascript; charset=utf-8",
    ".mjs": "text/javascript; charset=utf-8",
    ".css": "text/css; charset=utf-8",
    ".json": "application/json",
    ".webmanifest": "application/manifest+json",
    ".txt": "text/plain; charset=utf-8",
    ".svg": "image/svg+xml",
    ".png": "image/png",
    ".jpg": "image/jpeg",
    ".jpeg": "image/jpeg",
    ".gif": "image/gif",
    ".webp": "image/webp",
    ".avif": "image/avif",
    ".ico": "image/vnd.microsoft.icon",
    ".woff": "font/woff",
    ".woff2": "font/woff2",
}
TEXT_SUFFIXES = {".html", ".js", ".mjs", ".css", ".json", ".webmanifest", ".txt", ".svg"}
SEGMENT = re.compile(r"[A-Za-z0-9._~-]+\Z")
# A hash in a file name, set off by - or ., with a digit or a capital in it so that a word does not
# count: the token after the last separator only, so a hash that holds a hyphen stays revalidated.
FINGERPRINT = re.compile(r"[-.]([A-Za-z0-9_]{8,32})\.[A-Za-z0-9]+\Z")
CACHE_REVALIDATE = "no-cache"
CACHE_IMMUTABLE = "max-age=31536000, immutable"

SECRET_PATTERNS = [
    ("a PEM private key", re.compile(r"-----BEGIN [A-Z ]*PRIVATE KEY-----")),
    ("a PEM certificate", re.compile(r"-----BEGIN (?:TRUSTED |X509 )?CERTIFICATE-----")),
    ("a PEM block", re.compile(r"-----BEGIN [A-Z0-9 ]+-----")),
    ("a cloud access key", re.compile(r"\bAKIA[0-9A-Z]{16}\b")),
    ("a code host token", re.compile(r"\b(?:gh[pousr]_[A-Za-z0-9]{36,}|github_pat_[A-Za-z0-9_]{22,})")),
    ("a chat token", re.compile(r"\bxox[abprs]-[A-Za-z0-9-]{10,}")),
    ("a signed token", re.compile(r"\beyJ[A-Za-z0-9_-]{8,}\.eyJ[A-Za-z0-9_-]{8,}\.[A-Za-z0-9_-]+")),
    ("a hardware address", re.compile(r"\b(?:[0-9A-Fa-f]{2}[:-]){5}[0-9A-Fa-f]{2}\b")),
]


class AssetError(Exception):
    """A folder or an argument the packager refuses."""


def largest_asset(deadline_ms, client_rate):
    """The most compressed bytes that one response can send in the deadline at the declared rate."""
    return client_rate * deadline_ms // 1000


def fingerprinted(name):
    """Whether a file name holds a content hash, so its content cannot change under that name."""
    match = FINGERPRINT.search(name)
    if match is None:
        return False
    token = match.group(1)
    return any(character.isdigit() or character.isupper() for character in token)


def positive(text):
    try:
        value = int(text)
    except ValueError:
        raise argparse.ArgumentTypeError(f"{text!r} is not a whole number") from None
    if value < 1:
        raise argparse.ArgumentTypeError(f"{text!r} must be at least 1")
    return value


def collect(folder, errors):
    """The files of a folder as (url path, file path), checking what can be known from names alone."""
    if not folder.exists():
        raise AssetError(f"the web folder {folder} does not exist: build the frontend first")
    if not folder.is_dir():
        raise AssetError(f"{folder} is not a folder")
    found = []

    def unreadable(error):
        errors.append(f"{error.filename}: cannot be read ({error.strerror})")

    for directory, names, files in os.walk(folder, followlinks=False, onerror=unreadable):
        directory = Path(directory)
        for name in list(names):
            if (directory / name).is_symlink():
                errors.append(f"{(directory / name).relative_to(folder)}: a symbolic link, which could "
                              "reach outside the folder")
                names.remove(name)
        for name in files:
            path = directory / name
            relative = path.relative_to(folder)
            if path.is_symlink():
                errors.append(f"{relative}: a symbolic link, which could reach outside the folder")
            elif not path.is_file():
                errors.append(f"{relative}: not a regular file")
            else:
                found.append(("/" + "/".join(relative.parts), path))
    return sorted(found)


def check_names(found, errors):
    """The files that can be served, with a message for each that cannot."""
    folded = {}
    valid = []
    for url, path in found:
        parts = url[1:].split("/")
        relative = url[1:]
        if any(part.startswith(".") for part in parts):
            errors.append(f"{relative}: a hidden file or folder, which a built folder does not need")
            continue
        if not all(SEGMENT.match(part) for part in parts):
            errors.append(f"{relative}: cannot be a URL path: only letters, digits and . _ ~ - are served, "
                          "because the server compares a path without decoding it")
            continue
        if url == "/api" or url.startswith("/api/"):
            errors.append(f"{relative}: under /api, which is the API's: an exact resource here would "
                          "answer in front of a route with a path parameter")
            continue
        if len(url) > PATH_MAX:
            errors.append(f"{relative}: the path is too long, at most {PATH_MAX} bytes")
            continue
        if path.suffix.lower() not in TYPES:
            errors.append(f"{relative}: not a type this packager serves ({', '.join(sorted(TYPES))}), so "
                          "it has no Content-Type, and a type is never guessed")
            continue
        if path.stat().st_size == 0:
            errors.append(f"{relative}: an empty file")
            continue
        if url.lower() in folded:
            errors.append(f"{relative}: and {folded[url.lower()]} differ only in case")
        folded[url.lower()] = relative
        valid.append((url, path))
    return valid


def scan_secrets(found, deny, errors):
    for url, path in found:
        relative = url[1:]
        content = path.read_bytes()
        plain = content
        packed = pregzipped(content, relative, errors)
        if packed:
            plain = decoded(content)
        elif packed is None:
            continue
        if path.suffix.lower() in TEXT_SUFFIXES:
            text = plain.decode("utf-8", errors="replace")
            for label, pattern in SECRET_PATTERNS:
                if pattern.search(text):
                    errors.append(f"{relative}: looks like a secret or device data: {label}. The bundle "
                                  "is public, so remove it from the folder")
                    break
        for entry in deny:
            if entry.encode("utf-8") in content or entry.encode("utf-8") in plain:
                errors.append(f"{relative}: holds {entry}, which the deny file lists")


def read_deny(path):
    if path is None:
        return []
    deny_file = Path(path)
    if not deny_file.is_file():
        raise AssetError(f"the deny file {path} does not exist")
    entries = []
    for line in deny_file.read_text(encoding="utf-8").splitlines():
        entry = line.strip()
        if entry and not entry.startswith("#"):
            entries.append(entry)
    return entries


def pregzipped(content, relative, errors):
    """Whether a file is already gzip, which is served as it is. A damaged one is an error."""
    if content[:2] != b"\x1f\x8b":
        return False
    try:
        decoder = zlib.decompressobj(wbits=31)
        decoder.decompress(content, DECODE_MAX)
        if decoder.unconsumed_tail or not decoder.eof:
            raise zlib.error("too large or incomplete")
    except zlib.error:
        errors.append(f"{relative}: starts like gzip but is not valid gzip, or decodes to over {DECODE_MAX} bytes")
        return None
    return True


def decoded(content):
    return zlib.decompressobj(wbits=31).decompress(content, DECODE_MAX)


def compress(content):
    packed = gzip.compress(content, compresslevel=9, mtime=0)
    return ("gzip", packed) if len(packed) < len(content) else ("identity", content)


def build_assets(found):
    assets = []
    for url, path in found:
        content = path.read_bytes()
        # A file that is already gzip is served as it is, with its Content-Encoding, and not packed twice.
        precompressed = content[:2] == b"\x1f\x8b"
        encoding, served = ("gzip", content) if precompressed else compress(content)
        assets.append({
            "path": url,
            "file": url[1:],
            "type": TYPES[path.suffix.lower()],
            "encoding": encoding,
            "cache": "immutable" if fingerprinted(path.name) else "revalidate",
            "bytes": len(content),
            "sha256": hashlib.sha256(content).hexdigest(),
            "served_bytes": len(served),
            "served_sha256": hashlib.sha256(served).hexdigest(),
            "_served": served,
            **({"precompressed": True} if precompressed else {}),
        })
    return assets


def check_budget(assets, args, limit, errors):
    total = sum(asset["served_bytes"] for asset in assets)
    if len(assets) > args.max_files:
        errors.append(f"the folder has {len(assets)} files, at most {args.max_files} are allowed: browsers "
                      "open several connections and the server has few clients, so bundle the files")
    if total > args.max_total_bytes:
        errors.append(f"the compressed total is {total} bytes, over the budget of {args.max_total_bytes}")
    for asset in assets:
        if asset["served_bytes"] > limit:
            errors.append(f"{asset['path']}: {asset['served_bytes']} bytes served exceeds the largest asset "
                          f"that fits, {limit} bytes: the response deadline {args.response_deadline_ms} ms "
                          f"at {args.client_rate} bytes/s. Split the file, or set "
                          "CONFIG_CRUX_WEB_RESPONSE_DEADLINE_MS and the declared client rate together")
    return total


def c_bytes(data):
    rows = []
    for start in range(0, len(data), 16):
        rows.append("    " + ", ".join(f"0x{byte:02x}" for byte in data[start:start + 16]) + ",")
    return "\n".join(rows)


def render_c(assets, style):
    lines = [
        "/* Generated by components/web/tools/web_assets.py. Do not edit. */",
        f"#define CRUX_ASSETS_INLINE_STYLES {1 if style == 'inline' else 0}",
        '#include "web_assets_internal.h"',
        "",
    ]
    for index, asset in enumerate(assets):
        lines.append(f"/* {asset['path']}: {asset['bytes']} bytes, served as {asset['encoding']} */")
        lines.append(f"static const uint8_t crux_web_asset_data_{index:04d}[] = {{")
        lines.append(c_bytes(asset["_served"]))
        lines.append("};")
        lines.append(f"static const crux_web_header_t crux_web_asset_headers_{index:04d}[] = {{")
        lines.append(f'    {{"Content-Type", "{asset["type"]}"}},')
        if asset["encoding"] == "gzip":
            lines.append('    {"Content-Encoding", "gzip"},')
        cache = CACHE_IMMUTABLE if asset["cache"] == "immutable" else CACHE_REVALIDATE
        lines.append(f'    {{"Cache-Control", "{cache}"}},')
        lines.append("    CRUX_WEB_ASSETS_SECURITY_HEADERS,")
        lines.append("};")
        lines.append("")
    lines.append("const crux_web_asset_t crux_web_asset_table[] = {")
    for index, asset in enumerate(assets):
        count = 2 + (1 if asset["encoding"] == "gzip" else 0) + SECURITY_HEADER_COUNT
        lines.append(f'    {{"{asset["path"]}", crux_web_asset_data_{index:04d}, '
                     f"sizeof(crux_web_asset_data_{index:04d}), crux_web_asset_headers_{index:04d}, "
                     f"{count}u}},")
    lines.append("};")
    lines.append(f"const size_t crux_web_asset_count = {len(assets)}u;")
    lines.append("")
    routes = [(asset["path"], index) for index, asset in enumerate(assets)]
    routes.extend(("/", index) for index, asset in enumerate(assets) if asset["path"] == "/index.html")
    routes.sort()
    for number, (path, index) in enumerate(routes):
        lines.append(f'CRUX_WEB_ROUTE(crux_web_asset_route_{number:04d}, "{path}", CRUX_WEB_GET, '
                     f"CRUX_WEB_PUBLIC, crux_web_assets_serve, &crux_web_asset_table[{index}]);")
    return "\n".join(lines) + "\n", len(routes)


def render_list(assets, args, limit, total, routes):
    return json.dumps({
        "format": FORMAT,
        "style": args.style,
        "budget": {
            "max_files": args.max_files,
            "max_total_bytes": args.max_total_bytes,
            "max_asset_bytes": limit,
            "response_deadline_ms": args.response_deadline_ms,
            "client_rate_bytes_per_second": args.client_rate,
        },
        "totals": {"files": len(assets), "routes": routes, "bytes": sum(a["bytes"] for a in assets),
                   "served_bytes": total},
        "assets": [{key: value for key, value in asset.items() if not key.startswith("_")} for asset in assets],
    }, indent=2, sort_keys=True) + "\n"


def report(assets, args, limit, total):
    print(f"web assets: {len(assets)} files, {total} bytes served (compressed), style {args.style}")
    for asset in assets:
        print(f"  {asset['path']:<40} {asset['encoding']:<8} {asset['cache']:<10} "
              f"{asset['bytes']:>8} -> {asset['served_bytes']:>8}")
    print(f"largest asset that fits: {limit} bytes (CONFIG_CRUX_WEB_RESPONSE_DEADLINE_MS "
          f"{args.response_deadline_ms} ms at {args.client_rate} bytes/s)")


def write_if_changed(path, text):
    path = Path(path)
    if not path.exists() or path.read_text(encoding="utf-8") != text:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("folder", type=Path, help="The built folder")
    parser.add_argument("--c-out", type=Path, help="Output: the generated C source")
    parser.add_argument("--hash-list", type=Path, help="Output: the asset hash list, for the SBOM")
    parser.add_argument("--style", choices=("strict", "inline"), default="strict",
                        help="Content policy for styles: strict, or inline styles allowed")
    parser.add_argument("--max-files", type=positive, default=DEFAULT_MAX_FILES)
    parser.add_argument("--max-total-bytes", type=positive, default=DEFAULT_MAX_TOTAL_BYTES,
                        help="Budget for all compressed bytes")
    parser.add_argument("--response-deadline-ms", type=positive, required=True,
                        help="CONFIG_CRUX_WEB_RESPONSE_DEADLINE_MS of the image")
    parser.add_argument("--client-rate", type=positive, default=DEFAULT_CLIENT_RATE,
                        help="Slowest honest client, in bytes a second, that the deadline is sized for")
    parser.add_argument("--deny-file", help="Strings that must not appear in any file, one per line")
    args = parser.parse_args(argv)
    for output in (args.c_out, args.hash_list):
        if output is not None and output.exists():
            output.unlink()
    errors = []
    try:
        deny = read_deny(args.deny_file)
        found = collect(args.folder, errors)
        if not any(url == "/index.html" for url, _ in found):
            errors.append("the folder has no index.html at its top: / serves it")
        servable = check_names(found, errors)
        scan_secrets(servable, deny, errors)
        limit = largest_asset(args.response_deadline_ms, args.client_rate)
        assets = build_assets(servable)
        total = check_budget(assets, args, limit, errors)
    except AssetError as error:
        print(f"web_assets: {error}", file=sys.stderr)
        return 1
    if errors:
        for error in errors:
            print(f"web_assets: {error}", file=sys.stderr)
        print(f"web_assets: largest asset that fits: {limit} bytes (CONFIG_CRUX_WEB_RESPONSE_DEADLINE_MS "
              f"{args.response_deadline_ms} ms at {args.client_rate} bytes/s)", file=sys.stderr)
        return 1
    source, routes = render_c(assets, args.style)
    if args.c_out:
        write_if_changed(args.c_out, source)
    if args.hash_list:
        write_if_changed(args.hash_list, render_list(assets, args, limit, total, routes))
    report(assets, args, limit, total)
    return 0


if __name__ == "__main__":
    sys.exit(main())
