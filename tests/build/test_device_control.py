"""Device-control tool against fake transports and scripted tools. No hardware or serial port."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time
import types
import unittest
from unittest import mock

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / 'tools'))

import device_control  # noqa: E402
from device_control import cli  # noqa: E402
from device_control import console as console_module  # noqa: E402
from device_control import lock as lock_module  # noqa: E402
from device_control.errors import (BoardLockHeld, ConsoleError, ConsoleTimeout,  # noqa: E402
                                   DeviceControlError, EvidenceExistsError, GateRefused,
                                   IdentityMismatch, ManifestError, ProfileError, ToolFailed)

BOARD = 'vendor_board@B//qspi'
TOOLS = {'command': ['tool', '--probe', '{probe_serial}', '{image}'], 'timeout_seconds': 5}


def profile_data(locks, **changes):
    data = {
        'board_id': 'bench/board-1', 'board': BOARD, 'mcu': 'MCU1', 'port': '/dev/fake', 'baud': 115200,
        'dtr': False, 'rts': False, 'ready_marker': 'APP READY', 'ready_timeout_seconds': 0.2,
        'write_timeout_seconds': 1, 'lock_directory': str(locks), 'probe_serial': '42',
        'reset': {'command': ['tool', 'reset', '{probe_serial}'], 'timeout_seconds': 5},
        'flash': dict(TOOLS), 'verify': dict(TOOLS, command=['tool', 'verify', '{image}']),
    }
    data.update(changes)
    return {key: value for key, value in data.items() if value is not None}


class FakeTransport:
    def __init__(self, *chunks):
        self.pending = list(chunks)
        self.written = []
        self.open_calls = 0
        self.close_calls = 0

    def open(self):
        self.open_calls += 1

    def close(self):
        self.close_calls += 1

    def read(self, size):
        return self.pending.pop(0) if self.pending else b''

    def write(self, data):
        self.written.append(data)


class FakeRunner:
    def __init__(self, on_call=None, fail=None):
        self.calls = []
        self.on_call = on_call
        self.fail = fail

    def __call__(self, command, log_path, timeout):
        self.calls.append((list(command), Path(log_path).name, timeout))
        if self.fail and self.fail in command:
            raise ToolFailed(f'{self.fail} failed')
        if self.on_call:
            self.on_call(command)


class Workspace(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.handoff = self.root / 'handoff'
        self.handoff.mkdir()
        self.image = self.handoff / 'zephyr.hex'
        self.image.write_bytes(b'image-bytes')
        self.evidence = self.root / 'evidence' / 'run-1'
        self.locks = self.root / 'locks'

    def manifest(self, **changes):
        data = {'schema_version': 1, 'board': BOARD, 'source_revision': 'abc', 'zephyr_revision': 'def',
                'build_id': 'build-1',
                'files': [{'path': 'zephyr.hex',
                           'sha256': hashlib.sha256(self.image.read_bytes()).hexdigest()}]}
        data.update(changes)
        path = self.handoff / 'manifest.json'
        path.write_text(json.dumps({key: value for key, value in data.items() if value is not None}))
        return path

    def profile(self, **changes):
        return device_control.BoardProfile.from_mapping(profile_data(self.locks, **changes))

    def run_for(self, transport=None, runner=None, gates=None, profile=None, manifest=None):
        return device_control.BoardRun(
            profile or self.profile(), manifest or self.manifest(), self.evidence,
            gates=gates or device_control.Gates(), transport=transport or FakeTransport(),
            runner=runner or FakeRunner())

    def result(self):
        return json.loads((self.evidence / 'result.json').read_text())

    def events(self):
        return [json.loads(line) for line in (self.evidence / 'events.jsonl').read_text().splitlines()]


class ProfileTest(Workspace):
    def test_commands_are_rendered_only_from_profile_data(self):
        profile = self.profile()
        self.assertEqual(profile.reset.render(profile), ['tool', 'reset', '42'])
        self.assertEqual(profile.flash.render(profile, image='/h/z.hex'), ['tool', '--probe', '42', '/h/z.hex'])

    def test_board_settings_have_no_defaults(self):
        for field in ('board_id', 'board', 'mcu', 'port', 'baud', 'dtr', 'rts', 'ready_marker',
                      'ready_timeout_seconds', 'write_timeout_seconds', 'lock_directory'):
            with self.subTest(field=field), self.assertRaisesRegex(ProfileError, field):
                self.profile(**{field: None})

    def test_rejects_wrong_types_and_unknown_fields(self):
        for changes in ({'dtr': 'false'}, {'rts': 0}, {'baud': True}, {'baud': 0}, {'board_id': ''},
                        {'ready_marker': ''}, {'ready_timeout_seconds': 0},
                        {'ready_timeout_seconds': float('inf')}, {'write_timeout_seconds': True},
                        {'dtrr': False}, {'identity_pattern': '(unclosed'},
                        {'identity_pattern': 'no group'}, {'identity_pattern': '(a)(b)'},
                        {'reset': {'command': [], 'timeout_seconds': 1}},
                        {'reset': {'command': ['tool'], 'timeout_seconds': 0}},
                        {'reset': {'command': 'tool', 'timeout_seconds': 1}},
                        {'reset': {'command': ['tool'], 'timeout_seconds': 1, 'shell': True}}):
            with self.subTest(changes=changes), self.assertRaises(ProfileError):
                self.profile(**changes)

    def test_lock_directory_must_be_absolute_so_every_run_finds_the_same_lock(self):
        with self.assertRaisesRegex(ProfileError, 'lock_directory'):
            self.profile(lock_directory='relative/locks')

    def test_flash_and_verify_must_name_the_checked_image(self):
        for section in ('flash', 'verify'):
            with self.subTest(section=section), self.assertRaisesRegex(ProfileError, 'image'):
                self.profile(**{section: {'command': ['tool', 'fixed.hex'], 'timeout_seconds': 1}})

    def test_reset_cannot_name_an_image_or_an_unknown_placeholder(self):
        for token in ('{image}', '{unknown}', '{probe}'):
            with self.subTest(token=token), self.assertRaisesRegex(ProfileError, 'placeholder'):
                self.profile(reset={'command': ['tool', token], 'timeout_seconds': 1})

    def test_placeholder_without_a_value_is_rejected_when_loading(self):
        with self.assertRaisesRegex(ProfileError, 'probe_serial'):
            self.profile(probe_serial=None)

    def test_visible_erase_and_fuse_arguments_are_rejected(self):
        for section, token in (('flash', '--erase'), ('flash', 'mass_ERASE'), ('verify', 'efuse'),
                               ('reset', 'fuse-write')):
            command = ['tool', token] + (['{image}'] if section != 'reset' else [])
            with self.subTest(section=section, token=token), self.assertRaisesRegex(ProfileError, 'erase or fuse'):
                self.profile(**{section: {'command': command, 'timeout_seconds': 1}})

    def test_erase_or_fuse_cannot_arrive_through_a_placeholder_value(self):
        for field, value in (('mcu', '--mass-erase'), ('probe_serial', 'x-FUSE'), ('board', 'erase'),
                             ('port', 'efuse')):
            command = {'command': ['tool', '{%s}' % field], 'timeout_seconds': 1}
            with self.subTest(field=field), self.assertRaisesRegex(ProfileError, 'erase or fuse'):
                self.profile(**{field: value, 'reset': command})
        self.assertEqual(self.profile(mcu='erase-capable').mcu, 'erase-capable')

    def test_erase_or_fuse_cannot_be_assembled_from_literal_text_and_values(self):
        for section, token, values in (('reset', 'er{mcu}', {'mcu': 'ase'}),
                                       ('reset', '{mcu}{port}', {'mcu': 'er', 'port': 'ase'}),
                                       ('reset', 'FU{probe_serial}', {'probe_serial': 'se'}),
                                       ('verify', '{board}se', {'board': 'fu'}),
                                       ('flash', 'er{mcu}', {'mcu': 'ase'})):
            command = ['tool', token] + (['{image}'] if section != 'reset' else [])
            with self.subTest(section=section, token=token), self.assertRaisesRegex(ProfileError, 'erase or fuse'):
                self.profile(**values, **{section: {'command': command, 'timeout_seconds': 1}})

    def test_an_image_placeholder_cannot_hide_a_visible_erase(self):
        with self.assertRaisesRegex(ProfileError, 'erase or fuse'):
            self.profile(flash={'command': ['tool', 'er{image}ase'], 'timeout_seconds': 1})

    def test_from_file_reports_unreadable_json_as_a_profile_error(self):
        path = self.root / 'bad.json'
        path.write_text('{')
        with self.assertRaises(ProfileError):
            device_control.BoardProfile.from_file(path)
        with self.assertRaises(ProfileError):
            device_control.BoardProfile.from_file(self.root / 'absent.json')


class ManifestTest(Workspace):
    def load(self, **changes):
        return device_control.Manifest.load(self.manifest(**changes))

    def test_every_listed_file_is_hashed(self):
        other = self.handoff / 'other.bin'
        other.write_bytes(b'other')
        good = {'path': 'zephyr.hex', 'sha256': hashlib.sha256(b'image-bytes').hexdigest()}
        manifest = self.load(files=[good, {'path': 'other.bin', 'sha256': '0' * 64}])
        with self.assertRaisesRegex(ManifestError, 'other.bin'):
            manifest.verify(BOARD)

    def test_board_mismatch_is_rejected(self):
        with self.assertRaisesRegex(ManifestError, 'board'):
            self.load(board='other_board').verify(BOARD)

    def test_missing_file_is_rejected(self):
        manifest = self.load()
        self.image.unlink()
        with self.assertRaisesRegex(ManifestError, 'zephyr.hex'):
            manifest.verify(BOARD)

    def test_malformed_manifests_are_rejected(self):
        for changes in ({'schema_version': 2}, {'board': None}, {'source_revision': None},
                        {'zephyr_revision': ''}, {'files': []}, {'files': None},
                        {'files': [{'path': 'zephyr.hex', 'sha256': 'ABC'}]},
                        {'files': [{'path': 'zephyr.hex'}]}):
            with self.subTest(changes=changes), self.assertRaises(ManifestError):
                self.load(**changes)

    def test_paths_cannot_leave_the_handoff_directory(self):
        outside = self.root / 'outside.bin'
        outside.write_bytes(b'x')
        digest = hashlib.sha256(b'x').hexdigest()
        (self.handoff / 'link.bin').symlink_to(outside)
        for name in ('../outside.bin', str(outside), 'a\\..\\b', 'C:/x', 'link.bin', ''):
            with self.subTest(name=name), self.assertRaises(ManifestError):
                self.load(files=[{'path': name, 'sha256': digest}]).verify(BOARD)

    def test_duplicate_paths_are_rejected(self):
        entry = {'path': 'zephyr.hex', 'sha256': hashlib.sha256(b'image-bytes').hexdigest()}
        with self.assertRaisesRegex(ManifestError, 'duplicate'):
            self.load(files=[entry, entry])

    def test_unlisted_artefact_is_rejected(self):
        with self.assertRaisesRegex(ManifestError, 'not listed'):
            self.load().verified_path('absent.hex')


class EvidenceTest(Workspace):
    def test_existing_directory_fails_and_is_left_untouched(self):
        self.evidence.mkdir(parents=True)
        marker = self.evidence / 'earlier.txt'
        marker.write_text('earlier')
        with self.assertRaises(EvidenceExistsError), self.run_for():
            self.fail('entered a run over existing evidence')
        self.assertEqual([path.name for path in self.evidence.iterdir()], ['earlier.txt'])

    def test_keeps_raw_bytes_events_inputs_and_identity(self):
        raw = b'\xff\x00APP READY\r\n\x1b[0m'
        with self.run_for(transport=FakeTransport(raw)) as run:
            run.open_console().read_until('APP READY', 0.2)
            run.console.write(b'PING\r')
        self.assertEqual((self.evidence / 'rx.bin').read_bytes(), raw)
        events = self.events()
        rx = next(event for event in events if event['event'] == 'rx')
        tx = next(event for event in events if event['event'] == 'tx')
        self.assertEqual(bytes.fromhex(rx['hex']), raw)
        self.assertEqual(bytes.fromhex(tx['hex']), b'PING\r')
        for event in events:
            self.assertIn('monotonic', event)
            self.assertIn('utc', event)
        inputs = json.loads((self.evidence / 'inputs.json').read_text())
        self.assertEqual(inputs['profile']['board_id'], 'bench/board-1')
        identity = json.loads((self.evidence / 'identity.json').read_text())
        self.assertEqual(identity['build_id'], 'build-1')
        self.assertEqual(identity['files']['zephyr.hex'], hashlib.sha256(b'image-bytes').hexdigest())
        self.assertEqual((self.evidence / 'manifest.json').read_bytes(),
                         (self.handoff / 'manifest.json').read_bytes())
        self.assertEqual(self.result()['status'], 'passed')

    def test_a_failure_in_the_test_body_is_retained(self):
        with self.assertRaisesRegex(ValueError, 'oracle'):
            with self.run_for(transport=FakeTransport(b'partial')) as run:
                try:
                    run.open_console().read_until('never', 0.01)
                except ConsoleTimeout:
                    raise ValueError('oracle rejected the output')
        self.assertEqual(self.result()['status'], 'failed')
        self.assertIn('oracle', self.result()['error'])
        self.assertEqual((self.evidence / 'rx.bin').read_bytes(), b'partial')

    def test_manifest_failure_is_retained_and_stops_before_the_lock(self):
        manifest = self.manifest(board='other_board')
        lock_path = lock_module.lock_path(self.locks, 'bench/board-1')
        with self.assertRaises(ManifestError), self.run_for(manifest=manifest):
            self.fail('entered a run with a mismatched manifest')
        self.assertEqual(self.result()['status'], 'failed')
        self.assertFalse(lock_path.exists())
        self.assertEqual((self.evidence / 'manifest.json').read_bytes(), manifest.read_bytes())

    def test_hash_failure_retains_expected_and_observed_identity(self):
        manifest = self.manifest(files=[{'path': 'zephyr.hex', 'sha256': '0' * 64}])
        with self.assertRaises(ManifestError), self.run_for(manifest=manifest):
            self.fail('entered a run with a mismatched hash')
        identity = json.loads((self.evidence / 'identity.json').read_text())
        self.assertEqual(identity['files']['zephyr.hex'], '0' * 64)
        self.assertEqual(identity['observed_sha256']['zephyr.hex'],
                         hashlib.sha256(b'image-bytes').hexdigest())

    def test_missing_file_is_recorded_as_not_observed(self):
        manifest = self.manifest()
        self.image.unlink()
        with self.assertRaises(ManifestError), self.run_for(manifest=manifest):
            self.fail('entered a run with a missing file')
        identity = json.loads((self.evidence / 'identity.json').read_text())
        self.assertIsNone(identity['observed_sha256']['zephyr.hex'])

    def test_malformed_manifest_is_copied_for_the_record(self):
        path = self.handoff / 'manifest.json'
        path.write_text('{not json')
        with self.assertRaises(ManifestError), self.run_for(manifest=path):
            self.fail('entered a run with a malformed manifest')
        self.assertEqual((self.evidence / 'manifest.json').read_text(), '{not json')
        self.assertEqual(self.result()['status'], 'failed')

    def test_malformed_manifest_leaves_an_identity_record(self):
        for name, content in (('not json', '{not json'), ('wrong schema', '{"schema_version": 2}')):
            with self.subTest(name=name):
                self.evidence = self.root / 'evidence' / name
                path = self.handoff / 'manifest.json'
                path.write_text(content)
                with self.assertRaises(ManifestError), self.run_for(manifest=path):
                    self.fail('entered a run with a malformed manifest')
                identity = json.loads((self.evidence / 'identity.json').read_text())
                self.assertEqual(identity['manifest_sha256'], hashlib.sha256(content.encode()).hexdigest())
                self.assertIsNone(identity['board'])
                self.assertEqual((identity['files'], identity['observed_sha256']), ({}, {}))

    def test_absent_manifest_leaves_an_identity_record(self):
        with self.assertRaises(ManifestError), self.run_for(manifest=self.handoff / 'absent.json'):
            self.fail('entered a run without a manifest')
        identity = json.loads((self.evidence / 'identity.json').read_text())
        self.assertIsNone(identity['manifest_sha256'])
        self.assertEqual(self.result()['status'], 'failed')

    def test_an_interrupt_while_closing_the_console_still_releases_the_lock_and_records_the_result(self):
        class Interrupted(FakeTransport):
            def close(self):
                raise KeyboardInterrupt()
        with self.assertRaises(KeyboardInterrupt):
            with self.run_for(transport=Interrupted()) as run:
                run.open_console()
        self.assertEqual(self.result()['status'], 'failed')
        self.assertEqual(self.result()['error_type'], 'KeyboardInterrupt')
        with device_control.BoardLock(self.locks, 'bench/board-1'):
            pass

    def test_an_interrupt_while_releasing_the_lock_still_records_the_result(self):
        with mock.patch.object(lock_module, '_release', side_effect=KeyboardInterrupt()):
            with self.assertRaises(KeyboardInterrupt), self.run_for():
                pass
        self.assertEqual(self.result()['error_type'], 'KeyboardInterrupt')
        with device_control.BoardLock(self.locks, 'bench/board-1'):
            pass

    def test_an_interrupt_during_cleanup_is_not_lost_to_a_body_failure(self):
        class Interrupted(FakeTransport):
            def close(self):
                raise KeyboardInterrupt()
        with self.assertRaises(KeyboardInterrupt):
            with self.run_for(transport=Interrupted()) as run:
                run.open_console()
                raise ValueError('oracle rejected the output')
        self.assertIn('oracle', self.result()['error'])
        self.assertIn('KeyboardInterrupt', self.result()['cleanup_error'])

    def test_evidence_files_are_closed_when_writing_the_result_fails(self):
        run = self.run_for()
        with self.assertRaisesRegex(OSError, 'disk full'):
            with run:
                run.evidence.write_json = mock.Mock(side_effect=OSError('disk full'))
        self.assertTrue(run.evidence._events.closed)
        with device_control.BoardLock(self.locks, 'bench/board-1'):
            pass

    def test_a_cleanup_failure_is_not_recorded_as_passed_and_still_releases_the_lock(self):
        class BadClose(FakeTransport):
            def close(self):
                raise OSError('port vanished')
        with self.assertRaisesRegex(OSError, 'port vanished'):
            with self.run_for(transport=BadClose()) as run:
                run.open_console()
        self.assertEqual(self.result()['status'], 'failed')
        self.assertIn('port vanished', self.result()['error'])
        with device_control.BoardLock(self.locks, 'bench/board-1'):
            pass

    def test_a_body_failure_is_recorded_even_if_cleanup_also_fails(self):
        class BadClose(FakeTransport):
            def close(self):
                raise OSError('port vanished')
        with self.assertRaisesRegex(ValueError, 'oracle'):
            with self.run_for(transport=BadClose()) as run:
                run.open_console()
                raise ValueError('oracle rejected the output')
        self.assertIn('oracle', self.result()['error'])


class LockTest(Workspace):
    HOLD = ('import sys, time\nsys.path.insert(0, sys.argv[1])\nfrom device_control.lock import BoardLock\n'
            'with BoardLock(sys.argv[2], sys.argv[3]):\n    print("held", flush=True)\n    time.sleep(120)\n')

    def holder(self, board='bench/board-1', script=None):
        process = subprocess.Popen([sys.executable, '-c', script or self.HOLD, str(REPO / 'tools'),
                                    str(self.locks), board], stdout=subprocess.PIPE, text=True)
        self.addCleanup(process.stdout.close)
        self.addCleanup(process.wait)
        self.addCleanup(process.kill)
        self.assertEqual(process.stdout.readline().strip(), 'held')
        return process

    def test_held_lock_fails_at_once_across_processes(self):
        self.holder()
        started = time.monotonic()
        with self.assertRaises(BoardLockHeld), device_control.BoardLock(self.locks, 'bench/board-1'):
            self.fail('took a held lock')
        self.assertLess(time.monotonic() - started, 2)

    def test_held_lock_fails_at_once_in_one_process(self):
        with device_control.BoardLock(self.locks, 'bench/board-1'):
            with self.assertRaises(BoardLockHeld), device_control.BoardLock(self.locks, 'bench/board-1'):
                self.fail('took a held lock')

    def test_released_by_the_os_when_the_holder_is_killed(self):
        process = self.holder()
        process.kill()
        process.wait()
        with device_control.BoardLock(self.locks, 'bench/board-1'):
            pass

    def test_a_tool_started_by_the_holder_does_not_inherit_the_lock(self):
        script = ('import subprocess, sys, time\nsys.path.insert(0, sys.argv[1])\n'
                  'from device_control.lock import BoardLock\n'
                  'with BoardLock(sys.argv[2], sys.argv[3]):\n'
                  '    child = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(120)"])\n'
                  '    print(child.pid, flush=True)\n    print("held", flush=True)\n    time.sleep(120)\n')
        process = subprocess.Popen([sys.executable, '-c', script, str(REPO / 'tools'), str(self.locks), 'b1'],
                                   stdout=subprocess.PIPE, text=True)
        self.addCleanup(process.stdout.close)
        self.addCleanup(process.wait)
        self.addCleanup(process.kill)
        child = int(process.stdout.readline())
        self.addCleanup(os.kill, child, 9)
        self.assertEqual(process.stdout.readline().strip(), 'held')
        process.kill()
        process.wait()
        with device_control.BoardLock(self.locks, 'b1'):
            pass

    def test_board_ids_do_not_contend_and_stay_inside_the_directory(self):
        with device_control.BoardLock(self.locks, 'bench/board-1'), device_control.BoardLock(self.locks, 'b2'):
            pass
        for name in ('../escape', 'a/b', '..'):
            self.assertEqual(lock_module.lock_path(self.locks, name).parent, self.locks)

    def test_clean_release_allows_the_next_run(self):
        with device_control.BoardLock(self.locks, 'b1'):
            pass
        with device_control.BoardLock(self.locks, 'b1'):
            pass

    def test_unexpected_lock_errors_are_not_reported_as_held(self):
        with mock.patch.object(lock_module, '_acquire', side_effect=OSError(5, 'I/O error')):
            with self.assertRaises(OSError) as raised, device_control.BoardLock(self.locks, 'b1'):
                self.fail('took a failed lock')
        self.assertNotIsInstance(raised.exception, BoardLockHeld)

    def test_windows_branch_with_a_fake_msvcrt(self):
        calls = []
        fake = types.SimpleNamespace(LK_NBLCK=2, LK_UNLCK=0, locking=lambda *args: calls.append(args))
        with tempfile.TemporaryFile() as stream, mock.patch.dict(sys.modules, {'msvcrt': fake}):
            lock_module._acquire_windows(stream)
            lock_module._release_windows(stream)
        self.assertEqual([call[1:] for call in calls], [(2, 1), (0, 1)])


class ReadmeTest(Workspace):
    def test_documented_profile_and_manifest_load(self):
        text = (REPO / 'tools/device_control/README.md').read_text()
        profile_text, manifest_text = re.findall(r'```json\n(.*?)```', text, re.DOTALL)
        profile = device_control.BoardProfile.from_mapping(json.loads(profile_text))
        self.assertEqual(profile.flash.render(profile, image='i')[-1], 'i')
        path = self.handoff / 'documented.json'
        path.write_text(manifest_text)
        self.assertEqual(device_control.Manifest.load(path).board, profile.board)


class ImportTest(unittest.TestCase):
    def test_imports_without_any_platform_or_serial_module(self):
        script = ('import sys\nfor name in ("fcntl", "termios", "msvcrt", "serial", "pty", "tty"):\n'
                  '    sys.modules[name] = None\n'
                  f'sys.path.insert(0, {str(REPO / "tools")!r})\n'
                  'import device_control, device_control.cli, device_control.console, device_control.lock\n')
        result = subprocess.run([sys.executable, '-c', script], text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_the_package_has_no_runtime_build_entry(self):
        self.assertFalse((REPO / 'tools/CMakeLists.txt').exists())
        self.assertNotRegex((REPO / 'CMakeLists.txt').read_text(), r'\btools/')
        self.assertNotRegex((REPO / 'zephyr/CMakeLists.txt').read_text(), r'\btools/')


class ConsoleTest(unittest.TestCase):
    def test_returns_raw_bytes_through_the_match_and_keeps_the_rest(self):
        console = console_module.Console(FakeTransport(b'\xffone\r\ntwo\r\nthree'))
        console.open()
        self.assertEqual(console.read_until(rb'one\r\n', 0.1), b'\xffone\r\n')
        self.assertEqual(console.read_until('two', 0.1), b'two')
        self.assertEqual(console.read_until(re.compile(b'three'), 0.1), b'\r\nthree')

    def test_timeout_reports_what_arrived_and_loses_nothing(self):
        console = console_module.Console(FakeTransport(b'partial'))
        console.open()
        with self.assertRaises(ConsoleTimeout) as raised:
            console.read_until('never', 0.01)
        self.assertEqual(raised.exception.received, b'partial')
        self.assertEqual(console.read_until('partial', 0.01), b'partial')

    def test_timeout_is_monotonic_and_bounded_under_constant_output(self):
        class Chatty(FakeTransport):
            def read(self, size):
                return b'x'
        console = console_module.Console(Chatty())
        console.open()
        started = time.monotonic()
        with mock.patch.object(console_module.time, 'time', side_effect=AssertionError('wall clock used')):
            with self.assertRaises(ConsoleTimeout):
                console.read_until('never', 0.05)
        self.assertLess(time.monotonic() - started, 2)

    def test_a_match_on_the_last_read_counts(self):
        console = console_module.Console(FakeTransport(b'done'))
        console.open()
        self.assertEqual(console.read_until('done', 0), b'done')

    def test_write_takes_bytes_and_requires_an_open_console(self):
        transport = FakeTransport()
        console = console_module.Console(transport)
        with self.assertRaises(ConsoleError):
            console.write(b'early')
        with self.assertRaises(ConsoleError):
            console.read_until('x', 0)
        console.open()
        console.write(b'ok')
        with self.assertRaises(TypeError):
            console.write('text')
        self.assertEqual(transport.written, [b'ok'])
        console.close()
        console.close()
        self.assertEqual(transport.close_calls, 1)

    def test_discard_pending_removes_buffered_and_waiting_bytes(self):
        transport = FakeTransport(b'old one', b'old two')
        console = console_module.Console(transport)
        console.open()
        with self.assertRaises(ConsoleTimeout):
            console.read_until('never', 0)
        console.discard_pending()
        self.assertEqual(transport.pending, [])
        with self.assertRaises(ConsoleTimeout) as raised:
            console.read_until('old', 0)
        self.assertEqual(raised.exception.received, b'')


class SerialTransportTest(Workspace):
    def fake_serial(self, order, open_error=None):
        class SerialException(OSError):
            pass

        class Serial:
            def __init__(self):
                object.__setattr__(self, 'values', {})

            def __setattr__(self, name, value):
                order.append(name)
                self.values[name] = value

            def open(self):
                order.append('open')
                if open_error:
                    raise SerialException(open_error)

            def fileno(self):
                return 99

            def close(self):
                order.append('close')

            def read(self, size):
                return b'data'

            def write(self, data):
                return len(data)
        return types.SimpleNamespace(Serial=Serial, SerialException=SerialException)

    def test_profile_settings_are_applied_before_the_port_opens(self):
        order = []
        fake = self.fake_serial(order)
        ioctl = []
        fcntl = types.SimpleNamespace(ioctl=lambda *args: ioctl.append(args))
        termios = types.SimpleNamespace(TIOCEXCL=7)
        with mock.patch.dict(sys.modules, {'serial': fake, 'fcntl': fcntl, 'termios': termios}):
            transport = console_module.SerialTransport(self.profile(dtr=True, rts=False))
            transport.open()
        self.assertEqual(order[-1], 'open')
        self.assertIn('port', order)
        for name in ('dtr', 'rts', 'exclusive', 'baudrate', 'timeout', 'write_timeout'):
            self.assertIn(name, order[:-1])
        values = transport._port.values
        self.assertEqual((values['dtr'], values['rts'], values['exclusive'], values['baudrate']),
                         (True, False, True, 115200))
        self.assertGreater(values['timeout'], 0)
        if os.name == 'posix':
            self.assertEqual(ioctl, [(99, 7)])

    @unittest.skipUnless(os.name == 'posix', 'TIOCEXCL is POSIX only')
    def test_failure_to_deny_other_programs_closes_the_port(self):
        order = []

        def refuse(*args):
            raise OSError('no exclusive use')
        modules = {'serial': self.fake_serial(order), 'fcntl': types.SimpleNamespace(ioctl=refuse),
                   'termios': types.SimpleNamespace(TIOCEXCL=7)}
        with mock.patch.dict(sys.modules, modules), self.assertRaisesRegex(ConsoleError, 'exclusive'):
            console_module.SerialTransport(self.profile()).open()
        self.assertEqual(order[-2:], ['open', 'close'])

    def test_open_failure_and_missing_pyserial_are_console_errors(self):
        fake = self.fake_serial([], open_error='port busy')
        with mock.patch.dict(sys.modules, {'serial': fake}), self.assertRaisesRegex(ConsoleError, 'port busy'):
            console_module.SerialTransport(self.profile()).open()
        with mock.patch.dict(sys.modules, {'serial': None}), self.assertRaisesRegex(ConsoleError, 'pyserial'):
            console_module.SerialTransport(self.profile()).open()

    @unittest.skipUnless(hasattr(os, 'openpty'), 'needs a pseudo-terminal')
    def test_exclusive_open_on_a_disposable_pty_with_real_pyserial(self):
        if importlib.util.find_spec('serial') is None:
            self.skipTest('pyserial is not installed')
        # In its own process: test_m5_runner requires that this one never loads pyserial.
        script = """
