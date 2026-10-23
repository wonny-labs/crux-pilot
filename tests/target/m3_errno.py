"""Verify compiler-derived target errno identity; no Linux-number fallback."""
import hashlib
import json
from pathlib import Path
import re
from m3_health_oracle import require
from prepare_m3_errno import NAMES, preprocess_command


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def verify_errno_map(path, image, *, source_suffix='/network_client.c'):
    path = Path(path)
    record = json.loads(path.read_text())
    require(record['schema'] == 1 and record['source_commit'] == image['source']['git_commit'] and
            record['image_bin_sha256'] == sha(image['binary']), 'Errno map belongs to a different image/source')
    require(set(record['values']) == set(NAMES) and all(type(v) is int and v > 0
            for v in record['values'].values()), 'Incomplete target errno ABI')
    paths = []
    for artifact in record['artifacts']:
        require(Path(artifact['path']).name == artifact['path'], 'Unsafe errno evidence path')
        actual = path.parent / artifact['path']
        require(sha(actual) == artifact['sha256'], 'Changed errno macro/header evidence')
        paths.append(artifact['path'])
    require(len(paths) == len(set(paths)) and 'macros.txt' in paths and 'header-trace.txt' in paths,
            'Missing/duplicate compiler errno evidence')
    macros = (path.parent / 'macros.txt').read_text()
    for name, value in record['values'].items():
        require(re.findall(r'^#define ' + name + r' ([0-9]+)$', macros, re.MULTILINE) == [str(value)],
                'Errno value differs from selected compiler output')
    trace = (path.parent / 'header-trace.txt').read_text()
    require(record['headers'] and any(h['original_path'].endswith('/sys/errno.h') for h in record['headers']),
            'Actual target errno definitions absent')
    for header in record['headers']:
        require(header['copy'] in paths and sha(path.parent / header['copy']) == header['sha256'] and
                header['original_path'] in trace, 'Selected errno header identity differs')
    entry = record['compile_command']
    require(hashlib.sha256(json.dumps(entry, sort_keys=True).encode()).hexdigest() ==
            record['compile_command_sha256'] and re.fullmatch(r'[0-9a-f]{64}', record['compiler']['sha256']),
            'Compiler identity/command changed')
    expected_command = preprocess_command(entry)
    require(record['preprocessing_command'] == expected_command and
            record['compiler']['path'] == expected_command[0] and
            record['compiler']['artifact'] in paths and
            sha(path.parent / record['compiler']['artifact']) == record['compiler']['sha256'],
            'Compiler executable or exact preprocessing command differs')
    compile_paths = [Path(a['path']) for a in image['artifacts'] if Path(a['path']).name == 'compile_commands.json']
    require(len(compile_paths) == 1, 'No unique image compilation database')
    commands = json.loads(compile_paths[0].read_text())
    require([e for e in commands if e['file'].endswith(source_suffix)] == [entry],
            'Errno probe command is not the image network compilation')
    return record['values']
