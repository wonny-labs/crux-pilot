"""What the bench image check refuses about the patched Zephyr copy: the 13 rows of the design's table (test T7).

``patched_zephyr.check_record`` reads the frozen manifest, the frozen record and the frozen artefacts and nothing else. The
fixture is a frozen folder written by hand (frozen_zephyr_fixtures.py), so these tests run without git, tar or patch; one test
with the real programs compares the shape of the fixture with a real freeze.

Dependence on the owner's answers (recorded 2026-11-10: Q1 yes, Q2 yes): the rows marked Q1 compare the record's digest with the
digest declared in the block, the rows marked Q2 use the re-derived tree.
"""
import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent / 'target'))
import bench_image
import patched_zephyr
from frozen_zephyr_fixtures import Frozen, MARKER_FILE, OMIT, REAL_PIN, sha
import patched_zephyr_fixtures


class Base(unittest.TestCase):
    count = 2

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.frozen = Frozen(self.temp.name, self.count)
        self.folder = self.frozen.folder

    def refuses(self, pattern, **arguments):
        with self.assertRaisesRegex(ValueError, pattern):
            self.frozen.check(**arguments)

    def rewrite_marker(self, text):
        """Change the frozen marker and keep the record's copy of its text and hash consistent, so that only the marker differs."""
        (self.folder / 'marker').write_text(text)
        self.frozen.edit_record(lambda r: r['marker'].update(text=text, sha256=sha(text.encode()), size_bytes=len(text.encode())))


class Accepts(Base):
    def test_the_two_patch_case_is_accepted_and_returns_the_declared_facts(self):
        facts = self.frozen.check()
        self.assertEqual([p['name'] for p in facts['patches']], ['first-1.patch', 'first-2.patch'])
        self.assertEqual([(f['path'], f['change'], f['patches']) for f in facts['patched_files']],
                         [('subsys/a.c', 'modified', ['first-1.patch', 'first-2.patch']), ('subsys/b.c', 'modified', ['first-2.patch']),
                          ('subsys/new.c', 'added', ['first-1.patch', 'first-2.patch'])])
        self.assertEqual((facts['pin'], facts['tree']), (REAL_PIN, self.frozen.block['tree']))

    def test_the_facts_are_json_serialisable_and_equal_on_a_second_call(self):
        self.assertEqual(json.loads(json.dumps(self.frozen.check())), self.frozen.check())

    def test_a_file_touched_by_both_patches_and_an_added_file_are_in_the_facts(self):
        paths = {f['path']: f for f in self.frozen.check()['patched_files']}
        self.assertEqual(paths['subsys/a.c']['patches'], self.frozen.names)
        self.assertEqual(paths['subsys/new.c']['change'], 'added')

    def test_the_check_never_reads_anything_but_the_frozen_folder_and_the_manifest(self):
        # the copy and the stock checkout live in the build environment: the folder is moved away from everything else
        moved = Path(self.temp.name) / 'elsewhere'
        shutil.copytree(self.temp.name + '/build', moved)
        self.assertEqual(patched_zephyr.check_record(moved / 'bench-manifest.json', moved / 'patched-zephyr' / 'patched-zephyr.json', REAL_PIN),
                         self.frozen.check())

    def test_the_pin_of_the_fixture_is_the_pin_of_the_bench_profile(self):
        self.assertEqual(REAL_PIN, bench_image.PINNED['zephyr'])
        self.assertEqual(MARKER_FILE, '.crux-wolf-tls')


class OnePatch(Base):
    count = 1

    def test_todays_block_migrated_to_the_list_is_accepted(self):
        facts = self.frozen.check()
        self.assertEqual([p['name'] for p in facts['patches']], ['first-1.patch'])
        marker = (self.folder / 'marker').read_text()
        self.assertEqual(marker, '%s %s\n' % (REAL_PIN, self.frozen.block['patches'][0]['sha256']))      # byte for byte today's marker


