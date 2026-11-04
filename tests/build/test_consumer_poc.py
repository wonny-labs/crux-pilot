"""Keep the out-of-tree consumer working from a real external copy, at each software level."""
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

REPO = Path(__file__).resolve().parents[2]
CMAKE = shutil.which('cmake')
RUNNER = REPO / 'tests/consumer_poc/run_poc.sh'


@unittest.skipUnless(CMAKE, 'CMake is required')
class ConsumerHostLevelsTest(unittest.TestCase):
    def build(self, root, *options):
        source = root / 'consumer'
        shutil.copytree(REPO / 'tests/consumer_poc', source)
        self.assertNotIn(REPO, source.parents)
        build = root / 'out'
        self.run_command([CMAKE, '-S', str(source), '-B', str(build), '-G', 'Ninja',
                          f'-DCRUX_SOURCE_DIR={REPO}', '-DFETCHCONTENT_FULLY_DISCONNECTED=ON',
                          '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON', *options])
        self.run_command([CMAKE, '--build', str(build), '--parallel', '2'])
        return build

    def run_command(self, command):
        result = subprocess.run(command, text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return result.stdout

    def test_unit_and_host_integration_pass_from_an_external_copy(self):
        with tempfile.TemporaryDirectory() as temporary:
            build = self.build(Path(temporary))
            self.run_command(['ctest', '--test-dir', str(build), '--output-on-failure'])
            tests = json.loads(self.run_command(['ctest', '--test-dir', str(build),
                                                 '--show-only=json-v1']))
            self.assertEqual([test['name'] for test in tests['tests']],
                             ['Consumer.Unit', 'Consumer.HostIntegration'])
            self.assertIn('BUILD_TESTING:BOOL=ON', (build / 'CMakeCache.txt').read_text())

    def test_only_the_selected_components_are_built(self):
        with tempfile.TemporaryDirectory() as temporary:
            build = self.build(Path(temporary))
            for entry in json.loads((build / 'compile_commands.json').read_text()):
                file = entry['file'].replace('\\', '/')
                match = re.search(r'/components/([^/]+)/', file)
                if match:
                    self.assertIn(match[1], {'time', 'thread', 'interrupt'}, file)
                self.assertNotIn('googletest', file)
                self.assertNotIn('/samples/', file)

    def test_unit_level_links_no_crux_target(self):
        with tempfile.TemporaryDirectory() as temporary:
            build = self.build(Path(temporary))
            commands = json.loads((build / 'compile_commands.json').read_text())
            names = {Path(entry['file']).name: entry['command'] for entry in commands}
            for name in ('periodic.c', 'test_periodic.c'):
                self.assertNotIn('/components/', names[name])
            symbols = self.run_command(['readelf', '--symbols', '--wide', str(build / 'consumer_unit')])
            self.assertNotIn('crux_', symbols)
            self.assertIn('crux_monotonic_now',
                          self.run_command(['readelf', '--symbols', '--wide',
                                            str(build / 'consumer_integration')]))

    def test_portable_application_sees_no_native_requirement(self):
        with tempfile.TemporaryDirectory() as temporary:
            build = self.build(Path(temporary))
            commands = json.loads((build / 'compile_commands.json').read_text())
            command = next(entry['command'] for entry in commands
                           if Path(entry['file']).name == 'app_run.c')
            self.assertNotRegex(command, r'/components/[^/]+/(?:src|ports)/|__ZEPHYR__|-pthread')

    def test_controlled_defect_fails_on_each_host_level_own_assertion(self):
        with tempfile.TemporaryDirectory() as temporary:
            build = self.build(Path(temporary), '-DCONSUMER_DEFECT_REBASE=ON')
            for name, diagnostic in (
                    ('Consumer.Unit', 'FAILED overrun skips whole periods without rebasing the phase'),
                    ('Consumer.HostIntegration', 'CONSUMER_INTEGRATION result=0 cycles=6 skipped=0')):
                with self.subTest(test=name):
                    result = subprocess.run(['ctest', '--test-dir', str(build), '--output-on-failure',
                                             '-R', f'^{re.escape(name)}$'], text=True, capture_output=True)
                    self.assertNotEqual(result.returncode, 0, result.stdout)
                    self.assertIn(diagnostic, result.stdout)


@unittest.skipUnless(os.environ.get('CRUX_CONSUMER_POC_ZEPHYR_TESTS') == '1',
                     'requires the pinned Zephyr environment and pytest; see tests/README.md')
class ConsumerZephyrLevelsTest(unittest.TestCase):
    def test_all_software_levels_and_the_defect_control(self):
        with tempfile.TemporaryDirectory() as temporary:
            result = subprocess.run([str(RUNNER), str(Path(temporary) / 'run')], text=True,
                                    capture_output=True, timeout=3600)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            for step in ('host-unit-integration', 'twister-native-sim', 'image-normal', 'functional-normal',
                         'image-defect', 'functional-defect', 'defect-found-by-oracle'):
                self.assertRegex(result.stdout, rf'(?m)^ok\s+{step}\s')


if __name__ == '__main__':
    unittest.main(verbosity=2)
