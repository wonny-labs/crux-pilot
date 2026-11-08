"""The S2 image profile, and the rule for an image built by west module discovery.

M5 builds name their modules explicitly, so a west project that is not pinned is recorded and never part of the
image. An image built by module discovery (S2) takes every active west project as a build module, so each one is
pinned exactly and an unpinned project is refused. The tests also pin that the M5 rules did not change.
"""
import contextlib
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / 'target'))
import prepare_m5_network as subject
import program_m5_image as program
from image_profile import ImageProfile
from test_m5_image_profile import Fixture, product_profile
from test_m5_s2_image import CONFIG, FACTS, synthetic_image

S2_NAME = 's2-controller-physical'
WEST = FACTS['west']
PINS = {name: revision for name, (path, revision) in WEST['pins'].items()}
PIN_PATHS = {name: path for name, (path, revision) in WEST['pins'].items()}
MODULES = {name: revision for name, (path, revision) in WEST['build_modules'].items()}
MODULE_PATHS = {name: path for name, (path, revision) in WEST['build_modules'].items()}


def project(name, revision, path):
    return dict(name=name, revision=revision, path=path, git_commit=revision, dirty=False)


def s2_projects():
    return [project(n, PINS[n], PIN_PATHS[n]) for n in PINS] + [project(n, MODULES[n], MODULE_PATHS[n]) for n in MODULES]


class S2Fixture(Fixture):
    """A freeze input of the S2 workspace: the four pins and the four discovered build modules."""

    def projects(self, context_path=None):
        projects = s2_projects()
        if context_path:
            projects.insert(0, dict(name='manifest', path=context_path, revision='HEAD', git_commit='e' * 40, dirty=False))
        return projects

    def replace_image(self):
        """Put a synthetic image with the real S2 configuration where the freeze reads it."""
        folder = self.build / 'zephyr'
        (self.root / 'image').mkdir()
        binary, elf, config = synthetic_image(self.root / 'image')
        for source, name in ((binary, 'zephyr.bin'), (elf, 'zephyr.elf'), (config, '.config')):
            (folder / name).write_bytes(source.read_bytes())
        self.set_projects(self.projects())


class Registration(unittest.TestCase):
    def test_the_s2_profile_is_registered_under_its_own_name(self):
        self.assertIn(S2_NAME, subject.PROFILES)
        profile = subject.profile_named(S2_NAME)
        self.assertEqual(profile.name, S2_NAME)
        self.assertEqual(profile.manifest_path, WEST['manifest_path'])
        self.assertEqual((dict(profile.pinned), dict(profile.pinned_paths)), (PINS, PIN_PATHS))
        self.assertEqual((dict(profile.modules), dict(profile.module_paths)), (MODULES, MODULE_PATHS))

    def test_the_s2_image_has_no_commit_tag_and_no_errno_evidence(self):
        profile = subject.profile_named(S2_NAME)
        self.assertEqual((profile.commit_tag, profile.errno_suffix), (False, None))

    def test_the_s2_freeze_names_the_recorder_and_no_build_script_of_the_product(self):
        self.assertEqual(subject.profile_named(S2_NAME).tool_scripts, ('record-build-provenance.py',))

    def test_the_m5_profile_is_registered_unchanged_and_has_no_modules(self):
        profile = subject.profile_named('m5-network-physical')
        self.assertEqual((dict(profile.modules), dict(profile.module_paths)), ({}, {}))
        self.assertEqual(set(subject.PROFILES), {S2_NAME, 'm5-network-physical'})
        self.assertEqual((profile.manifest_path, dict(profile.pinned), profile.commit_tag, profile.errno_suffix),
                         ('crux-pilot', subject.PINNED, True, '/transaction.c'))

    def test_the_two_profiles_share_the_four_pins_but_not_their_facts(self):
        s2, m5 = subject.profile_named(S2_NAME), subject.profile_named('m5-network-physical')
        self.assertEqual((dict(s2.pinned), dict(s2.pinned_paths)), (dict(m5.pinned), dict(m5.pinned_paths)))
        self.assertNotEqual(s2.check_image, m5.check_image)

    def test_the_driver_accepts_the_s2_profile_by_name_and_still_defaults_to_m5(self):
        base = ['program', '--execute-hardware']
        for name in ('plan', 'approval', 'manifest', 'entry-manifest', 'recovery-manifest', 'guard-session', 'evidence'):
            base += ['--' + name, '/x']
        self.assertEqual(program.build_parser().parse_args(base).profile, 'm5-network-physical')
        self.assertEqual(program.build_parser().parse_args(base + ['--profile', S2_NAME]).profile, S2_NAME)
        self.assertEqual(program.bind_profile(S2_NAME)[0].name, S2_NAME)


