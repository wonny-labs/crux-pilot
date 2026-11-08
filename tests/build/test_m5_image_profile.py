"""A second image profile freezes and verifies with its own identity, and never under another profile's gates.

The hardware safety gates are not part of a profile; these tests only cover what a profile supplies:
the manifest repository, the pinned dependencies, the image checks, the commit tag and the errno evidence.
"""
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

TARGET = Path(__file__).resolve().parents[1] / 'target'
sys.path.insert(0, str(TARGET))
import prepare_m5_network as subject
import program_m5_image as program
from image_profile import ImageProfile

PRODUCT_PINS = {'zephyr': '1' * 40, 'hal_nxp': '2' * 40}
PRODUCT_PATHS = {'zephyr': 'zephyr', 'hal_nxp': 'modules/hal/nxp'}


def product_profile(**changes):
    values = dict(name='product-x-physical', manifest_path='product-x', pinned=PRODUCT_PINS,
                  pinned_paths=PRODUCT_PATHS, check_image=lambda binary, elf, config: {'geometry': 'product'},
                  commit_tag=False, errno_suffix=None)
    values.update(changes)
    return ImageProfile(**values)


class Fixture(unittest.TestCase):
    """Build, inputs and provenance of a product image, in a temporary directory."""

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.build = self.root / 'build'
        folder = self.build / 'zephyr'
        folder.mkdir(parents=True)
        self.source = dict(git_commit='c' * 40, dirty=False, recoverable_from_git=True, snapshot_sha256='d' * 64)
        for name in ('zephyr.elf', 'zephyr.bin', '.config', 'zephyr.map', 'zephyr.dts'):
            (folder / name).write_bytes(b'image ' + name.encode())
        (self.build / 'compile_commands.json').write_text('[]')
        self.inputs = self.root / 'inputs.json'
        self.set_projects(self.projects())
        self.output = self.root / 'manifest.json'
        self.tools = self.root / 'tools'
        self.tools.mkdir()
        (self.tools / 'tool.py').write_text('frozen tool')
        for mocked in (patch.object(subject, 'source_snapshot', return_value=self.source),
                       patch.object(subject, 'prepare_errno', side_effect=self.errno),
                       patch.object(subject, 'verify_errno_map', return_value={'EIO': 5})):
            mocked.start()
            self.addCleanup(mocked.stop)
        self.errno_calls = 0

    def errno(self, build, inputs, output, **kwargs):
        self.errno_calls += 1
        output.mkdir()
        (output / 'errno-map.json').write_text('{}')

    def projects(self, context_path=None):
        projects = [dict(name=k, revision=v, path=PRODUCT_PATHS[k], git_commit=v, dirty=False)
                    for k, v in PRODUCT_PINS.items()]
        if context_path:
            projects.insert(0, dict(name='manifest', path=context_path, revision='HEAD',
                                    git_commit='e' * 40, dirty=False))
        return projects

    def set_projects(self, projects):
        data = dict(source=self.source, west_projects=projects)
        self.inputs.write_text(json.dumps(data))
        folder = self.build / 'zephyr'
        data['artifacts'] = [dict(path=str(p.relative_to(self.root)), size_bytes=p.stat().st_size,
                                  sha256=hashlib.sha256(p.read_bytes()).hexdigest()) for p in folder.iterdir()]
        (self.build / 'provenance.json').write_text(json.dumps(data))

    def prepare(self, profile, **extra):
        return subject.prepare(self.build, self.inputs, self.output, self.root,
                               tool_paths=[self.tools / 'tool.py'], profile=profile, **extra)

    def verify(self, profile=None):
        return subject.verify(self.output, subject.sha(self.output), profile)


