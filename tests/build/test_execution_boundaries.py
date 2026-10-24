"""Execution-specific enforcement; broad Issue #24 enforcement remains separate."""
from pathlib import Path
import json
import subprocess
import tempfile
import unittest

REPO = Path(__file__).resolve().parents[2]
CHECK = REPO / 'scripts/check-execution-boundaries.py'

class ExecutionBoundaryTest(unittest.TestCase):
    def check_source(self, source, expected):
        with tempfile.TemporaryDirectory() as temp:
            item = Path(temp) / 'consumer.c'
            item.write_text(source)
            result = subprocess.run(['python3', str(CHECK), '--source', str(item)], capture_output=True, text=True)
            self.assertEqual(result.returncode, expected, result.stdout + result.stderr)

    def test_rejects_native_headers_types_calls_and_conditionals(self):
        for source in ['#include <zephyr/kernel.h>\n', '#include <fsl_gpio.h>\n',
                       '#include <pthread.h>\n', 'struct k_mutex forbidden;\n',
                       'void f(void) { k_sleep(1); }\n', '#if defined(__ZEPHYR__)\n#endif\n']:
            with self.subTest(source=source): self.check_source(source, 1)

    def test_permits_iso_c_and_public_contract(self):
        self.check_source('#include <errno.h>\n#include <time.h>\n#include <crux/thread.h>\n#include <crux/sync.h>\n', 0)

    def test_rejects_native_and_private_compile_requirements(self):
        flags = ['-I/tmp/zephyr/include', '-D__ZEPHYR__=1']
        flags += [f'-I/tmp/components/{component}/{private}'
                  for component in ('thread', 'sync')
                  for private in ('src', 'host', 'zephyr', 'ports/linux', 'ports/zephyr')]
        for flag in flags:
            with self.subTest(flag=flag), tempfile.TemporaryDirectory() as temp:
                record = Path(temp) / 'commands.json'
                record.write_text(json.dumps([{'file': str(REPO / 'tests/portable_execution/conformance.c'),
                    'directory': temp, 'arguments': ['cc', flag, '-c', 'consumer.c']}]))
                result = subprocess.run(['python3', str(CHECK), '--compile-commands', str(record)], capture_output=True, text=True)
                self.assertEqual(result.returncode, 1, result.stdout + result.stderr)

    def test_standalone_consumer_compile_commands_are_checked(self):
        for source in ('consumer.c', 'consumer.cpp'):
            with self.subTest(source=source), tempfile.TemporaryDirectory() as temp:
                record = Path(temp) / 'commands.json'
                record.write_text(json.dumps([{
                    'file': str(REPO / 'tests/thread_sync_consumer' / source),
                    'directory': temp,
                    'arguments': ['cc', '-I/tmp/components/sync/ports/linux', '-c', source],
                }]))
                result = subprocess.run(['python3', str(CHECK), '--compile-commands', str(record)],
                                        capture_output=True, text=True)
                self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
                self.assertIn('native/private compile requirement', result.stderr)

    def test_rejects_direct_provider_or_native_link(self):
        for links, expected in [('crux::thread;crux::sync', 0),
                                ('crux::thread;crux::sync;zephyr_interface', 1),
                                ('crux_thread_host;crux_sync_host', 1),
                                ('crux::thread;crux::sync;pthread', 1),
                                ('crux::thread crux::sync', 1),
                                ('crux::thread', 1), ('crux::sync', 1)]:
            with self.subTest(links=links), tempfile.TemporaryDirectory() as temp:
                record = Path(temp) / 'boundary.json'
                record.write_text(json.dumps({'links': links}))
                result = subprocess.run(['python3', str(CHECK), '--target-record', str(record)], capture_output=True, text=True)
                self.assertEqual(result.returncode, expected, result.stdout + result.stderr)

    def test_default_sources_pass_with_native_providers_present(self):
        result = subprocess.run(['python3', str(CHECK)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