class Row1LegacyOrMissingBlock(Base):
    pattern = 'legacy or missing patched Zephyr block'

    def test_no_block_a_null_block_and_a_block_that_is_not_an_object(self):
        for label, block in dict(absent=OMIT, null=None, string='x', empty_list=[], number=2).items():
            with self.subTest(label):
                self.frozen.write_manifest(block)
                self.refuses(self.pattern)

    def test_a_block_without_schema_2(self):
        for label, change in dict(missing=lambda b: b.pop('schema'), one=lambda b: b.update(schema=1), three=lambda b: b.update(schema=3),
                                  string=lambda b: b.update(schema='2'), true=lambda b: b.update(schema=True)).items():
            with self.subTest(label):
                self.frozen.edit_block(change)
                self.refuses(self.pattern)

    def test_the_old_single_patch_block_and_a_block_with_any_old_key(self):
        old = dict(decision='D8', patch='zephyr-tls-4.4.1.patch', patch_sha256='3' * 64, patched_file='subsys/net/lib/sockets/sockets_tls.c',
                   patched_file_sha256='1' * 64, pin=REAL_PIN)
        self.frozen.write_manifest(old)
        self.refuses(self.pattern)
        for key in patched_zephyr.OLD_KEYS:
            with self.subTest(key):
                self.frozen.edit_block(lambda b, key=key: b.update({key: 'x'}))
                self.refuses(self.pattern)

    def test_the_recorded_single_patch_manifest_of_the_feasibility_run_is_refused(self):
        evidence = Path(__file__).resolve().parents[2] / 'docs/evidence/rt1060-management-image-feasibility/image-plan-c8/build/bench-manifest.json'
        self.frozen.manifest.write_text(evidence.read_text())
        self.refuses(self.pattern)

    def test_a_block_with_another_shape_is_refused_as_malformed(self):
        for label, change in dict(
                extra_key=lambda b: b.update(extra=1), no_patches=lambda b: b.update(patches=[]), no_files=lambda b: b.update(patched_files=[]),
                short_pin=lambda b: b.update(pin='abc'), bad_hash=lambda b: b['patches'][0].update(sha256='xyz'),
                duplicate_patch=lambda b: b['patches'].append(dict(b['patches'][0])),
                unsorted_files=lambda b: b['patched_files'].reverse(),
                deleted_with_hash=lambda b: b['patched_files'][0].update(change='deleted'),
                added_without_hash=lambda b: b['patched_files'][2].update(sha256=None),
                unknown_patch_name=lambda b: b['patched_files'][0].update(patches=['other.patch']),
                patches_out_of_order=lambda b: b['patched_files'][0].update(patches=['first-2.patch', 'first-1.patch']),
                marker_as_patched_file=lambda b: b['patched_files'].insert(0, dict(b['patched_files'][0], path=MARKER_FILE)),
                excluded_name=lambda b: b['patched_files'].append(dict(b['patched_files'][0], path='x/__pycache__/m.py')),
                unsafe_path=lambda b: b['patched_files'][0].update(path='../a.c'),
                other_algorithm=lambda b: b['tree'].update(algorithm='crux-zephyr-tree-v2'),
                zero_count=lambda b: b['tree'].update(file_count=0), bool_count=lambda b: b['tree'].update(file_count=True),
                short_tree=lambda b: b['tree'].update(sha256='1'),
                marker_path=lambda b: b.update(marker_file='a/b'), bad_patch_name=lambda b: b['patches'][0].update(name='../x')).items():
            with self.subTest(label):
                self.frozen.edit_block(change, resign=True)
                self.refuses('Malformed patched Zephyr block')


