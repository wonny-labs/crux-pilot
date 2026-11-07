#!/usr/bin/env python3
"""Generate Zephyr's Mbed TLS entropy glue without its fallback, without modifying Zephyr."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

BASE_SHA256 = "302e373808c9bc019ab92523e92cdebb05bc45f0bf6e30c6a942bdce19982e6d"
PATCHED_SHA256 = "65ed87b0d7878df4e3a80a514a4244ecbae2908246e715fd77c19f30f6fc0403"
RELATIVE_SOURCE = Path("modules/mbedtls/zephyr_entropy.c")
PATCH = Path(__file__).resolve().parents[1] / "patches/zephyr/mbedtls-entropy-fail-closed.patch"


def digest(data):
    return hashlib.sha256(data).hexdigest()


def prepare(zephyr, output):
    zephyr = zephyr.resolve()
    output = output.resolve()
    if output.is_relative_to(zephyr):
        raise ValueError("Generated source must remain outside the Zephyr checkout")
    source = (zephyr / RELATIVE_SOURCE).resolve()
    original = source.read_bytes()
    if digest(original) != BASE_SHA256:
        raise ValueError("Unsupported upstream Mbed TLS entropy source; no generated source written")
    patch = PATCH.read_bytes()
    # --no-index isolates application from any enclosing Git worktree.
    with tempfile.TemporaryDirectory(prefix="crux-mbedtls-entropy-") as directory:
        work = Path(directory)
        copied = work / RELATIVE_SOURCE
        copied.parent.mkdir(parents=True)
        copied.write_bytes(original)
        frozen_patch = work / "correction.patch"
        frozen_patch.write_bytes(patch)
        subprocess.run(["git", "apply", "--no-index", "--check", "--whitespace=error-all", str(frozen_patch)], cwd=work, check=True)
        subprocess.run(["git", "apply", "--no-index", "--whitespace=error-all", str(frozen_patch)], cwd=work, check=True)
        corrected = copied.read_bytes()
    if digest(corrected) != PATCHED_SHA256:
        raise ValueError("Unexpected patched Mbed TLS entropy source; no generated source written")
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(corrected)
    metadata = {
        "upstream_source": str(source),
        "upstream_sha256": digest(original),
        "patch_sha256": digest(patch),
        "generated_sha256": digest(corrected),
        "scope": "MBEDTLS_PSA_DRIVER_GET_ENTROPY: a failed entropy read fails, with no sys_rand_get() fallback",
    }
    output.with_suffix(".json").write_text(json.dumps(metadata, indent=2) + "\n")
    return metadata


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("zephyr", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    prepare(args.zephyr.resolve(), args.output.resolve())


if __name__ == "__main__":
    main()
