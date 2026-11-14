"""The Zephyr File root binding is a documented composition interface, reachable through a public target.

These checks run on the host: they compile the header alone and read the CMake definition. The Zephyr
builds that use the target are exercised by the Zephyr CI shards.
"""
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
FILE_INCLUDE = ROOT / 'components/file/include'
PORT = ROOT / 'components/file/ports/zephyr'
PUBLIC_INCLUDE = PORT / 'include'
PUBLIC_HEADER = PUBLIC_INCLUDE / 'crux/file_zephyr.h'
CC = os.environ.get('CC', 'cc')
FLAGS = ['-std=c17', '-Wall', '-Wextra', '-Wpedantic', '-Werror']
USES = """
crux_file_result_t (*bind_default)(const char *) = crux_file_zephyr_bind;
crux_file_result_t (*bind_root)(crux_file_root_t, const char *) = crux_file_zephyr_bind_root;
crux_file_result_t (*last_error)(void) = crux_file_zephyr_last_cleanup_error;
int main(void) { return bind_default && bind_root && last_error ? 0 : 1; }
"""


def compile_source(source, includes, extra=()):
    with tempfile.TemporaryDirectory() as tmp:
        path = Path(tmp) / 'tu.c'
        path.write_text(source)
        command = [CC, *FLAGS, *extra, *[arg for inc in includes for arg in ('-I', str(inc))], '-fsyntax-only', str(path)]
        return subprocess.run(command, capture_output=True, text=True)


class PublicHeader(unittest.TestCase):
    def test_header_compiles_with_only_the_file_contract_and_its_own_include_root(self):
        result = compile_source('#include <crux/file_zephyr.h>\n' + USES, [FILE_INCLUDE, PUBLIC_INCLUDE])
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_header_includes_no_native_header(self):
        result = compile_source('#include <crux/file_zephyr.h>\n' + USES, [FILE_INCLUDE, PUBLIC_INCLUDE], ['-H'])
        self.assertEqual(result.returncode, 0, result.stderr)
        included = [line.lstrip('. ').strip() for line in result.stderr.splitlines() if line.startswith('.')]
        crux = [path for path in included if '/crux/' in path]
        self.assertEqual(sorted(Path(p).name for p in crux), ['file.h', 'file_zephyr.h'])
        # Everything else the header pulls in is outside this repository: the C library, never a native header.
        outside = [path for path in included if not path.startswith(str(ROOT))]
        for path in outside:
            self.assertNotRegex(path.lower(), r'zephyr|nxp|cmsis|mcux|/boards?/')

    def test_the_private_header_keeps_working_without_the_new_include_root(self):
        result = compile_source('#include "file_native.h"\n' + USES, [FILE_INCLUDE, PORT])
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_the_private_header_declares_nothing_a_second_time(self):
        text = (PORT / 'file_native.h').read_text()
        self.assertNotRegex(text, r'crux_file_zephyr_(bind|bind_root|last_cleanup_error)\s*\(')
        self.assertRegex(text, r'#include\s+"include/crux/file_zephyr\.h"')

    def test_the_public_path_is_unique_across_components(self):
        found = sorted(p.relative_to(ROOT).as_posix() for p in (ROOT / 'components').rglob('file_zephyr.h'))
        self.assertEqual(found, ['components/file/ports/zephyr/include/crux/file_zephyr.h'])

    def test_lines_fit_the_column_limit_of_the_format_check(self):
        for path in (PUBLIC_HEADER, PORT / 'file_native.h'):
            for number, line in enumerate(path.read_text().splitlines(), 1):
                self.assertLessEqual(len(line), 100, '%s:%d' % (path.name, number))

    def test_the_lifetime_rule_matches_the_file_contract_and_the_provider(self):
        text = ' '.join(PUBLIC_HEADER.read_text().replace('\n *', ' ').split())
        self.assertIn('Do not unmount or remount while a File handle is open or a File call is in progress', text)
        self.assertIn('CRUX_FILE_ERR_NOT_READY', text)
        self.assertNotIn('must stay mounted while File calls can be made', text)

    def test_the_header_is_usable_from_a_c_plus_plus_composition(self):
        text = PUBLIC_HEADER.read_text()
        self.assertIn('extern "C" {', text)
        self.assertRegex(text, r'#ifdef __cplusplus\s*\n\}\s*\n#endif')

    def test_every_declaration_is_documented_for_ownership_and_context(self):
        text = PUBLIC_HEADER.read_text()
        for word in ('composition', 'mounted', 'once', 'concurrent', 'never mount'):
            self.assertIn(word, text.lower(), word)


