"""The capture and freeze steps of the patched Zephyr copy of the bench image (design B2, tests T6, T8, T9 and the freeze half of T7/T10).

Host only. The fixtures use the real ``git``, ``tar`` and ``patch`` programs on a synthetic stock tree and synthetic patches (a
LIST of two, and a one-patch case): the wolfSSL patch and the ``tcp_init_isn`` patch of session C11 are not in the repository
and are not invented here. Where those programs are missing the tests are skipped, and a skip is not a pass.
"""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
import unittest.mock

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent / 'target'))
import patched_zephyr as subject
import zephyr_tree
from patched_zephyr_fixtures import Bench, MARKER_FILE, OMIT, available, patch_deletes, run

NEEDS_PROGRAMS = unittest.skipUnless(available(), 'git, tar and patch are needed: SKIPPED, not passed')


class Base(unittest.TestCase):
    count = 2
    independent = False
    delete = False

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.bench = Bench(self.temp.name, self.count, self.independent, delete=self.delete)

    def refuses(self, pattern, **override):
        with self.assertRaisesRegex(ValueError, pattern):
            self.bench.freeze(**override)

    def rewrite_block(self, change):
        block = json.loads(json.dumps(self.bench.block))
        change(block)
        self.bench.write_manifest(block)


@NEEDS_PROGRAMS
class Capture(Base):
    def test_capture_records_the_marker_text_and_the_tree_digest_of_the_copy(self):
        pre = json.loads(self.bench.pre.read_text())
        digest = zephyr_tree.tree_digest(self.bench.copy)
        self.assertEqual(pre['tree'], dict(sha256=digest['sha256'], file_count=digest['file_count'], symlink_count=digest['symlink_count']))
        self.assertEqual(pre['marker_text'], (self.bench.copy / MARKER_FILE).read_text())
        self.assertEqual((pre['schema'], pre['algorithm'], pre['marker_file']), (1, 'crux-zephyr-tree-v1', MARKER_FILE))
        self.assertEqual(pre['copy'], str(self.bench.copy))

    def test_capture_refuses_a_copy_that_fails_a_rule_of_the_digest(self):
        copy = self.bench.copy
        cases = {
            'a .git entry': lambda: (copy / '.git').mkdir(),
            'a symlink to the outside': lambda: os.symlink('../../../outside', copy / 'out-link'),
            'a fifo': lambda: os.mkfifo(copy / 'pipe'),
            'an absolute symlink': lambda: os.symlink('/etc/passwd', copy / 'abs-link'),
        }
        for label, make in cases.items():
            with self.subTest(label):
                make()
                with self.assertRaises(ValueError):
                    subject.capture(copy, self.bench.build / ('refused-%s.json' % len(label)), MARKER_FILE)
                for leftover in ('.git', 'out-link', 'pipe', 'abs-link'):
                    path = copy / leftover
                    if path.is_symlink() or path.is_file() or (path.exists() and not path.is_dir()):
                        path.unlink()
                    elif path.is_dir():
                        path.rmdir()

    def test_capture_refuses_a_copy_without_a_marker_or_with_a_malformed_one(self):
        marker = self.bench.copy / MARKER_FILE
        text = marker.read_text()
        marker.unlink()
        with self.assertRaisesRegex(ValueError, 'marker'):
            subject.capture(self.bench.copy, self.bench.build / 'x.json', MARKER_FILE)
        marker.write_text(text.rstrip('\n'))
        with self.assertRaisesRegex(ValueError, 'marker'):
            subject.capture(self.bench.copy, self.bench.build / 'x.json', MARKER_FILE)

    def test_capture_never_overwrites_an_earlier_capture(self):
        with self.assertRaises(FileExistsError):
            subject.capture(self.bench.copy, self.bench.pre, MARKER_FILE)

    def test_the_marker_path_must_be_a_plain_relative_name(self):
        for name in ('', '../x', '/abs', 'a/../b', '.git/x'):
            with self.subTest(name), self.assertRaises(ValueError):
                subject.capture(self.bench.copy, self.bench.build / 'x.json', name)


