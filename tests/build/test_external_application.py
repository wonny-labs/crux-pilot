"""Exercise the supported source-tree entry point from a real external project."""
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

REPO = Path(__file__).resolve().parents[2]
CMAKE = shutil.which('cmake')


@unittest.skipUnless(CMAKE, 'CMake is required')
class ExternalApplicationTest(unittest.TestCase):
    def configure(self, root, profile, *options):
        source = root / 'application'
        shutil.copytree(REPO / 'tests/external_application', source)
        self.assertNotIn(REPO, source.parents)
        build = root / 'out'
        result = subprocess.run([CMAKE, '-S', str(source), '-B', str(build), '-G', 'Ninja',
                                 f'-DCRUX_SOURCE_DIR={REPO}', f'-DPROFILE={profile}',
                                 '-DFETCHCONTENT_FULLY_DISCONNECTED=ON', *options],
                                text=True, capture_output=True)
        return result, build

    def run_command(self, command):
        result = subprocess.run(command, text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return result.stdout

    def test_external_configurations_and_parent_isolation(self):
        for profile, selected, symbol in (
                ('time', {'time'}, 'crux_time_get'),
                ('thread', {'thread'}, 'crux_thread_create'),
                ('sync', {'sync'}, 'crux_mutex_create'),
                ('messaging', {'queue', 'monotonic'}, 'crux_queue_create'),
                ('parent', set(), None)):
            with self.subTest(profile=profile), tempfile.TemporaryDirectory() as temporary:
                result, build = self.configure(Path(temporary), profile)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.run_command([CMAKE, '--build', str(build), '--parallel', '2'])
                self.run_command(['ctest', '--test-dir', str(build), '--output-on-failure'])
                commands = json.loads((build / 'compile_commands.json').read_text())
                for entry in commands:
                    file = entry['file'].replace('\\', '/')
                    match = re.search(r'/components/([^/]+)/', file)
                    if match:
                        self.assertIn(match[1], selected, file)
                    self.assertNotIn('googletest', file)
                    self.assertNotIn('/samples/', file)
                    if Path(file).name in ('consumer.c', 'consumer.cpp'):
                        self.assertNotRegex(entry['command'],
                                            r'/components/[^/]+/(?:src|ports)/|__ZEPHYR__|-pthread')
                symbols = self.run_command(['readelf', '--symbols', '--wide', str(build / 'external_app')])
                if symbol:
                    self.assertRegex(symbols, r'\b' + symbol + r'\b')
                if 'thread' not in selected:
                    self.assertNotRegex(symbols, r'\b(?:crux_thread_|thread_native_)')
                if 'sync' not in selected:
                    self.assertNotRegex(symbols, r'\b(?:crux_mutex_|crux_semaphore_|sync_native_)')
                tests = json.loads(self.run_command(['ctest', '--test-dir', str(build),
                                                    '--show-only=json-v1']))
                self.assertEqual([t['name'] for t in tests['tests']],
                                 ['Parent.Before', 'Parent.After', 'Parent.Cpp', 'Parent.Application'])
                self.assertIn('BUILD_TESTING:BOOL=ON', (build / 'CMakeCache.txt').read_text())

    def test_c_only_parent_keeps_testing_policy_and_languages(self):
        for testing in ('ON', 'OFF'):
            with self.subTest(testing=testing), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                (root / 'main.c').write_text('#include <crux/status.h>\nint main(void) { return CRUX_OK; }\n')
                (root / 'CMakeLists.txt').write_text(
                    'cmake_minimum_required(VERSION 3.28)\nproject(parent_c LANGUAGES C)\n'
                    f'set(BUILD_TESTING {testing} CACHE BOOL "Parent policy")\n'
                    'include(CTest)\n'
                    f'add_subdirectory("{REPO}" crux)\n'
                    'get_property(languages GLOBAL PROPERTY ENABLED_LANGUAGES)\n'
                    'if(NOT languages STREQUAL "C")\n'
                    'message(FATAL_ERROR "Unexpected language enabled: ${languages}")\nendif()\n'
                    f'if(NOT BUILD_TESTING STREQUAL "{testing}")\n'
                    'message(FATAL_ERROR "Parent testing policy changed")\nendif()\n'
                    'add_executable(parent_c main.c)\n'
                    'target_link_libraries(parent_c PRIVATE crux::base)\n')
                build = root / 'out'
                self.run_command([CMAKE, '-S', str(root), '-B', str(build), '-G', 'Ninja',
                                  '-DFETCHCONTENT_FULLY_DISCONNECTED=ON'])
                self.run_command([CMAKE, '--build', str(build)])
                self.run_command([str(build / 'parent_c')])

    def test_missing_or_unsupported_time_provider(self):
        for provider in ('', 'zephyr_rtc', 'memory;zephyr_rtc'):
            with self.subTest(provider=provider), tempfile.TemporaryDirectory() as temporary:
                result, _ = self.configure(Path(temporary), 'time', f'-DCRUX_TIME_PROVIDER={provider}')
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('CRUX_TIME_PROVIDER', result.stdout + result.stderr)

    def test_conflicting_time_providers(self):
        with tempfile.TemporaryDirectory() as temporary:
            result, _ = self.configure(Path(temporary), 'time', '-DCONFLICT_PROVIDER=ON')
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('CRUX_TIME_BACKEND', result.stdout + result.stderr)

    def test_missing_native_composition_fails_at_link(self):
        with tempfile.TemporaryDirectory() as temporary:
            result, build = self.configure(Path(temporary), 'messaging', '-DOMIT_PROVIDER=ON')
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([CMAKE, '--build', str(build)], text=True, capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('undefined reference', result.stdout + result.stderr)
