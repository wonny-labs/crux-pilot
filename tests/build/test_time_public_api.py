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
            for component in ('base', 'time', 'thread', 'delay'):
                command.extend(['-I', str(REPO / 'components' / component / 'include')])
            return subprocess.run([*command, str(source)], text=True, capture_output=True)

    def test_each_header_is_self_contained_in_c17_and_cpp17(self):
        for language in ('c', 'c++'):
            for header in ('time_types.h', 'time.h', 'time_authority.h', 'time_calendar.h', 'thread.h', 'delay.h'):
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

    def test_realtime_domains_are_not_interchangeable(self):
        for language in ('c', 'c++'):
            valid = self.compile(language,
                '#include <crux/time_authority.h>\n#include <crux/time_calendar.h>\n'
                'crux_status_t probe(void) { crux_realtime_t r = {946684800, 0};\n'
                'crux_time_utc_t calendar; return crux_time_to_utc(r, &calendar); }\n')
            self.assertEqual(valid.returncode, 0, valid.stdout + valid.stderr)
            for operation in ('crux_time_get_realtime(&m)',
                              'crux_time_set_realtime(&m, CRUX_TIME_SOURCE_MANUAL, false)',
                              'crux_time_to_utc(m, &calendar)'):
                with self.subTest(language=language, operation=operation):
                    result = self.compile(language,
                        '#include <crux/time_authority.h>\n#include <crux/time_calendar.h>\n'
                        'crux_status_t probe(void) { crux_monotonic_time_t m = {0};\n'
                        'crux_time_utc_t calendar = {0, 0, 0, 0, 0, 0, 0}; (void)calendar;\n'
                        f'return {operation}; }}\n')
                    self.assertNotEqual(result.returncode, 0, 'wrong realtime clock domain accepted')

    def test_delay_requires_duration_values_and_outputs(self):
        for language in ('c', 'c++'):
            valid = self.compile(language, '#include <crux/delay.h>\n'
                'crux_status_t probe(void) { return crux_delay_busy_wait(crux_duration_from_ns(1)); }\n')
            self.assertEqual(valid.returncode, 0, valid.stdout + valid.stderr)
            for operation in ('crux_delay_busy_wait(t)', 'crux_delay_get_max_duration(&t)'):
                result = self.compile(language, '#include <crux/delay.h>\n'
                    'crux_status_t probe(void) { crux_monotonic_time_t t = {0};\n'
                    f'return {operation}; }}\n')
                self.assertNotEqual(result.returncode, 0, 'wrong Delay clock domain accepted')


if __name__ == '__main__':
    unittest.main()