@NEEDS_PROGRAMS
class FreezeRecord(Base):
    """T6."""

    def test_the_frozen_folder_holds_the_record_the_patches_the_files_the_marker_and_the_listing(self):
        out = self.bench.build / 'frozen'
        record = self.bench.freeze(out=out)
        names = sorted(p.relative_to(out).as_posix() for p in out.rglob('*') if p.is_file())
        self.assertEqual(names, sorted(['patched-zephyr.json', 'marker', 'tree-files.tsv', 'patches/first-1.patch', 'patches/first-2.patch',
                                        'files/subsys/a.c', 'files/subsys/b.c', 'files/subsys/new.c']))
        self.assertEqual(json.loads((out / 'patched-zephyr.json').read_text()), record)
        self.assertEqual((out / 'marker').read_bytes(), (self.bench.copy / MARKER_FILE).read_bytes())
        self.assertEqual((out / 'files/subsys/a.c').read_bytes(), (self.bench.copy / 'subsys/a.c').read_bytes())
        for name in self.bench.names:
            self.assertEqual((out / 'patches' / name).read_bytes(), (self.bench.patches / name).read_bytes())

    def test_the_record_content(self):
        record = self.bench.freeze()
        self.assertEqual((record['schema'], record['algorithm'], record['pin']), (1, 'crux-zephyr-tree-v1', self.bench.pin))
        self.assertEqual(record['excluded'], dict(names=['.DS_Store'], directories=['__pycache__'], suffixes=['.pyc', '.pyo']))
        self.assertEqual(record['marker']['text'], (self.bench.copy / MARKER_FILE).read_text())
        self.assertEqual([p['name'] for p in record['patches']], self.bench.names)
        self.assertEqual([(f['path'], f['change'], f['patches']) for f in record['patched_files']],
                         [('subsys/a.c', 'modified', self.bench.names), ('subsys/b.c', 'modified', ['first-2.patch']),
                          ('subsys/new.c', 'added', self.bench.names)])
        by_path = {f['path']: f for f in record['patched_files']}
        self.assertIsNone(by_path['subsys/new.c']['stock_sha256'])                       # added: no stock file
        self.assertEqual(by_path['subsys/a.c']['stock_sha256'], hashlib.sha256(b'a1\na2\na3\na4\na5\n').hexdigest())
        self.assertEqual(by_path['subsys/a.c']['sha256'], hashlib.sha256((self.bench.copy / 'subsys/a.c').read_bytes()).hexdigest())
        tree = record['tree']
        self.assertEqual(tree['pre'], tree['post'])
        self.assertEqual(tree['post']['sha256'], self.bench.block['tree']['sha256'])
        self.assertEqual(tree['rederived']['sha256'], tree['post']['sha256'])
        self.assertNotEqual(tree['stock']['sha256'], tree['post']['sha256'])
        self.assertEqual(record['compile_commands'], dict(translation_units=3, sources_under_copy=2, sources_under_stock=0))
        canonical = json.dumps(self.bench.block, sort_keys=True, separators=(',', ':')).encode()
        self.assertEqual(record['manifest_block_sha256'], hashlib.sha256(canonical).hexdigest())

    def test_every_named_file_is_listed_with_its_size_and_hash(self):
        out = self.bench.build / 'frozen'
        record = self.bench.freeze(out=out)
        named = subject.named_files(out / 'patched-zephyr.json')
        self.assertEqual(sorted(named), sorted(p.relative_to(out).as_posix() for p in out.rglob('*')
                                               if p.is_file() and p.name != 'patched-zephyr.json'))
        for relative, spec in named.items():
            data = (out / relative).read_bytes()
            self.assertEqual((spec['size_bytes'], spec['sha256']), (len(data), hashlib.sha256(data).hexdigest()))
        self.assertEqual(record['tree_listing']['path'], 'tree-files.tsv')

    def test_the_listing_has_one_line_per_entry_of_the_copy(self):
        out = self.bench.build / 'frozen'
        self.bench.freeze(out=out)
        lines = (out / 'tree-files.tsv').read_text().splitlines()
        entries = zephyr_tree.tree_digest(self.bench.copy)['entries']
        self.assertEqual(lines, ['\t'.join((e['path'], e['kind'], e['flag'], e['sha256'])) for e in entries])
        self.assertIn(MARKER_FILE + '\tfile\tplain\t' + hashlib.sha256((self.bench.copy / MARKER_FILE).read_bytes()).hexdigest(), lines)

    def test_two_freezes_of_the_same_inputs_are_byte_identical_and_sorted(self):
        first, second = self.bench.build / 'one', self.bench.build / 'two'
        self.bench.freeze(out=first)
        self.bench.freeze(out=second)
        a = sorted(p.relative_to(first).as_posix() for p in first.rglob('*') if p.is_file())
        self.assertEqual(a, sorted(p.relative_to(second).as_posix() for p in second.rglob('*') if p.is_file()))
        for relative in a:
            self.assertEqual((first / relative).read_bytes(), (second / relative).read_bytes(), relative)
        text = (first / 'patched-zephyr.json').read_text()
        self.assertEqual(text, json.dumps(json.loads(text), indent=2, sort_keys=True) + '\n')

    def test_the_record_holds_no_absolute_path_of_the_build_host(self):
        self.bench.freeze()
        text = (self.bench.build / 'patched-zephyr' / 'patched-zephyr.json').read_text()
        self.assertNotIn(self.temp.name, text)

    def test_the_marker_is_inside_both_the_pre_and_the_post_digest(self):
        pre = json.loads(self.bench.pre.read_text())['tree']['sha256']
        marker = self.bench.copy / MARKER_FILE
        text = marker.read_text()
        marker.unlink()
        self.assertNotEqual(zephyr_tree.tree_digest(self.bench.copy)['sha256'], pre)
        marker.write_text(text)
        self.assertEqual(zephyr_tree.tree_digest(self.bench.copy)['sha256'], pre)
        record = self.bench.freeze()
        self.assertEqual((record['tree']['pre']['sha256'], record['tree']['post']['sha256']), (pre, pre))

    def test_a_file_changed_between_the_captures_is_refused(self):
        (self.bench.copy / 'subsys' / 'b.c').write_bytes(b'changed during the build\n')
        self.refuses('changed since the capture')

    def test_a_file_added_removed_or_a_marker_changed_during_the_build_is_refused(self):
        for label, change in dict(added=lambda: (self.bench.copy / 'extra.c').write_bytes(b'x'),
                                  removed=lambda: (self.bench.copy / 'include' / 'x.h').unlink(),
                                  marker=lambda: (self.bench.copy / MARKER_FILE).write_text('%s %s\n' % (self.bench.pin, '9' * 64))).items():
            with self.subTest(label):
                self.tearDown_temp()
                self.setUp()
                change()
                self.refuses('changed since the capture')

    def tearDown_temp(self):
        self.temp.cleanup()

    def test_pycache_written_by_the_build_does_not_change_the_post_digest(self):
        cache = self.bench.copy / 'scripts' / '__pycache__'
        cache.mkdir()
        (cache / 'tool.cpython-312.pyc').write_bytes(b'bytecode')
        (self.bench.copy / 'scripts' / 'old.pyc').write_bytes(b'bytecode')
        self.bench.freeze()

    def test_an_existing_output_folder_is_never_overwritten(self):
        out = self.bench.build / 'frozen'
        self.bench.freeze(out=out)
        with self.assertRaises(FileExistsError):
            self.bench.freeze(out=out)

    def test_a_capture_made_for_another_copy_path_is_refused(self):
        other = self.bench.root / 'ws' / 'other-copy'
        shutil.copytree(self.bench.copy, other, symlinks=True)
        with self.assertRaisesRegex(ValueError, 'another copy'):
            self.bench.freeze(copy=other)


