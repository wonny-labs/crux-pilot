"""The Zephyr File root binding is a documented composition interface, reachable through a public target.

These checks run on the host: they compile the header alone and read the CMake definition. The Zephyr
builds that use the target (a sample and a native_sim test) are exercised by the Zephyr CI shards.
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
        self.assertTrue(all(path.startswith(str(ROOT)) or path.startswith('/') for path in included))
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

    def test_every_declaration_is_documented_for_ownership_and_context(self):
        text = PUBLIC_HEADER.read_text()
        for word in ('composition', 'mounted', 'once', 'concurrent', 'never mount'):
            self.assertIn(word, text.lower(), word)


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
        for forbidden in ('zephyr_interface', '/src', 'PUBLIC', 'target_link_options'):
            self.assertNotIn(forbidden, text)


if __name__ == '__main__':
    unittest.main()
