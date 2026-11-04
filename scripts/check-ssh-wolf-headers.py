#!/usr/bin/env python3
"""Check that wolfSSL and wolfSSH agree on what they share, although only wolfSSH undefines NO_FILESYSTEM.

Preprocesses the wolfCrypt headers that wolfSSH includes, with the flags of a wolfSSH source from a finished
build, in the order wolfSSH sees them, with and without that #undef. The outputs must be identical, or a type or
declaration differs between the two. wolfssl/ssl.h is known to differ, so it is the control that the comparison
can see a difference. Repeat this on any upgrade of the stacks. It reads a build directory and writes nothing.
"""
import argparse
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

HEADERS = ("wc_port", "types", "memory", "error-crypt", "random", "hash", "sha", "sha256", "sha512", "hmac", "aes",
           "kdf", "coding", "asn", "asn_public", "signature", "dh", "rsa", "ecc", "curve25519", "ed25519")
LATER_CONTROL = "wolfssl/ssl.h"


def compile_flags(build):
    entries = json.loads((build / "compile_commands.json").read_text(encoding="utf-8"))
    entry = next((item for item in entries if item["file"].endswith("wolfssh/src/internal.c")), None)
    if entry is None:
        raise SystemExit("No wolfSSH source in this build")
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
        elif argument not in ("-c", "-MD", "-MMD") and not argument.endswith(".c"):
            flags.append(argument)
    return arguments[0], flags, entry["directory"]


def preprocess(compiler, flags, directory, headers, undefine):
    source = "#include <wolfssl/wolfcrypt/settings.h>\n#include <wolfssl/wolfcrypt/types.h>\n"
    if undefine:
        source += "#undef NO_FILESYSTEM\n"
    source += "".join(f"#include <{header}>\n" for header in headers)
    with tempfile.NamedTemporaryFile("w", suffix=".c") as stream:
        stream.write(source)
        stream.flush()
        result = subprocess.run([compiler, *flags, "-E", "-P", stream.name], cwd=directory, capture_output=True,
                                text=True, check=False)
    if result.returncode != 0:
        raise SystemExit(result.stderr[:4000])
    return [line.strip() for line in result.stdout.splitlines() if line.strip()]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build", type=Path)
    build = parser.parse_args().build
    compiler, flags, directory = compile_flags(build)
    shared = [f"wolfssl/wolfcrypt/{name}.h" for name in HEADERS]
    if preprocess(compiler, flags, directory, shared, False) != preprocess(compiler, flags, directory, shared, True):
        print("wolfCrypt headers differ with NO_FILESYSTEM undefined, so wolfSSH and wolfSSL may disagree.", file=sys.stderr)
        return 1
    control = [*shared, LATER_CONTROL]
    if preprocess(compiler, flags, directory, control, False) == preprocess(compiler, flags, directory, control, True):
        print(f"Control failed: {LATER_CONTROL} should differ, so this comparison sees nothing.", file=sys.stderr)
        return 1
    print(f"{len(shared)} wolfCrypt headers are identical either way, and the control {LATER_CONTROL} differs.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