@NEEDS_PROGRAMS
class FreezeRefusals(Base):
    """What each part of step 4 refuses (the design's table of check_image refusals has its twin in the check tests)."""

    def test_a_missing_patch_file_is_refused(self):
        (self.bench.patches / 'first-2.patch').unlink()
        self.refuses('missing patch first-2.patch')

    def test_a_patch_file_with_other_bytes_is_refused_as_changed(self):
        (self.bench.patches / 'first-2.patch').write_bytes((self.bench.patches / 'first-2.patch').read_bytes() + b'\n')
        self.refuses('changed patch first-2.patch')

    def test_a_copy_made_with_only_the_first_patch_is_missing_the_second(self):
        self.bench.rebuild_copy(names=['first-1.patch'])
        self.bench.recapture()
        self.refuses('marker differs')

    def test_a_copy_with_an_extra_patch_that_the_block_does_not_declare_is_refused(self):
        def one_patch_only(block):
            block['patches'] = block['patches'][:1]
            block['patched_files'] = [dict(f, patches=['first-1.patch']) for f in block['patched_files'] if 'first-1.patch' in f['patches']]
        self.rewrite_block(one_patch_only)
        self.refuses('marker differs')

    def test_a_marker_with_the_hashes_in_the_other_order_is_refused(self):
        self.bench.rebuild_copy(marker_names=list(reversed(self.bench.names)))
        self.bench.recapture()
        self.refuses('marker differs')

    def test_a_well_formed_marker_for_another_pin_or_hash_is_refused_by_the_freeze(self):
        marker = self.bench.copy / MARKER_FILE
        good = marker.read_text()
        other = {'pin': good.replace(self.bench.pin, '0' * 40), 'hash': good[:-2] + ('0' if good[-2] != '0' else '1') + '\n',
                 'old single marker': good.rsplit(' ', 1)[0] + '\n'}
        for label, text in other.items():
            with self.subTest(label):
                marker.write_text(text)
                self.bench.recapture()
                self.refuses('marker differs')

    def test_a_malformed_marker_is_refused_at_the_capture(self):
        marker = self.bench.copy / MARKER_FILE
        good = marker.read_text()
        for label, text in {'blank': good.rstrip('\n') + ' \n', 'no newline': good.rstrip('\n'), 'double newline': good + '\n'}.items():
            with self.subTest(label):
                marker.write_text(text)
                with self.assertRaisesRegex(ValueError, 'marker'):
                    subject.capture(self.bench.copy, self.bench.build / ('nm-%s.json' % len(label)), MARKER_FILE)

    def test_the_unpatched_tree_with_the_right_marker_is_refused(self):
        self.bench.rebuild_copy(names=[])
        (self.bench.copy / MARKER_FILE).write_text(zephyr_tree.marker_text(
            self.bench.pin, [p['sha256'] for p in self.bench.block['patches']]))
        self.bench.recapture()
        self.refuses('tree is not patched')

    def test_a_patched_file_that_differs_from_the_declared_hash_is_refused(self):
        (self.bench.copy / 'subsys' / 'b.c').write_bytes(b'b1\nsomething else\nb3\n')
        self.bench.recapture()
        self.refuses('patched file differs: subsys/b.c')

    def test_a_file_changed_in_the_copy_that_no_patch_touches_is_refused(self):
        (self.bench.copy / 'include' / 'x.h').write_bytes(b'#define X 2\n')
        self.bench.recapture()
        self.refuses('undeclared change.*include/x.h')

    def test_an_extra_file_in_the_copy_is_refused(self):
        (self.bench.copy / 'subsys' / 'extra.c').write_bytes(b'x\n')
        self.bench.recapture()
        self.refuses('undeclared change.*subsys/extra.c')

    def test_a_missing_file_in_the_copy_is_refused(self):
        (self.bench.copy / 'include' / 'x.h').unlink()
        self.bench.recapture()
        self.refuses('undeclared change.*include/x.h')

    def test_an_executable_bit_changed_in_the_copy_is_refused(self):
        os.chmod(self.bench.copy / 'scripts' / 'run.sh', 0o644)
        self.bench.recapture()
        self.refuses('undeclared change.*scripts/run.sh')

    def test_an_executable_bit_on_a_patched_file_is_a_difference_from_the_rederived_tree(self):
        os.chmod(self.bench.copy / 'subsys' / 'b.c', 0o755)                          # same bytes, so only the digest sees it
        self.bench.recapture()
        self.refuses('tree digest differs: the tree re-derived from the stock checkout is not the copy')

    def test_a_declared_patched_file_that_equals_the_stock_file_is_refused(self):
        def add_unpatched_file(block):
            block['patched_files'].append(dict(path='include/x.h', change='modified', patches=['first-1.patch'],
                                               sha256=hashlib.sha256(b'#define X 1\n').hexdigest()))
            block['patched_files'].sort(key=lambda f: f['path'].encode())
        self.rewrite_block(add_unpatched_file)
        self.refuses('(tree is not patched|declared patched files differ)')

    def test_declared_patched_files_that_differ_from_what_the_patches_touch_are_refused(self):
        for label, change in dict(
                missing=lambda b: b['patched_files'].pop(),
                changed_kind=lambda b: b['patched_files'][0].update(change='added'),
                other_patches=lambda b: b['patched_files'][0].update(patches=['first-1.patch']),
                renamed=lambda b: b['patched_files'][0].update(path='subsys/zz.c')).items():
            with self.subTest(label):
                self.setUp()
                self.rewrite_block(lambda b: (change(b), b['patched_files'].sort(key=lambda f: f['path'].encode())))
                self.refuses('declared patched files differ')

    def test_a_declared_tree_digest_that_differs_is_refused(self):
        self.rewrite_block(lambda b: b['tree'].update(sha256='0' * 64))
        self.refuses('tree digest differs')
        self.rewrite_block(lambda b: b['tree'].update(file_count=b['tree']['file_count'] + 1))
        self.refuses('tree digest differs')

    def test_a_block_of_the_old_shape_or_another_schema_is_refused(self):
        old = dict(decision='D8', patch='zephyr-tls-4.4.1.patch', patch_sha256='3' * 64, patched_file='a.c', patched_file_sha256='1' * 64,
                   pin=self.bench.pin)
        for label, block in dict(old=old, schema1=dict(self.bench.block, schema=1), none=None, absent=OMIT,
                                 mixed=dict(self.bench.block, patch='x.patch')).items():
            with self.subTest(label):
                self.bench.write_manifest(block)
                self.refuses('legacy or missing patched Zephyr block')

    def test_the_pin_must_be_the_same_in_the_block_the_checkout_and_the_marker(self):
        self.rewrite_block(lambda b: b.update(pin='1' * 40))
        self.refuses('pin differs')

    def test_the_stock_checkout_must_be_at_the_pin_and_clean(self):
        (self.bench.stock / 'subsys' / 'a.c').write_bytes(b'dirty\n')
        self.refuses('not clean')
        run(['git', 'checkout', '-q', '--', 'subsys/a.c'], cwd=self.bench.stock)
        (self.bench.stock / 'untracked.c').write_bytes(b'x\n')
        self.refuses('not clean')
        (self.bench.stock / 'untracked.c').unlink()
        (self.bench.stock / '.DS_Store').write_bytes(b'finder')                      # not a compiler input
        self.bench.freeze()
        (self.bench.stock / 'subsys' / 'a.c').write_bytes(b'a1\na2\na3\na4\na5\n')
        run(['git', '-c', 'commit.gpgsign=false', 'commit', '-q', '--allow-empty', '-m', 'moved'], cwd=self.bench.stock)
        self.refuses('pin differs')

    def test_the_copy_may_not_be_or_contain_or_sit_inside_the_stock_checkout(self):
        inside = self.bench.stock / 'copy-inside'
        shutil.copytree(self.bench.copy, inside, symlinks=True)
        with self.assertRaisesRegex(ValueError, 'overlap'):
            self.bench.freeze(copy=inside, pre=self.recapture_for(inside))
        with self.assertRaisesRegex(ValueError, 'overlap'):
            self.bench.freeze(copy=self.bench.stock, pre=self.recapture_for(self.bench.stock))

    def recapture_for(self, copy):
        pre = self.bench.build / ('pre-%s.json' % abs(hash(str(copy))))
        # the capture itself may refuse a .git entry; the stock checkout holds one, so write the file by hand
        data = json.loads(self.bench.pre.read_text())
        data['copy'] = str(copy)
        pre.write_text(json.dumps(data))
        return pre

    def projects(self, path):
        return [dict(name='zephyr', path='zephyr', revision=self.bench.pin, git_commit=self.bench.pin, dirty=False),
                dict(name='other', path=path, revision='1' * 40, git_commit='1' * 40, dirty=False)]

    def test_the_west_project_list_is_required_and_a_freeze_without_it_is_refused(self):
        arguments = dict(copy=self.bench.copy, zephyr=self.bench.stock, patches=self.bench.patches, manifest=self.bench.manifest,
                         compile_commands=self.bench.compile_commands, pre=self.bench.pre)
        for label, extra in dict(neither={}, inputs_only=dict(inputs=self.bench.inputs), workspace_only=dict(workspace=self.bench.workspace),
                                 none=dict(inputs=None, workspace=None)).items():
            with self.subTest(label):
                out = self.bench.out()
                with self.assertRaises((TypeError, ValueError)):
                    subject.freeze(out=out, **arguments, **extra)
                self.assertFalse(out.exists())

    def test_the_copy_may_not_overlap_a_west_project_of_the_inputs(self):
        for label, path in dict(same='zephyr-wolf', parent='.', sibling_inside='zephyr-wolf/sub', inside_it='zephyr-wolf/../zephyr-wolf').items():
            with self.subTest(label):
                self.bench.write_inputs(self.projects(path))
                out = self.bench.out()
                with self.assertRaisesRegex(ValueError, 'west project'):
                    self.bench.freeze(out=out)
                self.assertFalse(out.exists())
        self.bench.write_inputs(self.projects('modules/hal/nxp'))
        self.bench.freeze()

    def test_the_copy_inside_a_west_project_is_refused(self):
        self.bench.write_inputs(self.projects('.'))                                  # a project at the workspace root contains the copy
        with self.assertRaisesRegex(ValueError, 'west project'):
            self.bench.freeze()
        copy_inside = self.bench.root / 'ws' / 'other' / 'copy'
        copy_inside.parent.mkdir()
        shutil.copytree(self.bench.copy, copy_inside, symlinks=True)
        self.bench.write_inputs(self.projects('other'))
        data = json.loads(self.bench.pre.read_text())
        data['copy'] = str(copy_inside)
        self.bench.pre = self.bench.build / 'pre-inside.json'
        self.bench.pre.write_text(json.dumps(data))
        with self.assertRaisesRegex(ValueError, 'west project'):
            self.bench.freeze(copy=copy_inside)

    def test_the_copy_registered_as_the_zephyr_project_is_refused(self):
        self.bench.write_inputs([dict(name='zephyr', path='zephyr-wolf', revision=self.bench.pin, git_commit=self.bench.pin, dirty=False)])
        with self.assertRaisesRegex(ValueError, 'west project'):
            self.bench.freeze()

    def test_the_stock_checkout_must_be_the_west_zephyr_project(self):
        for label, projects in dict(
                other_path=[dict(name='zephyr', path='elsewhere', revision=self.bench.pin, git_commit=self.bench.pin, dirty=False)],
                no_zephyr=[dict(name='other', path='modules/hal/nxp', revision='1' * 40, git_commit='1' * 40, dirty=False)],
                empty=[]).items():
            with self.subTest(label):
                self.bench.write_inputs(projects)
                out = self.bench.out()
                with self.assertRaisesRegex(ValueError, 'not the west project zephyr|no west project zephyr'):
                    self.bench.freeze(out=out)
                self.assertFalse(out.exists())

    def test_a_malformed_inputs_file_is_a_refusal_not_a_crash(self):
        for label, text in dict(not_json='nope', list='[]', no_projects='{}', projects_string='{"west_projects": "x"}',
                                entry_string='{"west_projects": ["x"]}', entry_none='{"west_projects": [null]}',
                                entry_without_path='{"west_projects": [{"name": "zephyr"}]}').items():
            with self.subTest(label):
                self.bench.inputs.write_text(text)
                with self.assertRaises(ValueError):
                    self.bench.freeze(out=self.bench.out())
        self.bench.inputs.unlink()
        with self.assertRaises(ValueError):
            self.bench.freeze(out=self.bench.out())

    def test_the_manifest_context_project_of_the_inputs_is_not_a_conflict(self):
        # the inputs file of a real build lists the manifest checkout too; it lives beside the workspace and does not overlap
        self.bench.write_inputs(self.projects('modules/hal/nxp') + [dict(name='manifest', path='crux-pilot', revision='HEAD',
                                                                       git_commit='e' * 40, dirty=False)])
        self.bench.freeze()