import json, os, sys
sys.path.insert(0, sys.argv[1])
from device_control import BoardProfile, Console, SerialTransport
from device_control.errors import ConsoleError
master, slave = os.openpty()
profile = BoardProfile.from_mapping(dict(json.loads(sys.argv[2]), port=os.ttyname(slave)))
transport = SerialTransport(profile)
console = Console(transport)
console.open()
try:
    SerialTransport(profile).open()
except ConsoleError:
    pass
else:
    raise SystemExit('second open succeeded')
os.write(master, b'hello')
assert console.read_until('hello', 2) == b'hello'
console.write(b'out')
assert os.read(master, 3) == b'out'
console.close()
"""
        result = subprocess.run([sys.executable, '-c', script, str(REPO / 'tools'),
                                 json.dumps(profile_data(self.locks))],
                                text=True, capture_output=True, timeout=60)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


class ResetTest(Workspace):
    def booting(self, transport, *output):
        return FakeRunner(on_call=lambda command: transport.pending.extend(output))

    def test_opening_a_run_touches_nothing(self):
        transport, runner = FakeTransport(), FakeRunner()
        with self.run_for(transport, runner, device_control.Gates(allow_reset=True, flash_approval='ref')):
            pass
        self.assertEqual((transport.open_calls, runner.calls), (0, []))

    def test_refused_by_default_before_the_port_or_tool_is_touched(self):
        transport, runner = FakeTransport(), FakeRunner()
        with self.run_for(transport, runner) as run:
            with self.assertRaisesRegex(GateRefused, '--allow-reset'):
                run.reset()
        self.assertEqual((transport.open_calls, runner.calls), (0, []))
        self.assertTrue(any(event['event'] == 'refused' for event in self.events()))

    def test_allowed_reset_runs_the_profile_command_and_waits_for_ready(self):
        transport = FakeTransport()
        runner = self.booting(transport, b'boot\r\nAPP READY\r\n')
        with self.run_for(transport, runner, device_control.Gates(allow_reset=True)) as run:
            run.reset()
        self.assertEqual(runner.calls, [(['tool', 'reset', '42'], 'reset.log', 5)])
        self.assertEqual(transport.open_calls, 1)
        self.assertEqual(transport.close_calls, 1)

    def test_a_stale_ready_marker_does_not_satisfy_the_reset(self):
        transport = FakeTransport(b'APP READY\r\n')
        with self.run_for(transport, FakeRunner(), device_control.Gates(allow_reset=True)) as run:
            with self.assertRaises(ConsoleTimeout):
                run.reset()

    def test_tool_failure_stops_the_reset(self):
        transport = FakeTransport(b'APP READY')
        runner = FakeRunner(fail='reset')
        with self.assertRaises(ToolFailed):
            with self.run_for(transport, runner, device_control.Gates(allow_reset=True)) as run:
                run.reset()
        self.assertEqual(self.result()['status'], 'failed')

    def test_profile_without_a_reset_command_refuses(self):
        runner = FakeRunner()
        with self.run_for(runner=runner, gates=device_control.Gates(allow_reset=True),
                          profile=self.profile(reset=None)) as run:
            with self.assertRaisesRegex(GateRefused, 'reset'):
                run.reset()
        self.assertEqual(runner.calls, [])

    def test_printed_identity_is_compared_with_the_manifest(self):
        profile = self.profile(identity_pattern=r'BUILD=(\S+)')
        for name, output, error in (
                ('before ready', b'BUILD=build-1\r\nAPP READY\r\n', None),
                ('after ready', b'APP READY\r\nBUILD=build-1\r\n', None),
                ('wrong', b'BUILD=other\r\nAPP READY\r\n', IdentityMismatch),
                ('absent', b'APP READY\r\n', ConsoleTimeout)):
            with self.subTest(name=name):
                self.evidence = self.root / 'evidence' / name
                transport = FakeTransport()
                run = self.run_for(transport, self.booting(transport, output),
                                   device_control.Gates(allow_reset=True), profile=profile)
                with run:
                    if error:
                        with self.assertRaises(error):
                            run.reset()
                    else:
                        run.reset()
                        identity = json.loads((self.evidence / 'identity.json').read_text())
                        self.assertEqual(identity['observed_build_id'], 'build-1')

    def test_identity_pattern_needs_a_build_id_in_the_manifest(self):
        with self.assertRaisesRegex(ManifestError, 'build_id'):
            with self.run_for(profile=self.profile(identity_pattern=r'BUILD=(\S+)'),
                              manifest=self.manifest(build_id=None)):
                self.fail('entered a run that cannot compare identity')


class FlashTest(Workspace):
    def allowed(self):
        return device_control.Gates(flash_approval='issue-1#comment-2')

    def test_refused_without_an_approval_reference(self):
        for gates in (device_control.Gates(), device_control.Gates(flash_approval=''),
                      device_control.Gates(flash_approval='   '),
                      device_control.Gates(allow_reset=True)):
            with self.subTest(gates=gates):
                self.evidence = self.root / 'evidence' / str(abs(hash(str(gates))))
                transport, runner = FakeTransport(), FakeRunner()
                with self.run_for(transport, runner, gates) as run:
                    with self.assertRaisesRegex(GateRefused, '--allow-flash'):
                        run.flash('zephyr.hex')
                self.assertEqual((transport.open_calls, runner.calls), (0, []))

    def test_flashes_then_verifies_the_checked_image_and_records_the_approval(self):
        transport, runner = FakeTransport(), FakeRunner()
        with self.run_for(transport, runner, self.allowed()) as run:
            run.flash('zephyr.hex')
        image = str(self.image.resolve())
        self.assertEqual(runner.calls, [(['tool', '--probe', '42', image], 'flash.log', 5),
                                        (['tool', 'verify', image], 'verify.log', 5)])
        self.assertEqual(transport.open_calls, 0)
        inputs = json.loads((self.evidence / 'inputs.json').read_text())
        self.assertEqual(inputs['gates']['flash_approval'], 'issue-1#comment-2')

    def test_verify_failure_fails_the_flash(self):
        with self.assertRaises(ToolFailed):
            with self.run_for(runner=FakeRunner(fail='verify'), gates=self.allowed()) as run:
                run.flash('zephyr.hex')
        self.assertEqual(self.result()['status'], 'failed')

    def test_flash_tool_failure_skips_verify(self):
        runner = FakeRunner(fail='--probe')
        with self.assertRaises(ToolFailed):
            with self.run_for(runner=runner, gates=self.allowed()) as run:
                run.flash('zephyr.hex')
        self.assertEqual(len(runner.calls), 1)

    def test_image_changed_after_the_run_started_is_refused(self):
        runner = FakeRunner()
        with self.assertRaises(ManifestError):
            with self.run_for(runner=runner, gates=self.allowed()) as run:
                self.image.write_bytes(b'tampered')
                run.flash('zephyr.hex')
        self.assertEqual(runner.calls, [])

    def test_hash_or_board_mismatch_never_reaches_the_tool(self):
        runner = FakeRunner()
        bad = [{'path': 'zephyr.hex', 'sha256': '0' * 64}]
        for name, manifest in (('hash', {'files': bad}), ('board', {'board': 'other'})):
            with self.subTest(name=name):
                self.evidence = self.root / 'evidence' / name
                with self.assertRaises(ManifestError):
                    with self.run_for(runner=runner, gates=self.allowed(), manifest=self.manifest(**manifest)):
                        self.fail('entered a run with a bad manifest')
        self.assertEqual(runner.calls, [])

    def test_unlisted_artefact_never_reaches_the_tool(self):
        runner = FakeRunner()
        with self.assertRaises(ManifestError):
            with self.run_for(runner=runner, gates=self.allowed()) as run:
                run.flash('other.hex')
        self.assertEqual(runner.calls, [])

    def test_missing_flash_or_verify_command_refuses(self):
        for section in ('flash', 'verify'):
            with self.subTest(section=section):
                self.evidence = self.root / 'evidence' / section
                runner = FakeRunner()
                with self.run_for(runner=runner, gates=self.allowed(),
                                  profile=self.profile(**{section: None})) as run:
                    with self.assertRaisesRegex(GateRefused, section):
                        run.flash('zephyr.hex')
                self.assertEqual(runner.calls, [])


class RunLifetimeTest(Workspace):
    def attempts(self, run):
        return (run.reset, lambda: run.flash('zephyr.hex'), run.open_console, run.confirm_boot)

    def test_operations_outside_a_run_touch_nothing(self):
        transport, runner = FakeTransport(), FakeRunner()
        gates = device_control.Gates(allow_reset=True, flash_approval='ref')
        unopened = self.run_for(transport, runner, gates)
        for attempt in self.attempts(unopened):
            with self.assertRaisesRegex(DeviceControlError, 'not active'):
                attempt()
        with unopened as run:
            pass
        for attempt in self.attempts(run):
            with self.assertRaisesRegex(DeviceControlError, 'not active'):
                attempt()
        self.assertEqual((transport.open_calls, runner.calls), (0, []))


    def test_the_console_cannot_reopen_the_port_after_the_run_ends(self):
        transport = FakeTransport()
        with self.run_for(transport) as run:
            console = run.open_console()
        self.assertEqual(transport.open_calls, 1)
        with self.assertRaisesRegex(ConsoleError, 'run has ended'):
            console.open()
        self.assertEqual(transport.open_calls, 1)

    def test_the_console_cannot_first_open_the_port_after_the_run_ends(self):
        transport = FakeTransport()
        with self.run_for(transport) as run:
            pass
        with self.assertRaisesRegex(ConsoleError, 'run has ended'):
            run.console.open()
        self.assertEqual(transport.open_calls, 0)

    def test_the_console_stays_closed_when_closing_it_failed(self):
        class BadClose(FakeTransport):
            def close(self):
                raise OSError('port vanished')
        transport = BadClose()
        with self.assertRaises(OSError):
            with self.run_for(transport) as run:
                run.open_console()
        with self.assertRaisesRegex(ConsoleError, 'run has ended'):
            run.console.open()
        self.assertEqual(transport.open_calls, 1)

    def test_the_console_can_reopen_while_the_run_is_active(self):
        transport = FakeTransport()
        with self.run_for(transport) as run:
            run.open_console()
            run.console.close()
            run.console.open()
        self.assertEqual((transport.open_calls, transport.close_calls), (2, 2))


class RunLockTest(Workspace):
    def test_second_run_on_a_board_fails_and_retains_its_refusal(self):
        with self.run_for():
            first = self.evidence
            self.evidence = self.root / 'evidence' / 'run-2'
            with self.assertRaises(BoardLockHeld), self.run_for():
                self.fail('entered a run on a locked board')
            self.assertEqual(self.result()['status'], 'failed')
            self.evidence = first

    def test_lock_is_released_after_a_failure(self):
        with self.assertRaises(ValueError):
            with self.run_for():
                raise ValueError('test failed')
        with device_control.BoardLock(self.locks, 'bench/board-1'):
            pass


class RunToolTest(Workspace):
    def run_tool(self, code, timeout=30):
        device_control.run.run_tool([sys.executable, '-c', code], self.root / 'tool.log', timeout)

    def test_output_goes_to_the_log(self):
        self.run_tool('import sys; print("out"); print("err", file=sys.stderr)')
        self.assertEqual(sorted((self.root / 'tool.log').read_text().split()), ['err', 'out'])

    def test_nonzero_exit_and_missing_tool_fail(self):
        with self.assertRaisesRegex(ToolFailed, 'exit 3'):
            self.run_tool('raise SystemExit(3)')
        with self.assertRaises(ToolFailed):
            device_control.run.run_tool([str(self.root / 'absent-tool')], self.root / 'tool.log', 5)

    def test_timeout_is_bounded_and_stops_the_tool(self):
        started = time.monotonic()
        with self.assertRaisesRegex(ToolFailed, 'timed out'):
            self.run_tool('import time; time.sleep(120)', timeout=0.2)
        self.assertLess(time.monotonic() - started, 20)


    def test_timeout_also_stops_what_the_tool_started(self):
        beat = self.root / 'beat'
        heart = ('import sys, time\nend = time.monotonic() + 60\nwhile time.monotonic() < end:\n'
                 '    open(sys.argv[1], "a").write("x")\n    time.sleep(0.02)\n')
        wrapper = (f'import subprocess, sys, time\nsubprocess.Popen([sys.executable, "-c", {heart!r}, {str(beat)!r}])\n'
                   'time.sleep(120)\n')
        with self.assertRaisesRegex(ToolFailed, 'timed out'):
            self.run_tool(wrapper, timeout=5)
        self.assertTrue(beat.exists(), 'the child did not start before the timeout')
        size = beat.stat().st_size
        time.sleep(0.5)
        self.assertEqual(beat.stat().st_size, size, 'the child kept running after the timeout')

    def test_a_run_keeps_the_real_tool_output_as_evidence(self):
        class Booting(FakeTransport):
            def read(self, size):
                self.reads = getattr(self, 'reads', 0) + 1
                return b'APP READY' if self.reads > 1 else b''
        code = 'print("tool output")'
        profile = self.profile(reset={'command': [sys.executable, '-c', code], 'timeout_seconds': 30})
        run = device_control.BoardRun(profile, self.manifest(), self.evidence,
                                      device_control.Gates(allow_reset=True), transport=Booting())
        with run:
            run.reset()
        self.assertIn('tool output', (self.evidence / 'reset.log').read_text())
        self.assertEqual(self.result()['status'], 'passed')


class CliTest(Workspace):
    def main(self, *operation, transport=None, runner=None, profile=None, evidence=None):
        path = self.root / 'profile.json'
        path.write_text(json.dumps(profile or profile_data(self.locks)))
        return cli.main(['--profile', str(path), '--manifest', str(self.manifest()),
                         '--evidence', str(evidence or self.evidence), *operation],
                        transport=transport or FakeTransport(), runner=runner or FakeRunner())

    def test_reset_is_refused_without_the_flag(self):
        transport, runner = FakeTransport(), FakeRunner()
        with mock.patch('sys.stderr'):
            self.assertEqual(self.main('reset', transport=transport, runner=runner), 1)
        self.assertEqual((transport.open_calls, runner.calls), (0, []))
        self.assertEqual(self.result()['status'], 'failed')
        self.assertEqual(self.result()['error_type'], 'GateRefused')

    def test_reset_runs_with_the_flag(self):
        transport = FakeTransport()
        runner = FakeRunner(on_call=lambda command: transport.pending.append(b'APP READY'))
        self.assertEqual(self.main('reset', '--allow-reset', transport=transport, runner=runner), 0)
        self.assertEqual(len(runner.calls), 1)
        self.assertEqual(self.result()['status'], 'passed')

    def test_flash_is_refused_without_the_flag(self):
        runner = FakeRunner()
        with mock.patch('sys.stderr'):
            self.assertEqual(self.main('flash', 'zephyr.hex', runner=runner), 1)
        self.assertEqual(runner.calls, [])

    def test_allow_flash_needs_a_value(self):
        with mock.patch('sys.stderr'), self.assertRaises(SystemExit):
            self.main('flash', 'zephyr.hex', '--allow-flash')

    def test_flash_runs_with_the_reference_and_does_not_reset(self):
        runner = FakeRunner()
        self.assertEqual(self.main('flash', 'zephyr.hex', '--allow-flash=issue-1', runner=runner), 0)
        self.assertEqual([call[1] for call in runner.calls], ['flash.log', 'verify.log'])

    def test_flags_do_not_cross_operations(self):
        with mock.patch('sys.stderr'), self.assertRaises(SystemExit):
            self.main('reset', '--allow-flash=issue-1')
        with mock.patch('sys.stderr'), self.assertRaises(SystemExit):
            self.main('flash', 'zephyr.hex', '--allow-reset')

    def test_bad_profile_and_existing_evidence_fail_without_a_traceback(self):
        with mock.patch('sys.stderr'):
            self.assertEqual(self.main('reset', profile={'board_id': 'x'}), 1)
        self.evidence.mkdir(parents=True)
        with mock.patch('sys.stderr'):
            self.assertEqual(self.main('reset', '--allow-reset'), 1)

    def test_module_entry_point_refuses_by_default_as_a_real_process(self):
        path = self.root / 'profile.json'
        path.write_text(json.dumps(profile_data(self.locks)))
        result = subprocess.run(
            [sys.executable, '-m', 'device_control', '--profile', str(path), '--manifest', str(self.manifest()),
             '--evidence', str(self.evidence), 'reset'],
            cwd=self.root, env=dict(os.environ, PYTHONPATH=str(REPO / 'tools')), text=True, capture_output=True)
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertIn('--allow-reset', result.stderr)
        self.assertEqual(self.result()['error_type'], 'GateRefused')


class ErrorsTest(unittest.TestCase):
    def test_tool_errors_share_one_base(self):
        for error in (BoardLockHeld, ConsoleError, ConsoleTimeout, EvidenceExistsError, GateRefused,
                      IdentityMismatch, ManifestError, ProfileError, ToolFailed):
            self.assertTrue(issubclass(error, DeviceControlError), error)


if __name__ == '__main__':
    unittest.main(verbosity=2)
