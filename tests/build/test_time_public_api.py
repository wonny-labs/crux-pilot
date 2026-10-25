"""Compile public typed clock/sleep contracts with supported C/C++ consumers."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

REPO = Path(__file__).resolve().parents[2]


class TypedTimeHeaders(unittest.TestCase):
    def compile(self, language, body):
        compiler = shutil.which(os.environ.get('CXX' if language == 'c++' else 'CC',
                                               'c++' if language == 'c++' else 'cc'))
        if compiler is None:
            self.skipTest(f'{language} compiler unavailable')
        with tempfile.TemporaryDirectory() as temporary:
            source = Path(temporary) / ('probe.cpp' if language == 'c++' else 'probe.c')
            source.write_text(body)
            command = [compiler, '-std=c++17' if language == 'c++' else '-std=c17',
                       '-Wall', '-Wextra', '-Wpedantic', '-Werror', '-fsyntax-only']
            for component in ('base', 'time', 'thread'):
                command.extend(['-I', str(REPO / 'components' / component / 'include')])
            return subprocess.run([*command, str(source)], text=True, capture_output=True)

    def test_each_header_is_self_contained_in_c17_and_cpp17(self):
        for language in ('c', 'c++'):
            for header in ('time_types.h', 'time.h', 'thread.h'):
                with self.subTest(language=language, header=header):
                    result = self.compile(language, f'#include <crux/{header}>\n')
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_duration_cannot_replace_timestamp_or_timestamp_output(self):
        for language in ('c', 'c++'):
            valid = self.compile(language,
                '#include <crux/thread.h>\n#include <crux/time.h>\n'
                'crux_status_t probe(void) { crux_monotonic_time_t t;\n'
                'return crux_time_get_monotonic(&t); }\n')
            self.assertEqual(valid.returncode, 0, valid.stdout + valid.stderr)
            for operation in ('crux_thread_sleep_until(d)', 'crux_time_get_monotonic(&d)'):
                with self.subTest(language=language, operation=operation):
                    result = self.compile(language,
                        '#include <crux/thread.h>\n#include <crux/time.h>\n'
                        'crux_status_t probe(void) { crux_duration_t d = crux_duration_from_ns(1);\n'
                        f'return {operation}; }}\n')
                    self.assertNotEqual(result.returncode, 0, 'invalid clock-domain call accepted')


if __name__ == '__main__':
    unittest.main()
