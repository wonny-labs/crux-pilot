#!/usr/bin/env python3
"""Generate one pinned native driver correction without modifying Zephyr."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

BASE_SHA256 = "dace869f1ce96490d2d278fb9e807d4589cf4f264ea5b3189996068774ad8292"
PATCHED_SHA256 = "c65b7b84859eaf93a6bed2b89efba77f6ee8280120c2b463adf0d3c2264d22bf"
RELATIVE_SOURCE = Path("drivers/ethernet/mdio/mdio_nxp_enet.c")
PATCH = Path(__file__).resolve().parents[1] / "patches/zephyr/nxp-enet-mdio-completion.patch"


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
        raise ValueError("Unsupported upstream MDIO source; no generated source written")
    patch = PATCH.read_bytes()
    # --no-index isolates application from any enclosing Git worktree.
    with tempfile.TemporaryDirectory(prefix="crux-mdio-") as directory:
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
        raise ValueError("Unexpected patched MDIO source; no generated source written")
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(corrected)
    metadata = {
        "upstream_source": str(source),
        "upstream_sha256": digest(original),
        "patch_sha256": digest(patch),
        "generated_sha256": digest(corrected),
        "scope": "single-core RT1062 without zero-latency interrupts",
        "timeout_recovery": "fresh boot; no in-process poison reset",
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