SAMPLES_ON_THE_PUBLIC_TARGET = ('storage_sd_interrupt', 'storage_sd_replace', 'storage_sd_probe',
                                'storage_emmc', 'storage_reliance_sd')


def builds_compiling(*sources):
    return [p for p in ROOT.rglob('CMakeLists.txt')
            if p.relative_to(ROOT).parts[0] in ('samples', 'tests')
            and any(source in p.read_text() for source in sources)]


class Consumers(unittest.TestCase):
    def test_every_build_that_compiles_the_sd_composition_links_the_target(self):
        # sd_storage.c includes <crux/file_zephyr.h>, so every build that compiles it needs the include root.
        builds = builds_compiling('sd_storage.c')
        self.assertGreaterEqual(len(builds), 9)
        for path in builds:
            with self.subTest(build=path.relative_to(ROOT).as_posix()):
                self.assertIn('crux::file_zephyr', path.read_text())

    def test_every_build_that_compiles_the_emmc_composition_links_the_target(self):
        # emmc_storage.c includes <crux/file_zephyr.h> as well: the two samples and the two native_sim
        # fixtures that compile it need the include root.
        builds = builds_compiling('emmc_storage.c')
        self.assertGreaterEqual(len(builds), 4)
        for path in builds:
            with self.subTest(build=path.relative_to(ROOT).as_posix()):
                self.assertIn('crux::file_zephyr', path.read_text())

    def test_the_samples_that_only_need_the_binding_header_use_the_public_target(self):
        for name in SAMPLES_ON_THE_PUBLIC_TARGET:
            directory = ROOT / 'samples' / name
            with self.subTest(sample=name):
                text = (directory / 'CMakeLists.txt').read_text()
                self.assertRegex(text, r'target_link_libraries\(app PRIVATE[^)]*crux::file_zephyr')
                self.assertNotIn('components/file/ports/zephyr', text)
                for source in list(directory.glob('*.c')) + list(directory.glob('*.h')):
                    self.assertNotIn('file_native.h', source.read_text(), source.name)

    def test_the_emmc_composition_includes_the_public_header_in_format_order(self):
        lines = (ROOT / 'samples/storage_emmc/emmc_storage.c').read_text().splitlines()
        includes = [line for line in lines if line.startswith('#include')]
        self.assertIn('#include <crux/file_zephyr.h>', includes)
        self.assertNotIn('#include "file_native.h"', includes)
        # clang-format SortIncludes: CaseSensitive sorts '<crux/file.h>' before '<crux/file_zephyr.h>'
        # inside the block of angle-bracket includes.
        angle = [line for line in includes if line.startswith('#include <')]
        self.assertEqual(angle, sorted(angle))


class Documents(unittest.TestCase):
    def test_the_architecture_and_the_native_policy_name_the_composition_target(self):
        for name in ('architecture.md', 'native-interface-policy.md'):
            with self.subTest(document=name):
                text = ' '.join((ROOT / 'docs' / name).read_text().split())
                self.assertIn('`crux::file_zephyr`', text)
                self.assertIn('(file-composition-interface.md)', text)
                self.assertIn('not a portable usage target', text)

    def test_the_native_policy_says_the_consumer_check_rejects_it(self):
        text = ' '.join((ROOT / 'docs/native-interface-policy.md').read_text().split())
        self.assertRegex(text, r'`crux::file_zephyr`[^.]*rejected by the native-free consumer check')


class Target(unittest.TestCase):
    cmake = (PORT / 'CMakeLists.txt').read_text()

    def test_each_provider_branch_publishes_the_composition_target(self):
        calls = len(re.findall(r'^\s*crux_file_zephyr_composition\(\)\s*$', self.cmake, re.M))
        self.assertEqual(calls, self.cmake.count('add_library(crux_file INTERFACE)'))
        self.assertGreaterEqual(calls, 3)

    def test_the_target_exposes_only_the_public_include_root_and_the_file_contract(self):
        body = re.search(r'function\(crux_file_zephyr_composition\)(.*?)endfunction\(\)', self.cmake, re.S)
        self.assertIsNotNone(body)
        text = body.group(1)
        self.assertIn('add_library(crux::file_zephyr ALIAS', text)
        self.assertIn('ports/zephyr/include', text)
        self.assertIn('INTERFACE crux_file', text)
        for forbidden in ('zephyr_interface', '/src', 'PUBLIC', 'target_link_options', 'target_compile'):
            self.assertNotIn(forbidden, text)
        self.assertEqual(len(re.findall(r'^\s*target_', text, re.M)), 2)    # the include root and crux_file, nothing else


if __name__ == '__main__':
    unittest.main()