class Row2RecordAndManifestDiffer(Base):
    pattern = 'record and manifest differ'

    def test_a_missing_record(self):
        self.frozen.record_path.unlink()
        self.refuses(self.pattern)

    def test_a_record_that_is_not_json_or_not_an_object(self):
        for text in ('', 'not json', '[]', '"x"', 'null'):
            with self.subTest(text):
                self.frozen.record_path.write_text(text)
                self.refuses(self.pattern)

    def test_an_unknown_schema_or_algorithm(self):
        for label, change in dict(schema=lambda r: r.update(schema=2), schema_missing=lambda r: r.pop('schema'),
                                  algorithm=lambda r: r.update(algorithm='crux-zephyr-tree-v2'), extra_key=lambda r: r.update(extra=1),
                                  missing_key=lambda r: r.pop('compile_commands')).items():
            with self.subTest(label):
                self.setUp()
                self.frozen.edit_record(change)
                self.refuses(self.pattern)

    def test_a_manifest_block_hash_that_differs(self):
        self.frozen.edit_record(lambda r: r.update(manifest_block_sha256='0' * 64))
        self.refuses(self.pattern)

    def test_a_block_that_was_changed_after_the_record_was_made(self):
        self.frozen.edit_block(lambda b: b.update(decision='D9'))
        self.refuses(self.pattern)

    def test_a_malformed_record_is_a_refusal_not_a_crash(self):
        for label, change in dict(patches_string=lambda r: r.update(patches='x'), patch_without_name=lambda r: r['patches'][0].pop('name'),
                                  tree_none=lambda r: r.update(tree=None), files_none=lambda r: r.update(patched_files=None),
                                  marker_none=lambda r: r.update(marker=None)).items():
            with self.subTest(label):
                self.setUp()
                self.frozen.edit_record(change)
                with self.assertRaises(ValueError):
                    self.frozen.check()


class Row3MissingPatch(Base):
    pattern = 'missing patch first-2.patch'

    def test_missing_from_the_record(self):
        self.frozen.edit_record(lambda r: r['patches'].pop())
        self.refuses(self.pattern)

    def test_missing_from_the_marker(self):
        self.rewrite_marker('%s %s\n' % (REAL_PIN, self.frozen.block['patches'][0]['sha256']))             # the old one-patch marker
        self.refuses(self.pattern)

    def test_missing_from_the_frozen_artefacts(self):
        (self.folder / 'patches' / 'first-2.patch').unlink()
        self.refuses(self.pattern)

    def test_missing_when_the_first_patch_is_the_one_that_is_absent(self):
        self.frozen.edit_record(lambda r: r['patches'].pop(0))
        self.refuses('missing patch first-1.patch')


class Row4ExtraPatch(Base):
    pattern = 'unexpected patch'

    def test_an_extra_patch_in_the_record(self):
        self.frozen.edit_record(lambda r: r['patches'].append(dict(r['patches'][0], name='third.patch', path='patches/third.patch')))
        self.refuses(self.pattern + ' third.patch')

    def test_an_extra_hash_in_the_marker(self):
        text = (self.folder / 'marker').read_text().rstrip('\n') + ' ' + '7' * 64 + '\n'
        self.rewrite_marker(text)
        self.refuses(self.pattern)

    def test_a_marker_in_which_one_hash_is_replaced_by_another(self):
        hashes = [p['sha256'] for p in self.frozen.block['patches']]
        self.rewrite_marker('%s %s %s\n' % (REAL_PIN, hashes[0], '7' * 64))
        self.refuses('(missing|unexpected) patch')


class Row5ChangedPatch(Base):
    pattern = 'changed patch first-1.patch'

    def test_frozen_bytes_that_do_not_hash_to_the_block_value(self):
        (self.folder / 'patches' / 'first-1.patch').write_bytes(b'another patch\n')
        self.refuses(self.pattern)

    def test_a_record_that_names_another_hash_than_the_block(self):
        self.frozen.edit_record(lambda r: r['patches'][0].update(sha256='5' * 64))
        self.refuses(self.pattern)

    def test_a_single_appended_byte_is_enough(self):
        path = self.folder / 'patches' / 'first-1.patch'
        path.write_bytes(path.read_bytes() + b'\n')
        self.refuses(self.pattern)

    def test_the_second_patch_changed(self):
        (self.folder / 'patches' / 'first-2.patch').write_bytes(b'x')
        self.refuses('changed patch first-2.patch')