@NEEDS_PROGRAMS
class FreezeIndependentPatches(Base):
    """Two patches on disjoint files: the copy is identical in either order, so only the marker tells the order."""
    independent = True

    def test_the_two_orders_give_one_tree_and_the_marker_alone_decides(self):
        self.bench.freeze()
        reversed_order = list(reversed(self.bench.names))
        self.bench.rebuild_copy(names=reversed_order, marker_names=reversed_order)
        self.bench.recapture()
        self.refuses('marker differs')

    def test_the_declared_list_holds_each_file_once_with_its_patch_names(self):
        self.assertEqual([(f['path'], f['patches']) for f in self.bench.block['patched_files']],
                         [('subsys/a.c', ['first-1.patch']), ('subsys/b.c', ['first-2.patch']), ('subsys/new.c', ['first-1.patch'])])


@NEEDS_PROGRAMS
@unittest.skipUnless(available() and patch_deletes(), 'this patch program leaves an empty file where a diff deletes one: SKIPPED, not passed')
class DeletedFile(Base):
    delete = True

    def test_a_deleted_file_has_a_null_hash_and_no_frozen_copy(self):
        out = self.bench.build / 'frozen'
        record = self.bench.freeze(out=out)
        by_path = {f['path']: f for f in record['patched_files']}
        self.assertEqual((by_path['subsys/removed.c']['change'], by_path['subsys/removed.c']['sha256'],
                          by_path['subsys/removed.c']['frozen_path']), ('deleted', None, None))
        self.assertFalse((out / 'files' / 'subsys' / 'removed.c').exists())
        self.assertEqual(by_path['subsys/removed.c']['stock_sha256'], hashlib.sha256(b'gone soon\n').hexdigest())

    def test_a_file_that_reappears_in_the_copy_is_refused(self):
        (self.bench.copy / 'subsys' / 'removed.c').write_bytes(b'back again\n')
        self.bench.recapture()
        self.refuses('(patched file differs|undeclared change).*removed.c')


