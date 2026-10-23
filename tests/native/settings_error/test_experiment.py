#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Reproducible candidate-contract checks against original or patched source copies."""
import argparse
import errno
import hashlib
import json
import os
from pathlib import Path
import shutil
import tempfile
import unittest
from support import prepare, run, nvs_headers

HERE = Path(__file__).resolve().parent
STORE_CASES = {
    # expected: status, traversed sources, commit calls, actual bytes copied, lock depth
    'subtree_scan_error': '-5 1 0 0 0',
    'direct_scan_error': '-5 1 0 0 0',
    'earlier_error_two': '-5 1 0 0 0',
    'later_error': '-5 2 0 8 0',
    'short_read': '-5 1 0 2 0',
    'read_error': '-5 1 0 0 0',
    'small_buffer': '8 1 0 2 0',
    'zero_buffer': '8 1 0 0 0',
    'valid': '8 1 0 8 0',
    'valid_two': '5 2 0 13 0',
    'positive_stop': '7 1 0 0 0',
    'negative_stop': '-28 1 0 0 0',
    'getter_error': '-5 1 0 0 0',
    'valid_subtree': '0 1 1 0 0',
    'commit_error': f'-{errno.EAGAIN} 1 1 0 0',
}

# Native errno values differ across Host operating systems; expected semantics do not.
EIO, BAD = -errno.EIO, -errno.EBADMSG
# status, reads, deletes, writes, handlers, last_name_id, loaded(cache-on), received, lock depth
NVS_CASES = {
 'hard_name': (EIO,1,0,0,0,32769,0,0,0),
 'hard_value': (EIO,2,0,0,0,32769,0,0,0),
 'both_missing': (0,2,0,1,0,32768,1,0,0),
 'orphan_name': (0,2,2,1,0,32768,1,0,0),
 'delete_first': (EIO,2,1,0,0,32769,0,0,0),
 'delete_second': (EIO,2,2,0,0,32769,0,0,0),
 'count_error': (EIO,2,0,1,0,32769,0,0,0),
 'repair_zero': (0,2,0,1,0,32768,1,0,0),
 'valid_load': (0,3,0,0,1,32769,1,8,0),
 'value_read_error': (EIO,3,0,0,1,32769,0,0,0),
 'long_name': (BAD,1,0,0,0,32769,0,0,0),
 'boundary_name': (BAD,1,0,0,0,32769,0,0,0),
 'maximum_name': (0,3,0,0,1,32769,1,8,0),
 'embedded_name': (BAD,1,0,0,0,32769,0,0,0),
 'trailing_nul': (0,3,0,0,1,32769,1,8,0),
 'empty_name': (BAD,1,0,0,0,32769,0,0,0),
 'save_error': (EIO,1,0,0,0,32769,0,0,0),
 'save_long_name': (BAD,1,0,0,0,32769,0,0,0),
 'save_embedded': (BAD,1,0,0,0,32769,0,0,0),
 'save_valid': (0,1,0,1,0,32769,0,0,0),
 'save_zero': (0,1,0,1,0,32769,0,0,0),
 'save_value_error': (EIO,1,0,1,0,32769,0,0,0),
 'cached_save_error': (EIO,1,0,0,0,32769,0,0,0),
 'cached_long_name': (BAD,1,0,0,0,32769,0,0,0),
 'save_cache_complete': (0,0,0,3,0,32770,1,0,0),
 'save_cache_overflow': (0,1,0,1,0,32769,1,0,0),
 'cached_valid': (0,1,0,1,0,32769,0,0,0),
 'cached_missing': (0,2,0,2,0,32769,0,0,0),
 'cached_embedded': (BAD,1,0,0,0,32769,0,0,0),
 'init_valid': (0,1,0,0,0,32769,0,0,0),
 'init_missing': (0,1,0,0,0,32768,0,0,0),
 'init_read_error': (EIO,1,0,0,0,32769,0,0,0),
 'init_short': (BAD,1,0,0,0,32769,0,0,0),
 'init_long': (BAD,1,0,0,0,32769,0,0,0),
 'init_low': (BAD,1,0,0,0,32769,0,0,0),
 'init_high': (BAD,1,0,0,0,32769,0,0,0),
 'init_mount_error': (EIO,0,0,0,0,32769,0,0,0),
 'init_no_device': (-errno.ENODEV,0,0,0,0,32769,0,0,0),
 'integrated_valid': (8,3,0,0,1,32769,0,8,0),
 'integrated_small': (8,3,0,0,1,32769,0,2,0),
 'integrated_short': (EIO,3,0,0,1,32769,0,2,0),
 'integrated_hard_name': (EIO,1,0,0,0,32769,0,0,0),
 'integrated_count_error': (EIO,2,0,1,0,32769,0,0,0),
}