class ProfileInvariants(unittest.TestCase):
    def test_a_module_may_not_share_a_name_or_a_path_with_a_pin_or_the_manifest(self):
        base = dict(name='x-physical', manifest_path='product-x', pinned={'zephyr': '1' * 40}, pinned_paths={'zephyr': 'zephyr'},
                    check_image=lambda b, e, c: {}, commit_tag=False)
        ImageProfile(**base, modules={'lib': '2' * 40}, module_paths={'lib': 'modules/lib'})
        for label, changes in (('same name', dict(modules={'zephyr': '2' * 40}, module_paths={'zephyr': 'modules/z'})),
                               ('same path', dict(modules={'lib': '2' * 40}, module_paths={'lib': 'zephyr'})),
                               ('inside a pin', dict(modules={'lib': '2' * 40}, module_paths={'lib': 'zephyr/lib'})),
                               ('contains a pin', dict(modules={'lib': '2' * 40}, module_paths={'lib': '.'})),
                               ('inside the manifest', dict(modules={'lib': '2' * 40}, module_paths={'lib': 'product-x/lib'})),
                               ('path missing', dict(modules={'lib': '2' * 40}, module_paths={})),
                               ('path without module', dict(modules={}, module_paths={'lib': 'modules/lib'})),
                               ('revision not a commit', dict(modules={'lib': 'v1.0'}, module_paths={'lib': 'modules/lib'})),
                               ('two modules one path', dict(modules={'a': '2' * 40, 'b': '3' * 40},
                                                             module_paths={'a': 'modules/m', 'b': 'modules/m'}))):
            with self.subTest(label), self.assertRaises(ValueError):
                ImageProfile(**base, **changes)

    def test_the_pins_of_every_registered_profile_are_commits_with_a_path(self):
        for name in subject.PROFILES:
            profile = subject.profile_named(name)
            with self.subTest(name):
                self.assertEqual(set(profile.pinned), set(profile.pinned_paths))
                for revision in list(profile.pinned.values()) + list(profile.modules.values()):
                    self.assertRegex(revision, r'^[0-9a-f]{40}$')


class ModuleDiscoveryRule(S2Fixture):
    def frozen(self, projects, profile=None):
        data = json.loads(self.inputs.read_text())
        subject.frozen_inputs(dict(data, west_projects=projects), profile or subject.profile_named(S2_NAME))

    def test_the_exact_workspace_is_accepted_with_or_without_the_manifest_context(self):
        self.frozen(s2_projects())
        self.frozen([dict(name='manifest', path='crux-controller', revision='HEAD', git_commit='e' * 40, dirty=False)] + s2_projects())

    def test_the_manifest_context_must_name_the_product_manifest_repository(self):
        with self.assertRaises(ValueError):
            self.frozen([dict(name='manifest', path='crux-pilot', revision='HEAD', git_commit='e' * 40, dirty=False)] + s2_projects())

    def test_every_pin_and_every_module_is_required_exactly(self):
        for name in list(PINS) + list(MODULES):
            kept = [p for p in s2_projects() if p['name'] != name]
            with self.subTest(missing=name), self.assertRaises(ValueError):
                self.frozen(kept)
            for label, change in (('commit', dict(git_commit='d' * 40)), ('revision', dict(revision='d' * 40)),
                                  ('both', dict(git_commit='d' * 40, revision='d' * 40)), ('dirty', dict(dirty=True)),
                                  ('path', dict(path='elsewhere/' + name)), ('branch name', dict(revision='main'))):
                changed = [dict(p, **change) if p['name'] == name else p for p in s2_projects()]
                with self.subTest(name=name, change=label), self.assertRaises(ValueError):
                    self.frozen(changed)
            with self.subTest(duplicate=name), self.assertRaises(ValueError):
                self.frozen(s2_projects() + [p for p in s2_projects() if p['name'] == name])

    def test_a_project_the_profile_does_not_pin_is_refused_because_discovery_would_build_it(self):
        extra = project('mbedtls', 'a' * 40, 'modules/crypto/mbedtls')
        with self.assertRaisesRegex(ValueError, 'build module'):
            self.frozen(s2_projects() + [extra])

    def test_the_same_extra_project_stays_accepted_for_m5_and_for_a_profile_without_modules(self):
        extra = project('mbedtls', 'a' * 40, 'modules/crypto/mbedtls')
        m5 = subject.profile_named('m5-network-physical')
        base = [project(n, subject.PINNED[n], subject.PINNED_PATHS[n]) for n in subject.PINNED]
        self.frozen(base + [extra], m5)                                  # recorded, never part of the M5 image
        self.frozen(base + [project(n, MODULES[n], MODULE_PATHS[n]) for n in ('picolibc', 'fatfs', 'littlefs')], m5)
        self.frozen(self.projects_of(product_profile()) + [extra], product_profile())

    def projects_of(self, profile):
        return [project(n, profile.pinned[n], profile.pinned_paths[n]) for n in profile.pinned]

    def test_the_s2_modules_are_not_accepted_as_pins_of_another_profile(self):
        with self.assertRaises(ValueError):                              # M5 sees crux-pilot as its own manifest checkout path
            self.frozen(s2_projects(), subject.profile_named('m5-network-physical'))

    def test_an_m5_workspace_is_refused_under_the_s2_profile(self):
        base = [project(n, subject.PINNED[n], subject.PINNED_PATHS[n]) for n in subject.PINNED]
        with self.assertRaises(ValueError):
            self.frozen(base)

    def test_a_module_cannot_be_hidden_in_the_context_record(self):
        with self.assertRaises(ValueError):
            self.frozen(s2_projects() + [dict(name='manifest', path='modules/lib/picolibc', revision='HEAD', git_commit='e' * 40, dirty=False)])


