"""Check actual standalone component commands and final ELF for provider selection."""
import argparse
import json
from pathlib import Path
import re
import shlex
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('component', choices=('thread', 'sync'))
parser.add_argument('build', type=Path)
parser.add_argument('elf', type=Path)
args = parser.parse_args()
other = 'sync' if args.component == 'thread' else 'thread'
commands = json.loads((args.build / 'compile_commands.json').read_text())
files = [entry['file'].replace('\\', '/') for entry in commands]
assert any(f'/components/{args.component}/src/' in file for file in files), 'selected common source missing'
assert any(f'/components/{args.component}/ports/' in file for file in files), 'selected native provider missing'
assert not any(f'/components/{other}/' in file or '/components/execution/' in file for file in files), 'unselected provider compiled'
portable = [entry for entry in commands if Path(entry['file']).name in ('consumer.c', 'consumer.cpp')
            and 'thread_sync_consumer' in entry['file']]
assert {Path(entry['file']).name for entry in portable} == {'consumer.c', 'consumer.cpp'}
for entry in portable:
    for flag in entry.get('arguments') or shlex.split(entry['command']):
        assert not re.search(r'/components/[^/]+/(?:src|ports)/|/zephyr/include/|-D(?:__ZEPHYR__|KERNEL)|^-imacros$', flag), flag
        assert f'/components/{other}/include' not in flag, flag
symbols = subprocess.check_output(['readelf', '--symbols', '--wide', str(args.elf)], text=True)
required = 'crux_thread_create' if args.component == 'thread' else 'crux_mutex_create'
assert re.search(r'\b' + required + r'\b', symbols), 'selected API absent from final image'
forbidden = (r'\b(?:crux_mutex_|crux_semaphore_|sync_native_|sync_metadata)' if other == 'sync'
             else r'\b(?:crux_thread_|thread_native_|thread_metadata|worker_stacks)')
assert not re.search(forbidden, symbols), 'unselected capability present in final image'
assert not re.search(r'\bexec_native_', symbols), 'obsolete Execution provider linked'
print(f'{args.component}-only: C/C++ public commands and final provider selection passed')
