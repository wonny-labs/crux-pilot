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
                ('thread', {'thread', 'time'}, 'crux_thread_create'),
                ('sync', {'sync'}, 'crux_mutex_create'),
                ('serial', {'serial'}, 'crux_serial_open'),
                ('logging', {'logging'}, 'crux_log_write'),
                ('system', {'system'}, 'crux_system_status_get'),
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
                if 'serial' not in selected:
                    self.assertNotRegex(symbols, r'\b(?:crux_serial_|serial_port_)')
                if 'logging' not in selected:
                    self.assertNotRegex(symbols, r'\b(?:crux_log_|logging_port_)')
                if 'system' not in selected:
                    self.assertNotRegex(symbols, r'\b(?:crux_system_|crux_watchdog_|system_port_)')
                tests = json.loads(self.run_command(['ctest', '--test-dir', str(build),
                                                    '--show-only=json-v1']))
                self.assertEqual([t['name'] for t in tests['tests']],
                                 ['Parent.Before', 'Parent.After', 'Parent.Cpp', 'Parent.Application'])
                self.assertIn('BUILD_TESTING:BOOL=ON', (build / 'CMakeCache.txt').read_text())

    def test_independent_serial_consumer_preserves_parent_testing(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            shutil.copytree(REPO / 'tests/serial_consumer', root / 'serial')
            (root / 'CMakeLists.txt').write_text(
                'cmake_minimum_required(VERSION 3.28)\nproject(serial_parent LANGUAGES C CXX)\n'
                'include(CTest)\n'
                'add_test(NAME Parent.Before COMMAND "${CMAKE_COMMAND}" -E true)\n'
                f'set(CRUX_SOURCE_DIR "{REPO}" CACHE PATH "External dependency")\n'
                'add_subdirectory(serial)\n'
                'if(NOT BUILD_TESTING OR CRUX_BUILD_TESTS OR TARGET crux::thread OR TARGET crux::time)\n'
                'message(FATAL_ERROR "Serial inclusion changed parent policy or component selection")\nendif()\n'
                'add_test(NAME Parent.After COMMAND "${CMAKE_COMMAND}" -E true)\n')
            build = root / 'out'
            self.run_command([CMAKE, '-S', str(root), '-B', str(build), '-G', 'Ninja',
                              '-DFETCHCONTENT_FULLY_DISCONNECTED=ON'])
            self.run_command([CMAKE, '--build', str(build), '--parallel', '2'])
            output = self.run_command(['ctest', '--test-dir', str(build), '--output-on-failure'])
            for name in ('Parent.Before', 'Parent.After', 'SerialConsumer.c', 'SerialConsumer.cpp',
                         'SerialConsumer.controller', 'SerialConsumer.controller_stale',
                         'SerialConsumer.Policy'):
                self.assertIn(name, output)

    def test_independent_logging_consumer_preserves_parent_testing(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            shutil.copytree(REPO / 'tests/logging_consumer', root / 'logging')
            (root / 'CMakeLists.txt').write_text(
                'cmake_minimum_required(VERSION 3.28)\nproject(logging_parent LANGUAGES C CXX)\n'
                'set(CMAKE_C_STANDARD 99)\nset(CMAKE_CXX_STANDARD 20)\ninclude(CTest)\n'
                'add_test(NAME Parent.Before COMMAND "${CMAKE_COMMAND}" -E true)\n'
                f'set(CRUX_SOURCE_DIR "{REPO}" CACHE PATH "External dependency")\n'
                'add_subdirectory(logging)\n'
                'if(NOT BUILD_TESTING OR CRUX_BUILD_TESTS OR TARGET crux::serial OR TARGET crux::time '
                'OR TARGET crux::thread OR TARGET crux::monotonic '
                'OR NOT CMAKE_C_STANDARD EQUAL 99 OR NOT CMAKE_CXX_STANDARD EQUAL 20)\n'
                'message(FATAL_ERROR "Logging changed parent policy or component selection")\nendif()\n'
                'add_test(NAME Parent.After COMMAND "${CMAKE_COMMAND}" -E true)\n')
            build = root / 'out'
            self.run_command([CMAKE, '-S', str(root), '-B', str(build), '-G', 'Ninja',
                              '-DFETCHCONTENT_FULLY_DISCONNECTED=ON'])
            self.run_command([CMAKE, '--build', str(build), '--parallel', '2'])
            output = self.run_command(['ctest', '--test-dir', str(build), '--output-on-failure'])
            for name in ('Parent.Before', 'Parent.After', 'LoggingConsumer.ActualHost',
                         'LoggingConsumer.Policy'):
                self.assertIn(name, output)

    def test_independent_system_consumer_preserves_parent_testing(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            shutil.copytree(REPO / 'tests/system_consumer', root / 'system')
            (root / 'CMakeLists.txt').write_text(
                'cmake_minimum_required(VERSION 3.28)\nproject(system_parent LANGUAGES C CXX)\n'
                'set(CMAKE_C_STANDARD 99)\nset(CMAKE_CXX_STANDARD 20)\ninclude(CTest)\n'
                'add_test(NAME Parent.Before COMMAND "${CMAKE_COMMAND}" -E true)\n'
                f'set(CRUX_SOURCE_DIR "{REPO}" CACHE PATH "External dependency")\n'
                'add_subdirectory(system)\n'
                'if(NOT BUILD_TESTING OR CRUX_BUILD_TESTS OR TARGET crux::serial OR TARGET crux::time '
                'OR TARGET crux::thread OR TARGET crux::monotonic '
                'OR NOT CMAKE_C_STANDARD EQUAL 99 OR NOT CMAKE_CXX_STANDARD EQUAL 20)\n'
                'message(FATAL_ERROR "System changed parent policy or component selection")\nendif()\n'
                'add_test(NAME Parent.After COMMAND "${CMAKE_COMMAND}" -E true)\n')
            build = root / 'out'
            self.run_command([CMAKE, '-S', str(root), '-B', str(build), '-G', 'Ninja',
                              '-DFETCHCONTENT_FULLY_DISCONNECTED=ON'])
            self.run_command([CMAKE, '--build', str(build), '--parallel', '2'])
            output = self.run_command(['ctest', '--test-dir', str(build), '--output-on-failure'])
            for name in ('Parent.Before', 'Parent.After', 'SystemConsumer.ActualHost',
                         'SystemConsumer.Policy'):
                self.assertIn(name, output)

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

    def test_thread_requires_explicit_time_selection(self):
        with tempfile.TemporaryDirectory() as temporary:
            result, _ = self.configure(Path(temporary), 'thread', '-DCRUX_BUILD_TIME=OFF')
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('CRUX_BUILD_TIME', result.stdout + result.stderr)

    def test_conflicting_time_providers(self):
        with tempfile.TemporaryDirectory() as temporary:
            result, _ = self.configure(Path(temporary), 'time', '-DCONFLICT_PROVIDER=ON')
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('CRUX_TIME_BACKEND', result.stdout + result.stderr)

    def test_conflicting_serial_providers(self):
        with tempfile.TemporaryDirectory() as temporary:
            result, _ = self.configure(Path(temporary), 'serial', '-DCONFLICT_PROVIDER=ON')
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('CRUX_SERIAL_PROVIDER', result.stdout + result.stderr)

    def test_conflicting_logging_providers(self):
        with tempfile.TemporaryDirectory() as temporary:
            result, _ = self.configure(Path(temporary), 'logging', '-DCONFLICT_PROVIDER=ON')
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('CRUX_LOGGING_PROVIDER', result.stdout + result.stderr)

    def test_conflicting_system_providers(self):
        with tempfile.TemporaryDirectory() as temporary:
            result, _ = self.configure(Path(temporary), 'system', '-DCONFLICT_PROVIDER=ON')
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('CRUX_SYSTEM_PROVIDER', result.stdout + result.stderr)

    def test_missing_native_composition_fails_at_link(self):
        with tempfile.TemporaryDirectory() as temporary:
            result, build = self.configure(Path(temporary), 'messaging', '-DOMIT_PROVIDER=ON')
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([CMAKE, '--build', str(build)], text=True, capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('undefined reference', result.stdout + result.stderr)