class S2Freeze(S2Fixture):
    def setUp(self):
        super().setUp()
        self.replace_image()
        self.profile = subject.profile_named(S2_NAME)

    def test_the_s2_image_freezes_and_verifies_under_its_own_profile_with_the_real_checks(self):
        record = self.prepare(self.profile)
        self.assertEqual(record['profile'], S2_NAME)
        self.assertNotIn('errno', record['files'])
        self.assertEqual(self.errno_calls, 0)
        self.assertEqual(record['image']['main_stack']['bytes'], 4160)
        self.assertEqual(self.verify(self.profile)['image'], record['image'])

    def test_the_manifest_records_the_firmware_identity_of_the_binary_and_configuration(self):
        record = self.prepare(self.profile)
        self.assertRegex(record['image']['firmware_identity'], r'^[0-9a-f]{64}$')

    def test_the_firmware_identity_matches_the_recorded_one_for_the_recorded_image(self):
        # The recorder's own functions, the real normalised configuration and the recorded image hash.
        self.assertEqual(subject.firmware_identity_of(FACTS['image']['sha256'], (HERE / 'fixtures/s2-controller/s2.config').read_bytes()),
                         FACTS['image']['firmware_identity'])

    def test_the_freeze_is_refused_for_another_profile_and_for_a_changed_pin(self):
        self.prepare(self.profile)
        with self.assertRaises(ValueError):
            self.verify()                                                # the M5 gates by default
        changed = json.loads(self.inputs.read_text())
        changed['west_projects'] = [dict(p, git_commit='d' * 40) if p['name'] == 'picolibc' else p for p in changed['west_projects']]
        self.inputs.write_text(json.dumps(changed))
        with self.assertRaises(ValueError):
            subject.frozen_inputs(json.loads(self.inputs.read_text()), self.profile)

    def test_no_commit_tag_is_required_and_a_wrong_image_is_still_refused(self):
        folder = self.build / 'zephyr'
        (folder / '.config').write_text(CONFIG.replace('CONFIG_MAIN_STACK_SIZE=4096', 'CONFIG_MAIN_STACK_SIZE=2048'))
        self.set_projects(self.projects())
        with self.assertRaises(ValueError):
            self.prepare(self.profile)

    def test_the_frozen_tools_are_the_recorder_and_the_driver_scripts_of_the_tool_repository(self):
        tool_repo = self.root / 'platform'
        (tool_repo / 'tests/target').mkdir(parents=True)
        (tool_repo / 'scripts').mkdir()
        (tool_repo / 'tests/target/driver.py').write_text('driver')
        (tool_repo / 'scripts/record-build-provenance.py').write_text('recorder')
        record = subject.prepare(self.build, self.inputs, self.output, self.root, profile=self.profile, tool_repo=tool_repo)
        self.assertEqual(sorted(Path(t).name for t in record['tools']), ['driver.py', 'record-build-provenance.py'])


class FreezeCommands(unittest.TestCase):
    def test_the_freeze_command_selects_a_registered_profile_and_a_tool_repository(self):
        parser = subject.build_parser()
        base = ['prepare', '--build', 'b', '--inputs', 'i', '--output', 'o', '--repo', 'r']
        self.assertEqual(parser.parse_args(base).profile, 'm5-network-physical')
        self.assertIsNone(parser.parse_args(base).tool_repo)
        args = parser.parse_args(base + ['--profile', S2_NAME, '--tool-repo', 't'])
        self.assertEqual((args.profile, args.tool_repo), (S2_NAME, Path('t')))
        with self.assertRaises(SystemExit), contextlib.redirect_stderr(io.StringIO()):
            parser.parse_args(base + ['--profile', 'not-registered'])
        self.assertEqual(parser.parse_args(['verify', '--manifest', 'm', '--sha256', 'f', '--profile', S2_NAME]).profile, S2_NAME)

    def test_the_freeze_command_passes_the_profile_and_the_tool_repository_on(self):
        argv = ['prepare', '--build', 'b', '--inputs', 'i', '--output', 'o', '--repo', 'r', '--profile', S2_NAME, '--tool-repo', 't']
        with patch.object(subject, 'prepare') as prepare, patch.object(subject, 'sha', return_value='f' * 64), patch('builtins.print'):
            subject.main(argv)
        (_, kwargs) = prepare.call_args
        self.assertEqual((kwargs['profile'].name, kwargs['tool_repo']), (S2_NAME, Path('t')))
        with patch.object(subject, 'verify', return_value=dict(profile=S2_NAME, source=dict(git_commit='c' * 40),
                                                               files=dict(binary='b'))) as verify, \
                patch.object(subject, 'sha', return_value='f' * 64), patch('builtins.print'):
            subject.main(['verify', '--manifest', 'm', '--sha256', 'f' * 64, '--profile', S2_NAME])
        self.assertEqual(verify.call_args.args[2].name, S2_NAME)


if __name__ == '__main__':
    unittest.main()
