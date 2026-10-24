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
        self.check_source('#include <errno.h>\n#include <time.h>\n#include <crux/execution.h>\n', 0)

    def test_rejects_native_and_private_compile_requirements(self):
        for flag in ['-I/tmp/zephyr/include', '-I/tmp/components/execution/src',
                     '-I/tmp/components/execution/host', '-I/tmp/components/execution/zephyr',
                     '-I/tmp/components/execution/ports/linux',
                     '-I/tmp/components/execution/ports/zephyr', '-D__ZEPHYR__=1']:
            with self.subTest(flag=flag), tempfile.TemporaryDirectory() as temp:
                record = Path(temp) / 'commands.json'
                record.write_text(json.dumps([{'file': str(REPO / 'tests/portable_execution/conformance.c'),
                    'directory': temp, 'arguments': ['cc', flag, '-c', 'consumer.c']}]))
                result = subprocess.run(['python3', str(CHECK), '--compile-commands', str(record)], capture_output=True, text=True)
                self.assertEqual(result.returncode, 1, result.stdout + result.stderr)

    def test_rejects_direct_provider_or_native_link(self):
        for links, expected in [('crux::execution', 0), ('crux::execution;zephyr_interface', 1),
                                ('crux_execution_host', 1), ('crux::execution;pthread', 1)]:
            with self.subTest(links=links), tempfile.TemporaryDirectory() as temp:
                record = Path(temp) / 'boundary.json'
                record.write_text(json.dumps({'links': links}))
                result = subprocess.run(['python3', str(CHECK), '--target-record', str(record)], capture_output=True, text=True)
                self.assertEqual(result.returncode, expected, result.stdout + result.stderr)

    def test_default_sources_pass_with_native_providers_present(self):
        result = subprocess.run(['python3', str(CHECK)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
