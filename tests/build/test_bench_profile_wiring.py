"""The bench profile freezes and verifies with its two extra roles, ``bench_manifest`` and ``patched_zephyr`` (design B2, test T10).

The M5 and S2 profiles gain no role and their own tests pass unchanged. The bench profile stays UNREGISTERED. The stock Zephyr
project keeps its dirty-pin refusal: a valid patched-Zephyr record is evidence next to it and never an exception to it.
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

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent / 'target'))
import bench_image
import patched_zephyr
import prepare_m5_network as subject
from image_profile import ImageProfile, KNOWN_EXTRA_ROLES
from frozen_zephyr_fixtures import Frozen
from test_bench_image_profile import CONFIG, synthetic_image
from test_m5_image_profile import Fixture


def project(name):
    revision = bench_image.PINNED[name]
    return dict(name=name, revision=revision, path=bench_image.PINNED_PATHS[name], git_commit=revision, dirty=False)


class BenchFixture(Fixture):
    """A build directory of the bench image: the synthetic bench image, its frozen manifest and its frozen patched-Zephyr folder."""

    def projects(self, context_path=None):
        projects = [project(name) for name in bench_image.PINNED]
        if context_path:
            projects.insert(0, dict(name='manifest', path=context_path, revision='HEAD', git_commit='e' * 40, dirty=False))
        return projects

    def setUp(self):
        super().setUp()
        self.profile = bench_image.bench_profile()
        folder = self.build / 'zephyr'
        (self.root / 'image').mkdir()
        binary, elf, config = synthetic_image(self.root / 'image')
        for source, name in ((binary, 'zephyr.bin'), (elf, 'zephyr.elf'), (config, '.config')):
            (folder / name).write_bytes(source.read_bytes())
        self.set_projects(self.projects())
        self.frozen = Frozen(self.root)                        # build/bench-manifest.json and build/patched-zephyr/

    def prepare(self, profile=None, **extra):
        return super().prepare(profile or self.profile, **extra)

    def verify(self, profile=None):
        return super().verify(profile or self.profile)

    def resign(self, change):
        """Change the manifest on disk and return its new digest, as an approval record would have to name it."""
        record = json.loads(self.output.read_text())
        change(record)
        self.output.write_text(json.dumps(record, indent=2, sort_keys=True) + '\n')
        return subject.sha(self.output)


class Freezes(BenchFixture):
    def test_the_bench_image_freezes_and_verifies_with_both_extra_roles(self):
        record = self.prepare()
        self.assertEqual(record['profile'], bench_image.NAME)
        self.assertEqual({'bench_manifest', 'patched_zephyr'} - set(record['files']), set())
        self.assertEqual(record['files']['bench_manifest'], str((self.build / 'bench-manifest.json').resolve()))
        self.assertEqual(record['files']['patched_zephyr'], str((self.build / 'patched-zephyr' / 'patched-zephyr.json').resolve()))
        self.assertEqual(self.verify()['profile'], bench_image.NAME)

    def test_every_file_that_the_record_names_is_frozen_as_an_artefact(self):
        record = self.prepare()
        frozen = {a['path'] for a in record['artifacts']}
        named = patched_zephyr.named_files(self.frozen.record_path)
        for relative in list(named) + ['patched-zephyr.json']:
            self.assertIn(str((self.build / 'patched-zephyr' / relative).resolve()), frozen, relative)
        self.assertIn(str((self.build / 'bench-manifest.json').resolve()), frozen)

    def test_the_image_facts_carry_the_patched_zephyr_facts_into_the_manifest_digest(self):
        record = self.prepare()
        facts = record['image']['patched_zephyr']
        self.assertEqual((facts['pin'], facts['tree']), (bench_image.PINNED['zephyr'], self.frozen.block['tree']))
        self.assertEqual([p['name'] for p in facts['patches']], ['first-1.patch', 'first-2.patch'])
        self.assertEqual(facts['manifest_block_sha256'], self.frozen.record['manifest_block_sha256'])

    def test_the_facts_are_returned_by_check_image_and_equal_at_verify(self):
        self.prepare()
        record = self.verify()
        self.assertEqual(record['image']['patched_zephyr']['patches'], self.frozen.block['patches'])

    def test_a_one_patch_block_freezes_too(self):
        (self.root / 'single').mkdir()
        single = Frozen(self.root / 'single', 1)
        shutil.rmtree(self.build / 'patched-zephyr')
        shutil.copytree(single.folder, self.build / 'patched-zephyr')
        shutil.copy(single.manifest, self.build / 'bench-manifest.json')
        record = self.prepare()
        self.assertEqual([p['name'] for p in record['image']['patched_zephyr']['patches']], ['first-1.patch'])


class Refuses(BenchFixture):
    def test_prepare_refuses_a_missing_record_or_manifest(self):
        for name in ('patched-zephyr/patched-zephyr.json', 'bench-manifest.json'):
            with self.subTest(name):
                BenchFixture.setUp(self)
                self.output = self.root / 'manifest-missing.json'
                (self.build / name).unlink()
                with self.assertRaises(ValueError):
                    self.prepare()
                self.assertFalse(self.output.exists())

    def test_prepare_refuses_a_changed_patch_a_changed_marker_and_a_legacy_block(self):
        cases = dict(
            patch=lambda f: (f.folder / 'patches' / 'first-1.patch').write_bytes(b'changed'),
            marker=lambda f: (f.folder / 'marker').write_text('changed\n'),
            legacy=lambda f: f.write_manifest(dict(decision='D8', patch='x.patch', patch_sha256='3' * 64, patched_file='a.c',
                                                   patched_file_sha256='1' * 64, pin=bench_image.PINNED['zephyr'])))
        for label, change in cases.items():
            with self.subTest(label):
                BenchFixture.setUp(self)
                self.output = self.root / ('manifest-%s.json' % label)
                change(self.frozen)
                with self.assertRaisesRegex(ValueError, 'patch|marker|legacy'):
                    self.prepare()
                self.assertFalse(self.output.exists())

    def test_verify_refuses_a_frozen_artefact_changed_after_the_freeze(self):
        self.prepare()
        (self.build / 'patched-zephyr' / 'patches' / 'first-2.patch').write_bytes(b'changed after the freeze\n')
        with self.assertRaisesRegex(ValueError, 'Frozen artifact changed'):
            self.verify()

    def test_verify_refuses_a_changed_bench_manifest_and_a_changed_record(self):
        self.prepare()
        for victim in ('bench-manifest.json', 'patched-zephyr/patched-zephyr.json'):
            with self.subTest(victim):
                path = self.build / victim
                original = path.read_bytes()
                path.write_bytes(original + b' ')
                with self.assertRaisesRegex(ValueError, 'Frozen artifact changed'):
                    self.verify()
                path.write_bytes(original)
        self.verify()

    def test_verify_refuses_a_manifest_that_lacks_one_of_the_roles(self):
        self.prepare()
        for role in ('bench_manifest', 'patched_zephyr'):
            with self.subTest(role):
                digest = self.resign(lambda record, role=role: record['files'].pop(role, None))
                with self.assertRaisesRegex(ValueError, 'Incomplete image roles'):
                    subject.verify(self.output, digest, self.profile)
                BenchFixture.setUp(self)
                self.prepare()

    def test_verify_refuses_a_manifest_in_which_a_named_file_is_not_an_artefact(self):
        self.prepare()
        record = json.loads(self.output.read_text())
        named = str((self.build / 'patched-zephyr' / 'files' / 'subsys' / 'a.c').resolve())
        digest = self.resign(lambda r: r.update(artifacts=[a for a in r['artifacts'] if a['path'] != named]))
        with self.assertRaisesRegex(ValueError, 'named by the patched Zephyr record'):
            subject.verify(self.output, digest, self.profile)
        self.assertIn(named, {a['path'] for a in record['artifacts']})

    def test_verify_refuses_an_image_fact_that_differs(self):
        self.prepare()
        digest = self.resign(lambda r: r['image']['patched_zephyr']['tree'].update(sha256='f' * 64))
        with self.assertRaisesRegex(ValueError, 'Image geometry/source identity differs'):
            subject.verify(self.output, digest, self.profile)

    def test_a_profile_that_names_the_roles_refuses_a_check_that_ignores_them(self):
        profile = ImageProfile(**{**self.profile.__dict__, 'check_image': lambda binary, elf, config: {'geometry': 'ignores the roles'}})
        with self.assertRaises(TypeError):
            self.prepare(profile)


class DirtyPinRefusalUnchanged(BenchFixture):
    """The stock zephyr project keeps its refusal; a valid patched-Zephyr record changes nothing about it."""

    def refuses_with_a_valid_record(self, change):
        data = json.loads(self.inputs.read_text())
        change(data)
        with self.assertRaises(ValueError):
            subject.frozen_inputs(data, self.profile)
        self.inputs.write_text(json.dumps(data))
        self.set_projects(data['west_projects'])
        self.frozen.check()                                                              # the patched-Zephyr evidence itself is valid
        self.output = self.root / 'dirty.json'
        with self.assertRaises(ValueError):
            self.prepare()
        self.assertFalse(self.output.exists())

    def zephyr(self, data):
        return next(p for p in data['west_projects'] if p['name'] == 'zephyr')

    def test_a_dirty_zephyr_project_is_refused(self):
        self.refuses_with_a_valid_record(lambda data: self.zephyr(data).update(dirty=True))

    def test_a_zephyr_project_off_the_pin_is_refused(self):
        self.refuses_with_a_valid_record(lambda data: self.zephyr(data).update(revision='1' * 40, git_commit='1' * 40))

    def test_a_zephyr_project_at_the_path_of_the_copy_is_refused(self):
        self.refuses_with_a_valid_record(lambda data: self.zephyr(data).update(path='zephyr-wolf'))

    def test_the_copy_as_one_more_west_project_overlapping_the_pinned_zephyr_is_refused(self):
        extra = dict(name='zephyr-wolf', revision='2' * 40, path='zephyr/wolf-copy', git_commit='2' * 40, dirty=False)
        self.refuses_with_a_valid_record(lambda data: data['west_projects'].append(extra))

    def test_a_clean_workspace_is_accepted_with_the_record(self):
        self.prepare()


class Profile(unittest.TestCase):
    def setUp(self):
        self.profile = bench_image.bench_profile()

    def test_the_bench_profile_has_the_two_roles_and_the_new_tool_script_and_stays_unregistered(self):
        self.assertEqual(self.profile.extra_roles, ('bench_manifest', 'patched_zephyr'))
        self.assertEqual(self.profile.tool_scripts, ('record-build-provenance.py', 'freeze-patched-zephyr.sh'))
        self.assertNotIn(self.profile.name, subject.PROFILES)
        self.assertEqual(sorted(subject.PROFILES), ['m5-network-physical', 's2-controller-physical'])

    def test_the_m5_and_s2_profiles_gain_no_role_and_the_shared_role_set_is_unchanged(self):
        self.assertEqual(subject.profile_named('m5-network-physical').extra_roles, ())
        self.assertEqual(subject.profile_named('s2-controller-physical').extra_roles, ())
        self.assertEqual(subject.ROLES, {'binary', 'elf', 'config', 'map', 'dts', 'compilation', 'inputs', 'provenance', 'errno'})

    def test_the_extra_role_files_are_where_the_freeze_writes_them(self):
        self.assertEqual(subject.EXTRA_ROLE_FILES, {'bench_manifest': 'bench-manifest.json',
                                                    'patched_zephyr': 'patched-zephyr/patched-zephyr.json'})
        self.assertEqual(set(subject.EXTRA_ROLE_FILES), set(KNOWN_EXTRA_ROLES))
        self.assertEqual(patched_zephyr.RECORD_NAME, 'patched-zephyr.json')

    def test_the_extra_roles_are_validated(self):
        base = {**self.profile.__dict__}
        ImageProfile(**{**base, 'extra_roles': ()})
        ImageProfile(**{**base, 'extra_roles': ('bench_manifest',)})
        for label, roles in dict(unknown=('other',), duplicate=('bench_manifest', 'bench_manifest'), not_a_tuple=['bench_manifest'],
                                 record_without_manifest=('patched_zephyr',), role_of_the_set=('binary',), text='bench_manifest').items():
            with self.subTest(label), self.assertRaises(ValueError):
                ImageProfile(**{**base, 'extra_roles': roles})

    def test_the_check_requires_the_roles_as_keywords_and_has_no_way_to_skip_them(self):
        import inspect
        parameters = inspect.signature(bench_image.check_image).parameters
        self.assertEqual([n for n, p in parameters.items() if p.kind is p.KEYWORD_ONLY and p.default is p.empty],
                         ['bench_manifest', 'patched_zephyr'])
        self.assertEqual([n for n, p in parameters.items() if p.default is not p.empty], [])
        with tempfile.TemporaryDirectory() as temp:
            paths = synthetic_image(Path(temp))
            with self.assertRaises(TypeError):
                bench_image.check_image(*paths)
            with self.assertRaises(TypeError):
                bench_image.check_image(*paths, bench_manifest=Path(temp) / 'x')
            with self.assertRaises(TypeError):
                bench_image.check_image(*paths, bench_manifest=None, patched_zephyr=None, other=1)

    def test_the_new_modules_and_the_wrapper_are_frozen_as_tools(self):
        names = {p.name for p in subject.default_tools(ROOT, self.profile)}
        self.assertTrue({'zephyr_tree.py', 'patched_zephyr.py', 'freeze-patched-zephyr.sh', 'record-build-provenance.py'} <= names)

    def test_the_wrapper_script_is_executable_and_valid_shell_and_has_no_hardware_path(self):
        script = ROOT / 'scripts' / 'freeze-patched-zephyr.sh'
        self.assertTrue(os.access(script, os.X_OK))
        self.assertEqual(subprocess.run(['bash', '-n', str(script)]).returncode, 0)
        text = script.read_text()
        for word in ('JLink', 'west flash', 'sudo', 'curl', 'pyocd'):
            self.assertNotIn(word, text)

    def test_the_wrapper_passes_the_two_actions_to_the_module_and_refuses_anything_else(self):
        script = ROOT / 'scripts' / 'freeze-patched-zephyr.sh'
        for arguments in ([], ['other'], ['--help-me']):
            with self.subTest(arguments):
                done = subprocess.run(['bash', str(script)] + arguments, capture_output=True, text=True)
                self.assertEqual(done.returncode, 2)
                self.assertIn('Usage', done.stderr)
        done = subprocess.run(['bash', str(script), 'capture', '--help'], capture_output=True, text=True)
        self.assertEqual(done.returncode, 0)
        self.assertIn('--copy', done.stdout)

    def test_the_profile_is_still_the_one_of_the_plan_apart_from_the_new_roles(self):
        p = self.profile
        self.assertEqual((p.name, p.manifest_path, p.commit_tag, p.errno_suffix, p.capture, p.capture_seconds),
                         ('crux-mgmt-bench-rt1060-evkb-b', 'crux-pilot', False, None, 'raw', 300))


if __name__ == '__main__':
    unittest.main()
