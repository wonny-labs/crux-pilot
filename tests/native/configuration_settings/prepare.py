#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Prepare only disposable pinned-source copies; reuse #44 and #45 candidates."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile


def prepare(zephyr, output):
    zephyr, output = zephyr.resolve(), output.resolve()
    if output == zephyr or zephyr in output.parents:
        raise RuntimeError('Output must be outside pinned checkout')
    previous = Path(__file__).resolve().parent.parent
    spec = importlib.util.spec_from_file_location('startup', previous / 'nvs_startup/prepare.py')
    startup = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(startup)
    startup.prepare(zephyr, output / 'nvs')
    # Additional D3 review correction: #45 intentionally remains unchanged.
    late_patch = Path(__file__).with_name('late-mount-read.patch').resolve()
    subprocess.run(['patch', '--batch', '--forward', '--fuzz=0', '-p1', '-i',
                    str(late_patch)], cwd=output / 'nvs', check=True)
    paths = ['subsys/settings/src/settings_store.c', 'subsys/settings/src/settings_nvs.c']
    dirty = subprocess.check_output(['git', '-C', str(zephyr), 'status', '--porcelain', '--', *paths], text=True)
    if dirty:
        raise RuntimeError('Settings source inputs modified')
    hashes = {}
    with tempfile.TemporaryDirectory(dir=output) as staging:
        staging = Path(staging)
        for relative in paths:
            content = (zephyr / relative).read_bytes()
            pinned = subprocess.check_output(['git', '-C', str(zephyr), 'show', startup.PIN + ':' + relative])
            if content != pinned:
                raise RuntimeError('Settings source identity mismatch')
            target = staging / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(content)
            hashes[relative] = hashlib.sha256(content).hexdigest()
        for name in ['settings_store', 'settings_nvs']:
            subprocess.run(['patch', '--batch', '--forward', '--fuzz=0', '-p1', '-i',
                            str(previous / 'settings_error' / (name + '.patch'))], cwd=staging, check=True)
        for relative in paths:
            target = output / Path(relative).name
            (staging / relative).replace(target)
            hashes[target.name] = hashlib.sha256(target.read_bytes()).hexdigest()
    hashes['pin'] = startup.PIN
    hashes['subsys/kvss/nvs/nvs.c'] = startup.SOURCE_SHA
    hashes['late-mount-read.patch'] = hashlib.sha256(late_patch.read_bytes()).hexdigest()
    hashes['nvs.c'] = hashlib.sha256((output / 'nvs/nvs.c').read_bytes()).hexdigest()
    # Never follow a pre-existing manifest symlink or write through a hardlink.
    with tempfile.TemporaryDirectory(prefix='manifest-', dir=output) as staging:
        manifest = Path(staging) / 'manifest.json'
        manifest.write_text(json.dumps(hashes, indent=2) + '\n')
        manifest.replace(output / 'manifest.json')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--zephyr', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    prepare(args.zephyr, args.output)
