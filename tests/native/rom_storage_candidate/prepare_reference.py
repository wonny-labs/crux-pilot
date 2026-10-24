"""Compile the pinned, unmodified FCB using the candidate's exact compiler options.

Software-only artifact comparison. Writes reference artifacts beside the build.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shlex
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('build', type=Path)
parser.add_argument('workspace', type=Path)
args = parser.parse_args()
build, workspace = args.build.resolve(), args.workspace.resolve()
name = 'evkbmimxrt1060_flexspi_nor_config.c'
entries = [entry for entry in json.loads((build / 'compile_commands.json').read_text())
           if Path(entry['file']).name == name]
if len(entries) != 1 or Path(entries[0]['file']) != build / 'flash-target-candidate' / name:
    raise ValueError('expected one staged FCB compile input')
command = shlex.split(entries[0]['command'])
original = workspace / 'zephyr/boards/nxp/mimxrt1060_evk/xip' / name
if hashlib.sha256(original.read_bytes()).hexdigest() != '0ea7cf9246bac6643cad06396ff3f518ebf34b7793666646847e736eacb8c5f3':
    raise ValueError('unexpected original FCB')
reference = build.parent / 'original-fcb.obj'
command[command.index('-o') + 1] = str(reference)
command[command.index('-c') + 1] = str(original)
for flag in ['-MF', '-MT']:
    if flag in command:
        index = command.index(flag)
        del command[index:index + 2]
command = [arg for arg in command if arg != '-MD']
subprocess.run(command, cwd=entries[0]['directory'], check=True)
compiler = next(Path(arg) for arg in command if arg.endswith('arm-zephyr-eabi-gcc'))
objcopy = compiler.with_name('arm-zephyr-eabi-objcopy')
binary = build.parent / 'original-fcb.bin'
subprocess.run([str(objcopy), '-O', 'binary', '--only-section=.boot_hdr.conf',
                str(reference), str(binary)], check=True)
print(json.dumps({'reference_compile': command, 'sha256': hashlib.sha256(binary.read_bytes()).hexdigest(),
                  'bytes': len(binary.read_bytes()), 'compile_entries': entries}, indent=2))