@NEEDS_PROGRAMS
class OnePatch(Base):
    count = 1

    def test_the_one_patch_case_freezes(self):
        record = self.bench.freeze()
        self.assertEqual([p['name'] for p in record['patches']], ['first-1.patch'])
        self.assertEqual((self.bench.copy / MARKER_FILE).read_text(), '%s %s\n' % (self.bench.pin, record['patches'][0]['sha256']))


@NEEDS_PROGRAMS
class Rederivation(Base):
    """T8: the real ``patch`` command on a synthetic stock tree and patches."""

    def derive(self, **override):
        arguments = dict(zephyr=self.bench.stock, pin=self.bench.pin, patches=[(n, (self.bench.patches / n).read_bytes()) for n in self.bench.names],
                         marker_file=MARKER_FILE, scratch=self.bench.root / 'scratch')
        arguments.update(override)
        return subject.rederive(**arguments)

    def test_the_rederived_tree_equals_the_copy_and_differs_from_the_stock_tree(self):
        derived = self.derive()
        self.assertEqual(derived['patched']['sha256'], zephyr_tree.tree_digest(self.bench.copy)['sha256'])
        self.assertNotEqual(derived['stock']['sha256'], derived['patched']['sha256'])
        self.assertEqual(derived['patched']['file_count'], derived['stock']['file_count'] + 2)   # the marker and new.c

    def test_the_stock_tree_has_the_excluded_directories_removed_by_the_archive_pathspec(self):
        names = [e['path'] for e in self.derive()['stock_entries']]
        self.assertIn('subsys/a.c', names)
        self.assertFalse([n for n in names if n.split('/')[0] in ('doc', 'samples', 'tests')])
        self.assertNotIn(MARKER_FILE, names)                                          # a stock tree has no marker

    def test_the_expected_marker_is_written_before_hashing(self):
        derived = self.derive()
        self.assertIn(MARKER_FILE, [e['path'] for e in derived['patched_entries']])
        self.assertEqual(derived['marker_text'], (self.bench.copy / MARKER_FILE).read_text())

    def test_a_wrong_marker_in_the_copy_fails_the_marker_check_and_the_digest(self):
        (self.bench.copy / MARKER_FILE).write_text('wrong\n')
        self.assertNotEqual(self.derive()['patched']['sha256'], zephyr_tree.tree_digest(self.bench.copy)['sha256'])
        data = json.loads(self.bench.pre.read_text())
        data['tree'] = {k: zephyr_tree.tree_digest(self.bench.copy)[k] for k in ('sha256', 'file_count', 'symlink_count')}
        data['marker_text'] = 'wrong\n'
        self.bench.pre.write_text(json.dumps(data))
        self.refuses('marker differs')

    def test_a_patch_that_does_not_apply_is_refused(self):
        with self.assertRaisesRegex(ValueError, 'patch'):
            self.derive(patches=[('second.patch', (self.bench.patches / 'first-2.patch').read_bytes())])      # needs the first one

    def test_the_patches_apply_in_the_given_order_only(self):
        order = [(n, (self.bench.patches / n).read_bytes()) for n in reversed(self.bench.names)]
        with self.assertRaises(ValueError):
            self.derive(patches=order)

    def test_a_scratch_directory_that_exists_is_refused(self):
        (self.bench.root / 'scratch').mkdir()
        with self.assertRaises(ValueError):
            self.derive()

    def test_the_command_line_is_the_one_of_the_prepare_script(self):
        script = (Path(__file__).resolve().parents[2] / 'scripts' / 'prepare-wolf-tls-zephyr.sh').read_text()
        self.assertIn('patch --strip=1 --no-backup-if-mismatch --quiet', script)
        self.assertEqual(subject.PATCH_COMMAND, ['patch', '--strip=1', '--no-backup-if-mismatch', '--quiet'])
        self.assertIn("git -C \"$workspace/zephyr\" archive HEAD ':!doc' ':!samples' ':!tests'", script)
        self.assertEqual(subject.ARCHIVE_EXCLUDES, [':!doc', ':!samples', ':!tests'])

    def test_a_missing_patch_program_is_a_refusal_and_never_a_pass(self):
        with unittest.mock.patch.object(subject.shutil, 'which', return_value=None):
            with self.assertRaisesRegex(ValueError, 'patch.*not found'):
                self.derive()