class Experiment(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix='crux-settings-copy-')
        cls.work = Path(cls.temporary.name)
        cls.observations = []
        cls.hashes = prepare(ARGS.zephyr, cls.work, candidate=ARGS.variant == 'candidate')
        cc = os.environ.get('CC') or shutil.which('cc')
        if not cc:
            raise RuntimeError('C compiler required; no result claimed')
        nvs_headers(cls.work)
        cls.command = [cc, '-std=c17', '-Wall', '-Wextra', '-Wpedantic', '-Werror',
                       '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-g',
                       '-I', str(cls.work), '-I', str(cls.work / 'subsys/settings/include')]
        if ARGS.suite in ('store', 'all'):
            compiled = run([*cls.command, str(HERE / 'store.c'), '-o', str(cls.work / 'store')])
            if compiled.returncode:
                raise RuntimeError(compiled.stdout + compiled.stderr)

        if ARGS.suite in ('nvs', 'all'):
            for cache in (0, 1):
                compiled = run([*cls.command, '-DCONFIG_SETTINGS_NVS_NAME_CACHE='+str(cache),
                                '-DCONFIG_SETTINGS_NVS_NAME_CACHE_SIZE=2', str(HERE / 'nvs.c'),
                                '-o', str(cls.work / ('nvs'+str(cache)))])
                if compiled.returncode:
                    raise RuntimeError(compiled.stdout + compiled.stderr)

    @classmethod
    def tearDownClass(cls):
        unchanged = all(hashlib.sha256((ARGS.zephyr / path).read_bytes()).hexdigest() == digest
                        for path, digest in cls.hashes.items())
        if ARGS.report:
            ARGS.report.parent.mkdir(parents=True, exist_ok=True)
            ARGS.report.write_text(json.dumps({
                'variant': ARGS.variant, 'suite': ARGS.suite,
                'source_sha256': cls.hashes, 'pinned_inputs_unchanged': unchanged,
                'compiler': run([cls.command[0], '--version']).stdout.splitlines()[0],
                'flags': cls.command[1:9],
                'harness_sha256': {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                                   for p in HERE.iterdir() if p.suffix in ('.py', '.c')},
                'patch_sha256': {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                                 for p in HERE.glob('*.patch')},
                'cases': cls.observations,
            }, indent=2) + '\n')
        cls.temporary.cleanup()
        if not unchanged:
            raise AssertionError('Pinned inputs changed during test')

    def check_case(self, binary, case, expected):
        result = run([str(self.work / binary), case], cwd=self.work)
        self.observations.append({'binary': binary, 'case': case, 'expected': expected,
                                  'actual': result.stdout.strip(), 'returncode': result.returncode,
                                  'sanitizer_error': bool(result.stderr),
                                  'passed': result.returncode == 0 and result.stdout.strip() == expected and not result.stderr})
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(result.stdout.strip(), expected)
        self.assertEqual(result.stderr, '')

def add_cases():
    for case, expected in (STORE_CASES.items() if ARGS.suite in ('store', 'all') else []):
        def test(self, case=case, expected=expected):
            self.check_case('store', case, expected)
        setattr(Experiment, 'test_store_' + case, test)

    for cache in (0, 1):
        for case, values in (NVS_CASES.items() if ARGS.suite in ('nvs', 'all') else []):
            if not cache and case.startswith(('cached_', 'save_cache_')):
                continue
            expected_values = list(values)
            if not cache:
                expected_values[6] = -1
            expected = ' '.join(str(value) for value in expected_values)
            def test(self, cache=cache, case=case, expected=expected):
                self.check_case('nvs'+str(cache), case, expected)
            setattr(Experiment, 'test_nvs'+str(cache)+'_'+case, test)

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--zephyr', type=Path, required=True)
    parser.add_argument('--variant', choices=('original', 'candidate'), default='candidate')
    parser.add_argument('--suite', choices=('store', 'nvs', 'all'), default='all')
    parser.add_argument('--report', type=Path)
    ARGS = parser.parse_args()
    add_cases()
    unittest.main(argv=[__file__], verbosity=2)
