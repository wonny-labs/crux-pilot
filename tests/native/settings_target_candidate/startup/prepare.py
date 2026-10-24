#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Apply the experimental patch to a disposable copy, never the pinned checkout."""
import argparse
import hashlib
from pathlib import Path
import subprocess
import tempfile

PIN = '684c9e8f32e4373a21098559f748f06915f950c9'
SOURCE_SHA = '21ad6c753b5bc8370502799568426ec69648ea41a8b0b263df0f3eb840f6767b'

def prepare(zephyr, output):
    zephyr = zephyr.resolve()
    output = output.resolve()
    if output == zephyr or zephyr in output.parents:
        raise RuntimeError('Candidate output must be outside the Zephyr checkout')
    source = zephyr / 'subsys/kvss/nvs/nvs.c'
    revision = subprocess.check_output(['git', '-C', str(zephyr), 'rev-parse', 'HEAD'], text=True).strip()
    dirty = subprocess.check_output(['git', '-C', str(zephyr), 'status', '--porcelain',
                                     '--', 'subsys/kvss/nvs'], text=True).strip()
    if dirty or revision != PIN or hashlib.sha256(source.read_bytes()).hexdigest() != SOURCE_SHA:
        raise RuntimeError('Pinned NVS source identity mismatch; no candidate generated')
    output.mkdir(parents=True, exist_ok=True)
    candidate = output / 'nvs.c'
    # Fresh inode and atomic replacement avoid following an existing candidate
    # symlink or overwriting a hardlinked upstream file.
    with tempfile.TemporaryDirectory(prefix='nvs-copy-', dir=output) as staging:
        staged = Path(staging) / 'nvs.c'
        staged.write_bytes(source.read_bytes())
        subprocess.run(['patch', '--batch', '--forward', '--fuzz=0', '-p1', '-i',
                        str(Path(__file__).with_name('startup-read.patch').resolve())],
                       cwd=staging, check=True, timeout=30)
        if hashlib.sha256(source.read_bytes()).hexdigest() != SOURCE_SHA:
            raise RuntimeError('Pinned source changed during copy preparation')
        staged.replace(candidate)
    return candidate

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--zephyr', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    print(prepare(args.zephyr, args.output))
