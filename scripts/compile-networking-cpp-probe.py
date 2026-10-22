#!/usr/bin/env python3
"""Compile a standard/public-only C++ probe with the native profile's actual flags."""
import json
from pathlib import Path
import shlex
import subprocess
import sys

commands, source, output = map(Path, sys.argv[1:])
entry = next(item for item in json.loads(commands.read_text())
             if item['file'].endswith('/samples/networking/client.c'))
arguments = entry.get('arguments') or shlex.split(entry['command'])
compiler = 1 if Path(arguments[0]).name == 'ccache' else 0
if not arguments[compiler].endswith('gcc'):
    raise SystemExit('Expected the qualified native GCC toolchain')
arguments[compiler] = arguments[compiler][:-3] + 'g++'
arguments[arguments.index('-o') + 1] = str(output.resolve())
arguments[arguments.index('-c') + 1] = str(source.resolve())
arguments = [arg for arg in arguments if arg not in {
    '-std=c17', '-Werror=implicit-int', '-Werror=implicit-function-declaration',
    '-Wno-pointer-sign'}]
arguments += ['-std=c++17', '-fno-exceptions', '-fno-rtti']
subprocess.run(arguments, cwd=entry['directory'], check=True)
print('Native standard/public C++ header and C-linkage probe compiled.')
