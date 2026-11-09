#!/usr/bin/env python3
"""Refuses a linked bench image that holds RSA code (row C9, D11 of docs/rt1060-management-image-feasibility.md).

The declared profile uses TLS 1.3 with P-256, SSH with Ed25519 and ecdsa-sha2-nistp256, and ECDSA and Ed25519 stored
keys. None of it needs RSA, so the bench image is built with CONFIG_WOLFSSL_RSA off (NO_RSA in wolfCrypt). The three
guard levels refuse that setting being on. This is the fourth check, on the outcome: it reads the symbol table of the
linked image and fails when a defined symbol is RSA code or the single-precision math that only RSA and Diffie-Hellman
use. A module update or another definition that brings RSA back with the setting still off is caught here.

  check_bench_rsa_symbols.py [--nm NM] IMAGE

A few names of the trimmed image still hold the letters RSA, and ALLOWED lists them with the kind of symbol and the
largest size that each may have: wolfPSA's NO_RSA stubs, which return PSA_ERROR_NOT_SUPPORTED in 14 bytes (the real
functions are hundreds of bytes), and wolfSSL's PEM labels, which are constant strings. An allowed name with another
kind, a larger size, or no size at all is refused, so the real function or other data under the same name does not
pass. The size needs `nm -S`: a Mach-O object has none, so a host test of this script uses a stand-in nm for that.

Exit 0: no such symbol. Exit 1: at least one, and every one is named. Exit 2: the check could not read the image
(a missing file, an nm that fails, an image with no defined symbols), which is never a pass.
"""
import argparse
import re
import subprocess
import sys

# A symbol is refused when one of these matches its name. The patterns are case sensitive, so that the letters "rsa"
# inside a longer word (universal, traversal, parsable) do not match: wolfCrypt writes the algorithm as Rsa, RSA or
# _rsa, and the single-precision math of RSA and DH by size.
DENY = (
    r"Rsa(?![a-z])",
    r"(?<![A-Z])RSA(?![A-Z])",
    r"(?<![A-Z])RSA[A-Z][a-z]",
    r"(?<![A-Za-z])rsa(?![a-z])",
    r"^sp_(?:ModExp|RsaPrivate|RsaPublic)_",
    r"^sp_(?:2048|3072|4096)_",
)


ALLOWED = {
    "psa_asymmetric_sign_rsa": ("Tt", 64),
    "psa_asymmetric_verify_rsa": ("Tt", 64),
    "psa_asymmetric_encrypt_rsa": ("Tt", 64),
    "psa_asymmetric_decrypt_rsa": ("Tt", 64),
    "BEGIN_RSA_PRIV": ("rRdD", 48),
    "BEGIN_RSA_PUB": ("rRdD", 48),
    "END_RSA_PRIV": ("rRdD", 48),
    "END_RSA_PUB": ("rRdD", 48),
}


def defined_symbols(nm, image):
    """[(name, type, size)] of the symbols that the image defines, from `nm -P -S`: name, type, value, size."""
    try:
        result = subprocess.run([nm, "-P", "-S", str(image)], capture_output=True, text=True, check=False)
    except OSError as error:
        raise SystemExit(f"cannot run {nm}: {error}") from error
    if result.returncode != 0:
        raise SystemExit(f"{nm} failed on {image} (exit {result.returncode}): {result.stderr.strip()}")
    symbols = []
    for line in result.stdout.splitlines():
        fields = line.split()
        if len(fields) < 2:
            continue
        name, kind = fields[0], fields[1]
        if kind in ("U", "u", "w", "v"):
            continue  # a reference or a weak reference needs a definition elsewhere, which is what is looked for
        try:
            size = int(fields[3], 16) if len(fields) > 3 else 0
        except ValueError:
            size = 0
        symbols.append((name, kind, size))
    return symbols


def refused(name, kind, size):
    """True when the symbol is RSA code. A host object may carry one more leading underscore (Mach-O), so both forms
    count. A name of ALLOWED passes only as the kind of symbol and up to the size that its entry says."""
    forms = [name, name[1:]] if name.startswith("_") else [name]
    if not any(re.search(pattern, form) for pattern in DENY for form in forms):
        return False
    for form in forms:
        if form in ALLOWED:
            kinds, largest = ALLOWED[form]
            if kind in kinds and 0 < size <= largest:
                return False
    return True


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("image", help="the linked ELF image, or an object")
    parser.add_argument("--nm", default="nm", help="the nm of the toolchain that linked it")
    args = parser.parse_args()
    try:
        symbols = defined_symbols(args.nm, args.image)
    except SystemExit as error:
        print(f"check_bench_rsa_symbols: {error}", file=sys.stderr)
        sys.exit(2)
    if not symbols:
        print(f"check_bench_rsa_symbols: {args.image} has no defined symbols: nothing was checked", file=sys.stderr)
        sys.exit(2)
    found = sorted({f"{name} ({kind}, {size} B)" for name, kind, size in symbols if refused(name, kind, size)})
    if found:
        print(f"check_bench_rsa_symbols: the bench image links RSA code (D11, row C9), {len(found)} symbols:",
              file=sys.stderr)
        for name in found:
            print(f"  {name}", file=sys.stderr)
        return 1
    print(f"check_bench_rsa_symbols: {len(symbols)} defined symbols, none of them RSA code")
    return 0


if __name__ == "__main__":
    sys.exit(main())
