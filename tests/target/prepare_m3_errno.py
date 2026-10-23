"""Freeze the selected target libc errno ABI from the actual network compile command."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shlex
import subprocess

NAMES = ('EIO', 'ENETDOWN', 'ENETUNREACH', 'ECONNRESET', 'ENOTCONN',
         'ETIMEDOUT', 'ECONNREFUSED', 'EHOSTUNREACH')


def digest(data):
    return hashlib.sha256(data).hexdigest()


def preprocess_command(entry):
    source = shlex.split(entry['command'])
    command = []
    skip = False
    for argument in source:
        if skip:
            skip = False
            continue
        if argument in ('-o', '-MF', '-MT', '-MQ'):
            skip = True
        elif argument not in ('-c', '-MD', '-MMD', entry['file']):
            command.append(argument)
    return command + ['-E', '-dM', '-H', '-x', 'c', '-']


def prepare(build, inputs, output, *, source_suffix='/network_client.c'):
    output.mkdir(parents=True, exist_ok=False)
    source = json.loads(inputs.read_text())['source']
    if source['dirty'] or not source['recoverable_from_git']:
        raise ValueError('Unfrozen target inputs')
    entries = json.loads((build / 'compile_commands.json').read_text())
    selected = [e for e in entries if e['file'].endswith(source_suffix)]
    if len(selected) != 1:
        raise ValueError('Missing/ambiguous actual network compilation')
    entry = selected[0]
    command = preprocess_command(entry)
    result = subprocess.run(command, input='#include <errno.h>\n', cwd=entry['directory'],
                            text=True, capture_output=True, timeout=15, check=True)
    (output / 'macros.txt').write_text(result.stdout)
    (output / 'header-trace.txt').write_text(result.stderr)
    values = {}
    for name in NAMES:
        matches = re.findall(r'^#define ' + name + r' ([0-9]+)$', result.stdout, re.MULTILINE)
        if len(matches) != 1:
            raise ValueError('Missing/non-literal selected errno: ' + name)
        values[name] = int(matches[0])
    headers = []
    for line in result.stderr.splitlines():
        match = re.fullmatch(r'\.+ (/.+)', line)
        if match and Path(match[1]).is_file() and match[1] not in headers:
            headers.append(match[1])
    if not any(path.endswith('/sys/errno.h') for path in headers):
        raise ValueError('No selected actual errno definitions')
    identities = []
    for number, name in enumerate(headers):
        path = Path(name)
        copy = output / ('header-' + str(number) + '-' + path.name)
        copy.write_bytes(path.read_bytes())
        identities.append(dict(original_path=name, copy=copy.name, sha256=digest(copy.read_bytes())))
    compiler_copy = output / 'compiler-executable.bin'
    compiler_copy.write_bytes(Path(command[0]).read_bytes())
    artifacts = [dict(path=p.name, sha256=digest(p.read_bytes())) for p in sorted(output.iterdir()) if p.is_file()]
    record = dict(schema=1, values=values, image_bin_sha256=digest((build / 'zephyr/zephyr.bin').read_bytes()),
                  source_commit=source['git_commit'], compile_command=entry,
                  compile_command_sha256=digest(json.dumps(entry, sort_keys=True).encode()),
                  compiler=dict(path=command[0], artifact=compiler_copy.name, sha256=digest(compiler_copy.read_bytes())),
                  preprocessing_command=command, headers=identities, artifacts=artifacts)
    (output / 'errno-map.json').write_text(json.dumps(record, indent=2) + '\n')
    return record


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('build', 'inputs', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(prepare(args.build, args.inputs, args.output)['values']))
