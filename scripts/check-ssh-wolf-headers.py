#!/usr/bin/env python3
"""Check that nothing wolfSSL and wolfSSH share depends on NO_FILESYSTEM.

wolfSSL keeps NO_FILESYSTEM, and crux_wolfssh_settings.h undefines it for wolfSSH. Each wolfSSH source in a finished
build's compile_commands.json is preprocessed twice with its real flags: as built, and with a copy of the settings
file that lacks that #undef, which is what wolfSSL itself sees. The text from each wolfSSL header is then compared.
Some sources read a header before the #undef takes effect, so it cannot differ there, and others read it after.

Only wolfcrypt/wc_port.h may differ, and only by exactly ALLOWED_LINES, in that order. They are file functions and a file
type that do not exist in wolfSSL's own build, and no structure that the two share mentions them. Anything else fails this
check, including one of those lines added elsewhere. So does finding no difference at all, which would mean the
comparison sees nothing. Repeat this on any upgrade of the stacks. It reads a
build directory and writes only temporary files.
"""
import argparse
import difflib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tempfile

SETTINGS = "crux_wolfssh_settings.h"
ALLOWED_HEADER = "wolfcrypt/wc_port.h"
UNDEFINE = "#undef NO_FILESYSTEM"
WOLFSSL_HEADER = re.compile(r"/wolfssl/wolfssl/.+\.h$")
MARKER = re.compile(r'^# \d+ "([^"]+)"')
# The preprocessed lines of wolfcrypt/wc_port.h, whole and in order: wolfSSL's file functions, which its own build leaves
# out, and ReadDirCtx, the type that only those functions take.
ALLOWED_LINES = [
    "struct fs_file_t* z_fs_open(const char* filename, const char* mode);",
    "int z_fs_close(struct fs_file_t* file);",
    "int z_fs_rewind(struct fs_file_t* file);",
    "int wc_FileLoad(const char* fname, unsigned char** buf,",
    "size_t* bufLen, void* heap);",
    "typedef struct ReadDirCtx {",
    "struct fs_dirent entry;",
    "struct fs_dir_t dir;",
    "struct fs_dirent s;",
    "struct fs_dir_t* dirp;",
    "char name[(260 + 1)];",
    "} ReadDirCtx;",
    "int wc_ReadDirFirst(ReadDirCtx* ctx, const char* path, char** name);",
    "int wc_ReadDirNext(ReadDirCtx* ctx, const char* path, char** name);",
    "void wc_ReadDirClose(ReadDirCtx* ctx);",
    "int wc_FileExists(const char* fname);",
]


def sources_of(build):
    entries = json.loads((build / "compile_commands.json").read_text(encoding="utf-8"))
    found = [entry for entry in entries if re.search(r"/wolfssh/src/[^/]+\.c$", entry["file"])]
    if not found:
        raise SystemExit("No wolfSSH source in this build")
    return found


def compiler_flags(entry):
    arguments = shlex.split(entry["command"])
    if os.path.basename(arguments[0]) == "ccache":
        arguments = arguments[1:]
    flags = []
    skip = False
    for argument in arguments[1:]:
        if skip:
            skip = False
        elif argument in ("-o", "-MF", "-MT"):
            skip = True
        elif argument not in ("-c", "-MD", "-MMD"):
            flags.append(argument)
    return arguments[0], flags


def text_by_header(output):
    """Maps each wolfSSL header to the lines that came from it, keeping their order."""
    headers = {}
    current = None
    for line in output.splitlines():
        match = MARKER.match(line)
        if match:
            current = match.group(1) if WOLFSSL_HEADER.search(match.group(1)) else None
        elif current is not None and line.strip():
            headers.setdefault(current, []).append(" ".join(line.split()))
    return headers


def preprocess(entry, extra):
    compiler, flags = compiler_flags(entry)
    result = subprocess.run([compiler, *extra, *flags, "-E"], cwd=entry["directory"], capture_output=True,
                            text=True, check=False)
    if result.returncode != 0:
        raise SystemExit(f"{entry['file']}: {result.stderr[:2000]}")
    return text_by_header(result.stdout)


def settings_without_undefine():
    source = Path(__file__).resolve().parents[1] / "components/ssh/ports/zephyr" / SETTINGS
    text = source.read_text(encoding="utf-8")
    if UNDEFINE not in text:
        raise SystemExit(f"{SETTINGS} no longer holds {UNDEFINE}: this check needs a review")
    return text.replace(UNDEFINE, "")


def changed_lines(before, after):
    return [line[1:] for line in difflib.ndiff(before, after) if line[:2] in ("- ", "+ ")]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build", type=Path)
    build = parser.parse_args().build
    unexpected = []
    differing = {}
    compared = set()
    entries = sources_of(build)
    with tempfile.TemporaryDirectory() as directory:
        (Path(directory) / SETTINGS).write_text(settings_without_undefine(), encoding="utf-8")
        for entry in entries:
            source = Path(entry["file"]).name
            real = preprocess(entry, [])
            baseline = preprocess(entry, [f"-I{directory}"])
            for header in sorted(set(real) | set(baseline)):
                compared.add(header)
                name = header.split("/wolfssl/wolfssl/")[-1]
                changed = changed_lines(baseline.get(header, []), real.get(header, []))
                changed = [line.strip() for line in changed]
                if not changed:
                    continue
                differing.setdefault(name, set()).add(source)
                if name != ALLOWED_HEADER or changed != ALLOWED_LINES:
                    unexpected.extend(f"{source}: {name}: {line}" for line in changed)
    for line in unexpected:
        print(line, file=sys.stderr)
    if unexpected:
        print(f"{len(unexpected)} lines differ in a way that the list does not allow.", file=sys.stderr)
        return 1
    if not differing:
        print("Control failed: no header differed, so this comparison sees nothing.", file=sys.stderr)
        return 1
    print(f"{len(entries)} wolfSSH sources and {len(compared)} wolfSSL headers compared.")
    for name, sources in sorted(differing.items()):
        print(f"{name} differs by exactly the listed lines, in: {', '.join(sorted(sources))}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