class Row6OutOfOrder(Base):
    pattern = 'patches out of order'

    def test_the_record_lists_the_patches_in_the_other_order(self):
        self.frozen.edit_record(lambda r: r['patches'].reverse())
        self.refuses(self.pattern)

    def test_the_marker_lists_the_hashes_in_the_other_order(self):
        hashes = [p['sha256'] for p in self.frozen.block['patches']]
        self.rewrite_marker('%s %s %s\n' % (REAL_PIN, hashes[1], hashes[0]))
        self.refuses(self.pattern)

    def test_the_block_itself_in_the_other_order_does_not_hide_the_difference(self):
        self.frozen.edit_block(lambda b: (b['patches'].reverse(), [f.update(patches=list(reversed(f['patches']))) for f in b['patched_files']]),
                               resign=True)
        self.refuses(self.pattern)                # the record and the marker were made for the declared order


class Row7Marker(Base):
    pattern = 'marker differs'

    def test_an_absent_marker(self):
        (self.folder / 'marker').unlink()
        self.refuses(self.pattern)

    def test_a_marker_of_another_pin(self):
        hashes = ' '.join(p['sha256'] for p in self.frozen.block['patches'])
        self.rewrite_marker('%s %s\n' % ('1' * 40, hashes))
        self.refuses('(marker differs|pin differs)')

    def test_malformed_markers(self):
        hashes = ' '.join(p['sha256'] for p in self.frozen.block['patches'])
        good = '%s %s' % (REAL_PIN, hashes)
        for label, text in dict(no_newline=good, trailing_blank=good + ' \n', double_newline=good + '\n\n', tab=good.replace(' ', '\t', 1) + '\n',
                                crlf=good + '\r\n', empty='', only_pin=REAL_PIN + '\n', upper=good.upper() + '\n').items():
            with self.subTest(label):
                self.setUp()
                self.rewrite_marker(text)
                self.refuses(self.pattern)

    def test_a_frozen_marker_that_the_record_does_not_describe(self):
        (self.folder / 'marker').write_text((self.folder / 'marker').read_text().replace(REAL_PIN[:8], '0' * 8, 1))
        self.refuses(self.pattern)

    def test_a_marker_that_is_not_utf8(self):
        (self.folder / 'marker').write_bytes(b'\xff\xfe')
        self.refuses(self.pattern)


class Row8Pin(Base):
    pattern = 'pin differs'

    def test_the_record_names_another_pin(self):
        self.frozen.edit_record(lambda r: r.update(pin='2' * 40))
        self.refuses(self.pattern)

    def test_the_block_names_another_pin_than_the_profile(self):
        self.refuses(self.pattern, expected_pin='3' * 40)

    def test_the_block_and_the_record_agree_with_each_other_and_not_with_the_profile(self):
        self.frozen.edit_block(lambda b: b.update(pin='4' * 40), resign=True)
        self.frozen.edit_record(lambda r: r.update(pin='4' * 40))
        self.refuses('(pin differs|marker differs)')


class Row9UnpatchedTree(Base):
    pattern = 'tree is not patched'

    def test_a_patched_file_missing_from_the_record(self):
        self.frozen.edit_record(lambda r: r['patched_files'].pop(1))
        self.refuses(self.pattern)

    def test_a_patched_file_that_equals_its_stock_hash(self):
        self.frozen.edit_record(lambda r: r['patched_files'][0].update(stock_sha256=r['patched_files'][0]['sha256']))
        self.refuses(self.pattern)

    def test_the_rederived_stock_digest_equals_the_copy_digest(self):
        self.frozen.edit_record(lambda r: r['tree']['stock'].update(sha256=r['tree']['post']['sha256']))
        self.refuses(self.pattern)

    def test_an_empty_list_of_patched_files(self):
        self.frozen.edit_record(lambda r: r.update(patched_files=[]))
        self.refuses(self.pattern)


