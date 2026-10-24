#!/usr/bin/env python3
"""Bounded Issue #25 checks; not the general dependency policy of Issue #24."""
import argparse
import json
from pathlib import Path
import re
import shlex
import sys

ROOT = Path(__file__).resolve().parents[1]
CONSUMERS = (
    ROOT / 'tests/portable_execution/conformance.c',
    ROOT / 'tests/execution_runtime/src/runtime_fixture.c',
    ROOT / 'tests/execution_firmware/src/consumer.c',
    ROOT / 'tests/execution_firmware/src/main.cpp',
)
PUBLIC = ROOT / 'components/execution/include'
HEADER = re.compile(r'#\s*include\s*[<"](?:zephyr/|fsl_|board\.h|cmsis|pthread\.h|semaphore\.h|sched\.h|sys/|unistd\.h)')
NATIVE = re.compile(r'\b(?:k_[a-zA-Z_]\w*|pthread_\w+|sem_t|sem_(?:init|post|wait|timedwait|trywait)|clock_gettime|clock_nanosleep|nanosleep|fsl_\w+)\b')
ENVIRONMENT = re.compile(r'^\s*#\s*(?:if|ifdef|ifndef).*\b(?:__ZEPHYR__|CONFIG_\w+|__linux__|_WIN32|BOARD_\w+)\b', re.MULTILINE)
PRIVATE_PATH = re.compile(r'/components/execution/(?:src|host|zephyr|ports)(?:/|$)')
NATIVE_PATH = re.compile(r'/(?:zephyr/(?:include|arch|soc|boards|subsys|scripts)|modules/hal)(?:/|$)')

def source_errors(path):
    source = path.read_text()
    return [f'{path}: forbidden native dependency: {match.group(0)}'
            for pattern in (HEADER, NATIVE, ENVIRONMENT) for match in pattern.finditer(source)]

def command_errors(path):
    errors = []
    seen = 0
    for entry in json.loads(path.read_text()):
        item = Path(entry['file'])
        if not item.is_absolute():
            item = Path(entry['directory']) / item
        if item.resolve() not in CONSUMERS:
            continue
        seen += 1
        arguments = entry.get('arguments') or shlex.split(entry['command'])
        for argument in arguments:
            normal = argument.replace('\\', '/')
            if (PRIVATE_PATH.search(normal) or NATIVE_PATH.search(normal) or
                    re.match(r'-D(?:__ZEPHYR__|KERNEL)(?:=|$)', normal) or
                    normal == '-imacros'):
                errors.append(f'{item}: native/private compile requirement: {argument}')
    if seen == 0:
        errors.append(f'{path}: no designated consumer compile commands found')
    return errors

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, action='append', default=[])
    parser.add_argument('--compile-commands', type=Path, action='append', default=[])
    parser.add_argument('--target-record', type=Path, action='append', default=[])
    args = parser.parse_args()
    sources = args.source or (list(CONSUMERS) + list(PUBLIC.rglob('*.h')))
    errors = []
    for source in sources:
        errors.extend(source_errors(source))
    for commands in args.compile_commands:
        errors.extend(command_errors(commands))
    for record in args.target_record:
        links = json.loads(record.read_text())['links'].split(';')
        if links != ['crux::execution']:
            errors.append(f'{record}: consumer must link only crux::execution, got {links}')
    if errors:
        print('\n'.join(errors), file=sys.stderr)
        return 1
    print('Execution public/consumer boundary passed; native providers remain permitted.')
    return 0

if __name__ == '__main__':
    raise SystemExit(main())