class RederivationWithoutPrograms(unittest.TestCase):
    def test_the_skip_is_visible(self):
        # reported, so that a run without git, tar or patch is read as "not run" and not as a pass
        if not available():
            self.skipTest('git, tar or patch missing: the re-derivation tests above were SKIPPED')


@NEEDS_PROGRAMS
class CompileCommands(Base):
    """T9."""

    def commands(self, *files, directory=None, as_relative=False):
        directory = directory or str(self.bench.root / 'repo' / 'build')
        entries = []
        for file in files:
            entries.append(dict(directory=directory, command='cc ' + file, file=os.path.relpath(file, directory) if as_relative else file))
        self.bench.compile_commands.write_text(json.dumps(entries))

    def test_all_zephyr_sources_under_the_copy_pass_and_are_counted(self):
        self.commands(str(self.bench.copy / 'kernel/init.c'), str(self.bench.copy / 'subsys/a.c'), str(self.bench.root / 'repo/src/main.c'))
        self.assertEqual(self.bench.freeze()['compile_commands'], dict(translation_units=3, sources_under_copy=2, sources_under_stock=0))

    def test_one_source_under_the_stock_checkout_is_refused(self):
        self.commands(str(self.bench.copy / 'kernel/init.c'), str(self.bench.stock / 'kernel/other.c'))
        self.refuses('build was not from the copy')

    def test_no_source_under_the_copy_is_refused(self):
        self.commands(str(self.bench.root / 'repo/src/main.c'))
        self.refuses('build was not from the copy')
        self.commands()
        self.refuses('build was not from the copy')

    def test_relative_paths_are_resolved_against_their_directory(self):
        self.commands(str(self.bench.copy / 'kernel/init.c'), as_relative=True)
        self.assertEqual(self.bench.freeze()['compile_commands']['sources_under_copy'], 1)
        self.commands(str(self.bench.stock / 'kernel/init.c'), str(self.bench.copy / 'x.c'), as_relative=True)
        self.refuses('build was not from the copy')

    def test_a_dot_dot_that_leads_back_into_the_stock_checkout_is_seen(self):
        sneaky = str(self.bench.copy / '..' / 'zephyr' / 'kernel' / 'init.c')
        self.commands(str(self.bench.copy / 'x.c'), sneaky)
        self.refuses('build was not from the copy')

    def test_paths_are_strings_and_a_prefix_of_the_name_is_not_a_prefix_of_the_path(self):
        self.commands(str(self.bench.copy) + '-other/x.c', str(self.bench.copy / 'x.c'))
        self.assertEqual(self.bench.freeze()['compile_commands']['sources_under_copy'], 1)
        self.commands(str(self.bench.stock) + '-x/x.c', str(self.bench.copy / 'x.c'))
        self.assertEqual(self.bench.freeze(out=self.bench.out())['compile_commands']['sources_under_stock'], 0)

    def test_a_freeze_run_from_another_mount_point_is_refused_not_translated(self):
        self.commands('/ws/zephyr-wolf/kernel/init.c')
        self.refuses('build was not from the copy')

    def test_a_malformed_database_is_refused(self):
        for text in ('', 'not json', '{}', '[1]', '[{"file": 3}]', '[{"directory": "/x"}]', '[{"file": "a.c"}]'):
            with self.subTest(text):
                self.bench.compile_commands.write_text(text)
                with self.assertRaises(ValueError):
                    self.bench.freeze(out=self.bench.out())