class Row10PatchedFile(Base):
    pattern = 'patched file differs'

    def test_a_final_hash_that_differs_from_the_block(self):
        self.frozen.edit_record(lambda r: r['patched_files'][1].update(sha256='6' * 64))
        self.refuses(self.pattern + ': subsys/b.c')

    def test_a_patched_file_in_the_record_that_the_block_lacks(self):
        self.frozen.edit_record(lambda r: r['patched_files'].append(dict(r['patched_files'][0], path='subsys/extra.c', frozen_path='files/subsys/a.c')))
        self.refuses(self.pattern + ': subsys/extra.c')

    def test_a_change_or_patch_list_that_differs(self):
        for label, change in dict(change=lambda r: r['patched_files'][0].update(change='added'),
                                  patches=lambda r: r['patched_files'][0].update(patches=['first-1.patch'])).items():
            with self.subTest(label):
                self.setUp()
                self.frozen.edit_record(change)
                self.refuses(self.pattern)

    def test_a_path_listed_twice(self):
        self.frozen.edit_record(lambda r: r['patched_files'].append(dict(r['patched_files'][0])))
        self.refuses(self.pattern)


class Row11TreeDigest(Base):
    pattern = 'tree digest differs'

    def test_the_record_digest_differs_from_the_declared_digest(self):
        self.frozen.edit_record(lambda r: (r['tree']['post'].update(sha256='8' * 64), r['tree']['pre'].update(sha256='8' * 64),
                                           r['tree']['rederived'].update(sha256='8' * 64)))
        self.refuses(self.pattern)

    def test_the_file_count_differs(self):
        self.frozen.edit_record(lambda r: [r['tree'][k].update(file_count=99) for k in ('pre', 'post', 'rederived')])
        self.refuses(self.pattern)

    def test_the_pre_and_post_digests_differ(self):
        self.frozen.edit_record(lambda r: r['tree']['pre'].update(sha256='9' * 64))
        self.refuses(self.pattern)

    def test_the_rederived_digest_differs_or_is_absent(self):
        self.frozen.edit_record(lambda r: r['tree']['rederived'].update(sha256='a' * 64))
        self.refuses(self.pattern)
        self.setUp()
        self.frozen.edit_record(lambda r: r['tree'].pop('rederived'))
        with self.assertRaises(ValueError):
            self.frozen.check()
        self.setUp()
        self.frozen.edit_record(lambda r: r['tree'].update(rederived=None))
        with self.assertRaises(ValueError):
            self.frozen.check()

    def test_the_symlink_count_differs_between_the_copy_and_the_rederived_tree(self):
        self.frozen.edit_record(lambda r: r['tree']['rederived'].update(symlink_count=0))
        self.refuses(self.pattern)

    def test_the_block_declares_another_digest_than_the_copy(self):
        self.frozen.edit_block(lambda b: b['tree'].update(sha256='b' * 64), resign=True)
        self.refuses(self.pattern)

    def test_the_block_declares_another_algorithm_than_the_record(self):
        self.frozen.edit_record(lambda r: r.update(algorithm='other'))
        self.refuses('record and manifest differ')


class Row12CompiledTree(Base):
    pattern = 'build was not from the copy'

    def test_sources_under_the_stock_checkout(self):
        self.frozen.edit_record(lambda r: r['compile_commands'].update(sources_under_stock=1))
        self.refuses(self.pattern)

    def test_no_source_under_the_copy(self):
        self.frozen.edit_record(lambda r: r['compile_commands'].update(sources_under_copy=0))
        self.refuses(self.pattern)

    def test_counts_that_are_not_integers(self):
        for label, change in dict(string=lambda r: r['compile_commands'].update(sources_under_copy='2'),
                                  true=lambda r: r['compile_commands'].update(sources_under_copy=True),
                                  float=lambda r: r['compile_commands'].update(sources_under_stock=0.0),
                                  none=lambda r: r['compile_commands'].update(sources_under_stock=None)).items():
            with self.subTest(label):
                self.setUp()
                self.frozen.edit_record(change)
                self.refuses(self.pattern)