class SecondProfile(Fixture):
    def test_a_product_image_freezes_and_verifies_with_its_own_identity(self):
        profile = product_profile()
        record = self.prepare(profile)
        self.assertEqual((record['profile'], record['image']), ('product-x-physical', {'geometry': 'product'}))
        self.assertNotIn('errno', record['files'])
        self.assertEqual(self.errno_calls, 0)
        self.assertEqual(self.verify(profile)['profile'], 'product-x-physical')

    def test_a_manifest_is_refused_under_another_profile(self):
        self.prepare(product_profile())
        with self.assertRaises(ValueError):
            self.verify()                                   # the M5 gates by default
        with self.assertRaises(ValueError):
            self.verify(product_profile(name='product-y-physical'))

    def test_the_m5_profile_still_refuses_a_product_dependency_set(self):
        with self.assertRaises(ValueError):
            subject.frozen_inputs(json.loads(self.inputs.read_text()))

    def test_the_workspace_context_names_the_profiles_own_manifest_path(self):
        profile = product_profile()
        for path, accepted in (('product-x', True), ('crux-pilot', False)):
            with self.subTest(path=path):
                self.set_projects(self.projects(context_path=path))
                data = json.loads(self.inputs.read_text())
                if accepted:
                    subject.frozen_inputs(data, profile)
                else:
                    with self.assertRaises(ValueError):
                        subject.frozen_inputs(data, profile)
        with self.assertRaises(ValueError):                 # and the M5 default never accepts it
            subject.frozen_inputs(dict(json.loads(self.inputs.read_text()),
                                       west_projects=self.projects(context_path='product-x')))

    def test_another_project_may_not_sit_inside_the_profiles_manifest_checkout(self):
        profile = product_profile()
        data = json.loads(self.inputs.read_text())
        other = dict(name='lib', revision='3' * 40, path='product-x/lib', git_commit='3' * 40, dirty=False)
        with self.assertRaises(ValueError):
            subject.frozen_inputs(dict(data, west_projects=data['west_projects'] + [other]), profile)
        sibling = dict(name='crux-pilot', revision='4' * 40, path='crux-pilot', git_commit='4' * 40, dirty=False)
        subject.frozen_inputs(dict(data, west_projects=data['west_projects'] + [sibling]), profile)   # the platform is a project here

    def test_the_commit_tag_is_required_only_when_the_profile_says_so(self):
        tagged = product_profile(name='tagged-physical', commit_tag=True)
        with self.assertRaisesRegex(ValueError, 'Source tag absent'):
            self.prepare(tagged)
        (self.build / 'zephyr' / 'zephyr.bin').write_bytes(b'image ' + self.source['git_commit'][:12].encode())
        self.set_projects(self.projects())
        self.prepare(tagged)
        self.assertEqual(self.verify(tagged)['profile'], 'tagged-physical')

    def test_errno_evidence_is_kept_when_the_profile_names_a_source_file(self):
        profile = product_profile(name='errno-physical', errno_suffix='/transaction.c')
        record = self.prepare(profile)
        self.assertIn('errno', record['files'])
        self.assertEqual(self.errno_calls, 1)
        self.assertEqual(self.verify(profile)['errno_values'], {'EIO': 5})

    def test_source_snapshot_follows_the_source_repo_and_tools_follow_the_tool_repo(self):
        source_repo, tool_repo = self.root, self.root / 'platform'
        (tool_repo / 'tests/target').mkdir(parents=True)
        (tool_repo / 'scripts').mkdir()
        (tool_repo / 'tests/target/driver.py').write_text('driver')
        (tool_repo / 'scripts/record-build-provenance.py').write_text('recorder')
        profile = product_profile(tool_scripts=('record-build-provenance.py',))
        with patch.object(subject, 'source_snapshot', return_value=self.source) as snapshot:
            record = subject.prepare(self.build, self.inputs, self.output, source_repo, profile=profile, tool_repo=tool_repo)
        self.assertEqual({call.args[0] for call in snapshot.call_args_list}, {source_repo})
        self.assertEqual(sorted(Path(t).name for t in record['tools']), ['driver.py', 'record-build-provenance.py'])


class Selection(unittest.TestCase):
    def test_the_m5_profile_is_the_default_and_unknown_names_are_refused(self):
        self.assertEqual(subject.profile_named('m5-network-physical').name, 'm5-network-physical')
        self.assertEqual(subject.profile_named('m5-network-physical').manifest_path, 'crux-pilot')
        with self.assertRaises(ValueError):
            subject.profile_named('product-x-physical')

    def test_the_driver_takes_the_profile_by_registered_name_and_defaults_to_m5(self):
        base = ['program']
        for name in ('plan', 'approval', 'manifest', 'entry-manifest', 'recovery-manifest', 'guard-session', 'evidence'):
            base += ['--' + name, '/x']
        base.append('--execute-hardware')
        self.assertEqual(program.build_parser().parse_args(base).profile, 'm5-network-physical')
        with self.assertRaises(SystemExit):
            program.build_parser().parse_args(base + ['--profile', 'not-registered'])

    def test_a_registered_name_must_build_a_profile_of_that_name(self):
        with patch.dict(subject.PROFILES, {'alias-physical': lambda: product_profile()}):
            with self.assertRaises(ValueError):
                subject.profile_named('alias-physical')     # would verify manifests of 'product-x-physical'
        with patch.dict(subject.PROFILES, {'product-x-physical': product_profile}):
            self.assertEqual(subject.profile_named('product-x-physical').manifest_path, 'product-x')


class DriverWiring(unittest.TestCase):
    def test_the_selected_profile_reaches_every_manifest_verification_of_the_driver(self):
        with patch.dict(subject.PROFILES, {'product-x-physical': product_profile}), \
                patch.object(subject, 'verify', return_value={'verified': True}) as verify:
            profile, verifier = program.bind_profile('product-x-physical')
            self.assertEqual(verifier('/m.json', 'f' * 64), {'verified': True})
        self.assertEqual(profile.name, 'product-x-physical')
        verify.assert_called_once_with('/m.json', 'f' * 64, profile)

    def test_the_default_driver_binding_is_the_m5_profile(self):
        self.assertEqual(program.bind_profile('m5-network-physical')[0].name, 'm5-network-physical')
        with self.assertRaises(ValueError):
            program.bind_profile('product-x-physical')


if __name__ == '__main__':
    unittest.main()