class CommandLine(unittest.TestCase):
    def test_the_command_line_has_capture_and_freeze_with_the_names_of_the_design(self):
        parser = subject.build_parser()
        capture = parser.parse_args(['capture', '--copy', 'c', '--out', 'o'])
        self.assertEqual((capture.action, capture.marker_file), ('capture', '.crux-wolf-tls'))
        complete = ['--copy', 'c', '--zephyr', 'z', '--patches', 'p', '--manifest', 'm', '--compile-commands', 'cc', '--pre', 'pre',
                    '--out', 'o', '--inputs', 'i', '--workspace', 'w']
        freeze = parser.parse_args(['freeze'] + complete)
        self.assertEqual((freeze.action, freeze.inputs, freeze.workspace), ('freeze', Path('i'), Path('w')))
        for missing in ('--inputs', '--workspace'):
            arguments = ['freeze'] + [a for pair in zip(*[iter(complete)] * 2) for a in pair if pair[0] != missing]
            with self.subTest(missing), self.assertRaises(SystemExit), unittest.mock.patch('sys.stderr'):
                parser.parse_args(arguments)
        with self.assertRaises(SystemExit), unittest.mock.patch('sys.stderr'):
            parser.parse_args(['freeze', '--copy', 'c'])

    @NEEDS_PROGRAMS
    def test_main_runs_both_steps_and_reports_a_refusal_with_a_status(self):
        with tempfile.TemporaryDirectory() as temp:
            bench = Bench(temp)
            out = bench.build / 'cli-frozen'
            pre = bench.build / 'cli-pre.json'
            with unittest.mock.patch('sys.stdout'):
                self.assertEqual(subject.main(['capture', '--copy', str(bench.copy), '--out', str(pre)]), 0)
            with unittest.mock.patch('sys.stdout'):
                self.assertEqual(subject.main(['freeze', '--copy', str(bench.copy), '--zephyr', str(bench.stock), '--patches', str(bench.patches),
                                           '--manifest', str(bench.manifest), '--compile-commands', str(bench.compile_commands),
                                               '--pre', str(pre), '--out', str(out), '--inputs', str(bench.inputs),
                                               '--workspace', str(bench.workspace)]), 0)
            self.assertTrue((out / 'patched-zephyr.json').is_file())
            (bench.patches / 'first-1.patch').write_bytes(b'tampered')
            with unittest.mock.patch('sys.stderr'):
                self.assertEqual(subject.main(['freeze', '--copy', str(bench.copy), '--zephyr', str(bench.stock), '--patches', str(bench.patches),
                                               '--manifest', str(bench.manifest), '--compile-commands', str(bench.compile_commands),
                                               '--pre', str(pre), '--out', str(bench.build / 'cli-frozen-2'), '--inputs', str(bench.inputs),
                                               '--workspace', str(bench.workspace)]), 1)
            self.assertFalse((bench.build / 'cli-frozen-2').exists())                # a refusal leaves no partial folder


if __name__ == '__main__':
    unittest.main()