class Row13FrozenArtefact(Base):
    pattern = 'frozen artefact changed'

    def test_a_patched_file_copy_with_other_bytes(self):
        (self.folder / 'files' / 'subsys' / 'a.c').write_bytes(b'tampered\n')
        self.refuses(self.pattern)

    def test_a_missing_patched_file_copy(self):
        (self.folder / 'files' / 'subsys' / 'new.c').unlink()
        self.refuses(self.pattern)

    def test_a_changed_or_missing_tree_listing(self):
        (self.folder / 'tree-files.tsv').write_bytes(b'tampered\n')
        self.refuses(self.pattern)
        (self.folder / 'tree-files.tsv').unlink()
        self.refuses(self.pattern)

    def test_a_frozen_artefact_replaced_by_a_symlink(self):
        target = Path(self.temp.name) / 'outside.c'
        target.write_bytes(b'a final\n')                                     # the same bytes: the link itself is refused
        (self.folder / 'files' / 'subsys' / 'a.c').unlink()
        (self.folder / 'files' / 'subsys' / 'a.c').symlink_to(target)
        self.refuses(self.pattern)

    def test_a_path_that_leaves_the_folder_or_is_not_the_expected_one(self):
        for label, change in dict(
                dotdot=lambda r: r['patched_files'][0].update(frozen_path='../outside.c'),
                absolute=lambda r: r['patched_files'][0].update(frozen_path='/etc/hosts'),
                other_name=lambda r: r['patched_files'][0].update(frozen_path='files/subsys/b.c'),
                tree_listing_path=lambda r: r['tree_listing'].update(path='marker'),
                patch_path=lambda r: r['patches'][0].update(path='files/subsys/a.c')).items():
            with self.subTest(label):
                self.setUp()
                self.frozen.edit_record(change)
                with self.assertRaises(ValueError):
                    self.frozen.check()

    def test_a_record_size_or_hash_that_does_not_match_the_file(self):
        for label, change in dict(size=lambda r: r['patched_files'][0].update(size_bytes=1),
                                  listing_hash=lambda r: r['tree_listing'].update(sha256='0' * 64)).items():
            with self.subTest(label):
                self.setUp()
                self.frozen.edit_record(change)
                with self.assertRaises(ValueError):
                    self.frozen.check()

    def test_a_deleted_file_with_a_frozen_copy_is_refused(self):
        def deleted(block):
            block['patched_files'][0].update(change='deleted', sha256=None)
        self.frozen.edit_block(deleted, resign=True)
        self.frozen.edit_record(lambda r: r['patched_files'][0].update(change='deleted', sha256=None))
        with self.assertRaises(ValueError):
            self.frozen.check()


class ShapeOfARealFreeze(unittest.TestCase):
    """The hand-written fixture and the record of the real freeze must have the same schema."""

    @unittest.skipUnless(patched_zephyr_fixtures.available(), 'git, tar and patch are needed: SKIPPED, not passed')
    def test_the_fixture_has_the_keys_of_a_real_record(self):
        def keys(value, prefix=''):
            if isinstance(value, dict):
                return {prefix + '/' + k for k in value} | {x for k, v in value.items() for x in keys(v, prefix + '/' + k)}
            if isinstance(value, list) and value:
                return keys(value[0], prefix + '[]')
            return set()
        with tempfile.TemporaryDirectory() as real_root, tempfile.TemporaryDirectory() as fake_root:
            bench = patched_zephyr_fixtures.Bench(real_root)
            real = bench.freeze()
            fake = Frozen(fake_root).record
            self.assertEqual(keys(real), keys(fake))
            self.assertEqual(set(real), patched_zephyr.RECORD_KEYS)
            self.assertEqual(set(bench.block), patched_zephyr.BLOCK_KEYS)
            # and the check accepts the real freeze
            folder = bench.build / 'patched-zephyr'
            facts = patched_zephyr.check_record(bench.manifest, folder / 'patched-zephyr.json', bench.pin)
            self.assertEqual(facts['patches'], bench.block['patches'])
            self.assertEqual(facts['tree'], bench.block['tree'])


if __name__ == '__main__':
    unittest.main()
