"""Image-profile-driven UART capture in ``program`` mode: where it opens, that it never weakens a gate, never restarts.

Every probe command, the UART and the host are fakes. Nothing here runs on a board or authorises a board step.
The M5 profile path is covered twice: the unmodified tests of ``test_m5_program.py`` (no capture, no change), and
the tests below that compare the commands, checks and results of a profile without capture with one that has it.
"""
import inspect
import json
import sys
import time
import unittest
from dataclasses import fields
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent / 'target'))
import test_m5_program as base
import prepare_m5_network as prepare
import program_m5_image as program
import uart_raw_capture as raw
from capture_m3 import HardwareStop
from image_profile import ImageProfile

GOOD, OK, MISMATCH = base.GOOD, base.OK, base.MISMATCH


class FakeCapture:
    """Records what the driver asks of a capture; the real one is tested in test_m5_raw_capture.py."""

    def __init__(self, case, directory, window_error=None, abort_error=None):
        self.case, self.directory, self.window_error, self.abort_error = case, directory, window_error, abort_error
        self.calls, self.seen_done = [], None

    def window(self):
        self.case.events.append('window')
        self.calls.append('window')
        rows = json.loads((self.case.directory / 'operations.json').read_text())
        self.seen_done = any(r['step'] == '03-program-m5' and r['state'] == 'done' for r in rows)
        if self.window_error:
            raise self.window_error

    def abort(self, reason):
        self.case.events.append('abort')
        self.calls.append(('abort', reason))
        if self.abort_error:
            raise self.abort_error

    def summary(self):
        return dict(raw='uart-rx.bin', window='window')


class FakeSpec:
    def __init__(self, case, seconds=180, open_error=None, **capture_options):
        self.case, self.seconds, self.open_error, self.options = case, seconds, open_error, capture_options
        self.opened, self.capture = [], None

    def open(self, port, directory):
        self.case.events.append('capture-open')
        self.opened.append(dict(port=port, directory=directory, commands=len(self.case.commands), checks=list(self.case.checks)))
        if self.open_error:
            raise self.open_error
        Path(directory).mkdir(parents=True)
        self.capture = FakeCapture(self.case, directory, **self.options)
        return self.capture


class Case(base.Case):
    def preconditions(self, directory, label):
        self.events.append('check ' + label)
        super().preconditions(directory, label)

    def reverify(self):
        self.reverified.append(1)
        self.events.append('reverify')

    def driver(self, mode='program', **options):
        options.setdefault('reverify', self.reverify)
        return super().driver(mode, **options)

    def spec(self, **options):
        self.fake = FakeSpec(self, **options)
        return self.fake

    def program(self, spec=None, responses=None, **options):
        self.responses = list(responses or [GOOD, OK, OK])
        with self.driver(capture=spec, **options) as driver:
            return driver.program(self.m5, self.entry)

    def states(self):
        return [(r['step'], r['state']) for r in json.loads((self.directory / 'operations.json').read_text())]


class Order(Case):
    def test_the_capture_opens_after_the_load_step_checks_and_before_its_one_command(self):
        spec = self.spec()
        self.program(spec)
        load_check = self.events.index('check 03-program-m5')
        opened = self.events.index('capture-open')
        probes = [i for i, event in enumerate(self.events) if event == 'probe']
        self.assertGreater(opened, load_check)
        self.assertGreater(opened, max(i for i, e in enumerate(self.events) if e == 'reverify'))   # after the manifest re-verification
        self.assertEqual(probes[-1], opened + 1)                           # the very next thing is the load, verify, reset and go command
        self.assertEqual(self.events.count('capture-open'), 1)
        self.assertEqual(spec.opened[0]['checks'], ['01-identity-only', '02-entry-image', '03-program-m5'])
        self.assertEqual(spec.opened[0]['commands'], 2)                    # only the identity probe and the entry verify had run
        self.assertEqual(self.events.index('window'), probes[-1] + 1)

    def test_it_gets_the_port_the_driver_owns_and_a_directory_below_the_mode_directory(self):
        spec = self.spec()
        self.program(spec)
        self.assertIs(spec.opened[0]['port'], self.port)
        self.assertEqual(spec.opened[0]['directory'], self.directory / 'capture')

    def test_one_start_only_and_the_same_commands_as_without_a_capture(self):
        self.program(self.spec())
        with_capture = [c.replace(str(self.root), '') for c in self.commands]
        self.setUp()
        self.program(None)
        self.assertEqual(with_capture, [c.replace(str(self.root), '') for c in self.commands])
        self.assertEqual(len([c for c in with_capture if 'loadfile' in c]), 1)
        self.assertEqual(len([c for c in with_capture if 'g\n' in c.replace('IsHalted', '')]), 1)
        self.assertEqual(self.checks, ['01-identity-only', '02-entry-image', '03-program-m5'])

    def test_the_done_marker_is_written_before_the_window_starts(self):
        spec = self.spec()
        self.program(spec)
        self.assertTrue(spec.capture.seen_done)
        self.assertEqual([s for s in self.states() if s[0] == '03-program-m5'],
                         [('03-program-m5', 'checks'), ('03-program-m5', 'capture-open'), ('03-program-m5', 'done'),
                          ('03-program-m5', 'capture-closed')])

    def test_the_result_keeps_the_status_and_adds_the_capture_summary(self):
        result = self.program(self.spec())
        self.assertEqual(result, dict(status='M5_PROGRAMMED_VERIFIED_STARTED', entry_image_verified=True,
                                      capture=dict(raw='uart-rx.bin', window='window')))


class Refusals(Case):
    """Every gate that can refuse the load step does so before a capture exists."""

    def assertNoCapture(self, spec):
        self.assertNotIn('capture-open', self.events)
        self.assertEqual(spec.opened, [])
        self.assertFalse((self.directory / 'capture').exists())

    def test_a_failing_guard_or_ownership_check_at_the_load_step(self):
        spec = self.spec()
        original = self.preconditions
        def guard(directory, label):
            original(directory, label)
            if label.endswith('program-m5'):
                raise HardwareStop('Current mount protection unverified')
        self.preconditions = guard
        with self.assertRaisesRegex(HardwareStop, 'mount protection'):
            self.program(spec)
        self.assertNoCapture(spec)
        self.assertEqual(len(self.commands), 2)

    def test_series_budget_before_and_after_the_checks_of_the_load_step(self):
        for series, pattern in ((200, 'Insufficient series budget'), (130, 'exhausted by the guard')):
            with self.subTest(series=series):
                self.setUp()
                spec = self.spec()
                seconds = {'02-entry-image': 100, '03-program-m5': 0} if series == 200 else {'03-program-m5': 90}
                def timed(directory, label, seconds=seconds):
                    Case.preconditions(self, directory, label)
                    self.now += seconds.get(label, 0)
                self.preconditions = timed
                with self.assertRaisesRegex(HardwareStop, pattern):
                    self.program(spec, series_seconds=series)
                self.assertNoCapture(spec)
                self.assertEqual(len(self.commands), 2)                       # the refusal is at the load step, not before

    def test_a_changed_image_or_manifest_never_opens_a_capture(self):
        spec = self.spec()
        self.responses = [GOOD, OK]
        with self.assertRaisesRegex(HardwareStop, 'Image changed since approval'), self.driver(capture=spec) as driver:
            Path(self.m5).write_bytes(b'tampered')
            driver.program(self.m5, self.entry)
        self.assertNoCapture(spec)
        self.setUp()
        spec = self.spec()
        calls = []
        def broken():
            calls.append(1)
            if len(calls) == 2:
                raise HardwareStop('Recovery artefact changed')
        with self.assertRaisesRegex(HardwareStop, 'Recovery artefact changed'):
            self.program(spec, reverify=broken)
        self.assertNoCapture(spec)
        self.assertFalse(any('loadfile' in c for c in self.commands))

    def test_a_wrong_identity_or_entry_image_never_opens_a_capture(self):
        for responses in ([GOOD.replace('602003106', '1')], [GOOD, MISMATCH], [GOOD, GOOD]):
            with self.subTest(responses=len(responses)):
                self.setUp()
                spec = self.spec()
                with self.assertRaises(HardwareStop):
                    self.program(spec, responses=responses)
                self.assertNoCapture(spec)
                self.assertFalse(any('loadfile' in c for c in self.commands))

    def test_unsafe_paths_never_open_a_capture(self):
        spec = self.spec()
        with self.assertRaisesRegex(HardwareStop, 'Unsafe image path'), self.driver(capture=spec) as driver:
            driver.program('/b/"m5.bin', self.entry)
        self.assertNoCapture(spec)
        self.assertEqual(self.commands, [])

    def test_a_window_that_does_not_fit_the_series_budget_is_refused_before_the_load(self):
        spec = self.spec(seconds=300)
        with self.assertRaisesRegex(HardwareStop, 'Capture window does not fit the series budget'):
            self.program(spec, series_seconds=320)
        self.assertNoCapture(spec)
        self.assertEqual(len(self.commands), 2)
        self.assertFalse(any('loadfile' in c for c in self.commands))
        self.setUp()
        spec = self.spec(seconds=300)
        self.program(spec, series_seconds=335)                             # 30 s command limit + 300 s window just fit
        self.assertEqual(len(spec.opened), 1)

    def test_only_program_mode_may_carry_a_capture(self):
        for mode in ('identify', 'restore'):
            with self.subTest(mode=mode), self.assertRaisesRegex(HardwareStop, 'only for the program mode'):
                self.driver(mode, capture=self.spec())
            self.assertFalse(self.directory.exists())
            self.setUp()


class Failures(Case):
    def test_a_capture_that_cannot_open_stops_before_the_load_and_changes_nothing(self):
        spec = self.spec(open_error=OSError('no space left'))
        with self.assertRaisesRegex(HardwareStop, 'UART capture could not be opened; no probe command runs') as caught:
            self.program(spec)
        self.assertIsInstance(caught.exception.__cause__, OSError)
        self.assertEqual(len(self.commands), 2)
        self.assertFalse(any('loadfile' in c for c in self.commands))
        self.assertTrue(self.port.closed)
        failure = json.loads((self.directory / 'failure.json').read_text())
        self.assertEqual((failure['mode'], failure['automatic_retry']), ('program', False))
        self.assertNotIn(('03-program-m5', 'done'), self.states())

    def test_a_failing_start_command_aborts_the_capture_keeps_the_error_and_never_retries(self):
        spec = self.spec(abort_error=HardwareStop('UART capture reader failed: boom'))
        with self.assertRaisesRegex(HardwareStop, 'Probe timeout') as caught:
            self.program(spec, responses=[GOOD, OK, HardwareStop('Probe timeout; state uncertain; no further hardware changes')])
        self.assertEqual(len(self.commands), 3)                           # nothing ran after the failed command
        self.assertEqual(spec.capture.calls, [('abort', 'start command failed')])
        self.assertIn('boom', caught.exception.capture_failure)
        self.assertTrue(self.port.closed)

    def test_missing_verification_aborts_the_capture_instead_of_waiting_the_window(self):
        spec = self.spec()
        with self.assertRaisesRegex(HardwareStop, 'Complete M5 image verification absent'):
            self.program(spec, responses=[GOOD, OK, GOOD])
        self.assertEqual(spec.capture.calls, [('abort', 'complete image verification absent')])
        self.assertEqual(len(self.commands), 3)

    def test_a_capture_that_fails_during_the_window_is_a_stop_and_never_a_second_start(self):
        spec = self.spec(window_error=HardwareStop('UART capture reader failed: OSError(device disconnected)'))
        with self.assertRaisesRegex(HardwareStop, 'UART capture reader failed'):
            self.program(spec)
        self.assertEqual(len(self.commands), 3)
        self.assertEqual(len([c for c in self.commands if 'loadfile' in c]), 1)
        self.assertEqual(len([c for c in self.commands if c.startswith('loadfile')]), 1)
        self.assertEqual(spec.capture.calls, ['window'])                  # not aborted, not repeated
        self.assertNotIn('result', [p.name for p in self.directory.iterdir()])
        failure = json.loads((self.directory / 'failure.json').read_text())
        self.assertEqual((failure['steps'], failure['automatic_retry']), (3, False))
        self.assertIn(('03-program-m5', 'done'), self.states())            # the start itself is on record: the image did start
        self.assertTrue(self.port.closed)

    def test_an_interrupt_in_the_window_is_not_swallowed(self):
        spec = self.spec(window_error=KeyboardInterrupt())
        with self.assertRaises(KeyboardInterrupt):
            self.program(spec)
        self.assertTrue(self.port.closed)
        self.assertEqual(len(self.commands), 3)


class Ports(Case):
    """The real capture against a fake port: it reads what the driver owns and is gone before the driver closes it."""

    class Port(base.Port):
        def __init__(self, case):
            super().__init__()
            self.case, self.reads = case, 0

        def read(self, size):
            if self.closed:
                raise AssertionError('read after the driver closed the port')
            self.reads += 1
            time.sleep(.002)
            return b'S2 log line\r\npartial' if self.reads == 5 else b''

        def close(self):
            self.case.events.append('port-close')
            super().close()

    def test_real_capture_inside_the_driver(self):
        self.port = self.Port(self)
        spec = raw.CaptureSpec(1)
        result = self.program(spec)
        self.assertEqual(self.events[-1], 'port-close')
        self.assertEqual((self.directory / 'capture' / 'uart-rx.bin').read_bytes(), b'S2 log line\r\npartial')
        reads = self.port.reads
        time.sleep(.05)
        self.assertEqual(self.port.reads, reads)                          # no reader outlives the mode
        self.assertTrue((self.directory / '03-program-m5.log').exists())  # the Commander log and the capture do not collide
        self.assertEqual(result['capture']['final_line'], 'cut')
        self.assertEqual(result['status'], 'M5_PROGRAMMED_VERIFIED_STARTED')
        analysis = json.loads((self.directory / 'capture' / 'analysis.json').read_text())
        self.assertEqual(analysis['final_line']['state'], 'cut')

    def test_the_port_is_acquired_once_before_the_capture_and_released_once_after_it(self):
        self.port = self.Port(self)
        self.program(raw.CaptureSpec(1))
        self.assertEqual(self.events.count('acquire'), 1)
        self.assertEqual(self.events.count('port-close'), 1)
        self.assertEqual(self.events[0], 'acquire')
        self.assertTrue((self.directory / 'capture' / 'analysis.json').exists())          # closed only after the capture was complete

    def test_a_missing_port_or_a_port_that_fails_its_first_read_stops_before_the_load(self):
        class Dead(self.Port):
            def read(self, size):
                raise OSError('device gone')
        for port, cause in ((None, 'UART port'), (Dead(self), 'UART capture reader failed')):
            with self.subTest(cause=cause):
                self.setUp()
                self.port = port
                with self.assertRaises(HardwareStop):
                    self.program(raw.CaptureSpec(1))
                self.assertEqual(len(self.commands), 2)
                self.assertFalse(any('loadfile' in c for c in self.commands))

    def test_a_port_that_dies_mid_window_is_reported_without_a_second_command(self):
        class Dying(self.Port):
            def read(self, size):
                self.reads += 1
                time.sleep(.002)
                if self.reads > 40:                     # well after the capture opened
                    raise OSError('device disconnected')
                return b''
        self.port = Dying(self)
        with self.assertRaisesRegex(HardwareStop, 'UART capture reader failed'):
            self.program(raw.CaptureSpec(1))
        self.assertEqual(len(self.commands), 3)
        self.assertIn('device disconnected', json.loads((self.directory / 'capture' / 'analysis.json').read_text())['reader_failure'])


class NoCaptureIsUnchanged(Case):
    def test_without_a_capture_the_flow_result_and_records_are_what_they_were(self):
        result = self.program(None)
        self.assertEqual(result, dict(status='M5_PROGRAMMED_VERIFIED_STARTED', entry_image_verified=True))
        self.assertEqual(sorted(p.name for p in self.directory.iterdir() if p.is_dir()), [])
        self.assertEqual({s for _, s in self.states()}, {'checks', 'done'})

    def test_public_signatures_only_grew_by_optional_arguments(self):
        parameters = inspect.signature(program.Driver.__init__).parameters
        self.assertEqual(parameters['capture'].default, None)
        self.assertEqual(inspect.signature(program.Driver.run).parameters['before'].default, None)
        self.assertEqual(list(inspect.signature(program.Driver.program).parameters), ['self', 'm5_binary', 'entry_binary'])


S2 = prepare.profile_named(prepare.S2_NAME)
M5 = prepare.profile_named(prepare.M5_NAME)


def synthetic_profile(**changes):
    values = dict(name='synthetic-raw', manifest_path='synthetic', pinned={}, pinned_paths={},
                  check_image=lambda binary, elf, config: {}, commit_tag=False, capture='raw',
                  capture_seconds=raw.MAX_CAPTURE_SECONDS)
    values.update(changes)
    return ImageProfile(**values)


class Profiles(Case):
    def test_m5_has_no_capture_and_s2_has_a_raw_capture_with_a_bounded_window(self):
        self.assertEqual((M5.capture, M5.capture_seconds), ('none', 0))
        self.assertEqual((S2.capture, S2.capture_seconds), ('raw', 180))
        self.assertIsNone(program.capture_spec(M5, 'program'))
        spec = program.capture_spec(S2, 'program')
        self.assertIsInstance(spec, raw.CaptureSpec)
        self.assertEqual(spec.seconds, 180)

    def test_only_the_program_mode_gets_a_capture_from_a_profile(self):
        for mode in ('identify', 'restore'):
            self.assertIsNone(program.capture_spec(S2, mode))

    def test_capture_settings_are_validated(self):
        for changes in (dict(capture='live'), dict(capture='raw', capture_seconds=0), dict(capture='raw', capture_seconds=301),
                        dict(capture='raw', capture_seconds=1.5), dict(capture='raw', capture_seconds=True),
                        dict(capture='none', capture_seconds=30), dict(capture='raw', capture_seconds=-5)):
            with self.subTest(changes=changes), self.assertRaisesRegex(ValueError, 'Inconsistent image profile'):
                synthetic_profile(**changes)
        self.assertEqual(synthetic_profile(capture='none', capture_seconds=0).capture, 'none')
        self.assertEqual(synthetic_profile(capture_seconds=1).capture_seconds, 1)

    def test_the_fields_of_a_profile_are_exactly_the_reviewed_ones(self):
        # A new field is a new way for a profile to change behaviour: this list must change in the same reviewed change.
        self.assertEqual({f.name for f in fields(ImageProfile)},
                         {'name', 'manifest_path', 'pinned', 'pinned_paths', 'check_image', 'commit_tag', 'errno_suffix',
                          'tool_scripts', 'modules', 'module_paths', 'capture', 'capture_seconds', 'extra_roles'})
        for gate in ('guard', 'skip_guard', 'owners', 'skip_owners', 'voltage', 'retry', 'entry_verify', 'restore', 'budget', 'unsafe'):
            with self.subTest(gate=gate), self.assertRaises(TypeError):
                synthetic_profile(**{gate: True})

    def test_the_driver_takes_no_profile_and_no_gate_switch(self):
        self.assertEqual(set(inspect.signature(program.Driver.__init__).parameters),
                         {'self', 'plan', 'approval', 'mode', 'directory', 'probe', 'preconditions', 'acquire', 'images', 'reverify',
                          'clock', 'series_seconds', 'capture'})
        self.assertEqual(set(inspect.signature(program.Driver.run).parameters), {'self', 'label', 'commands', 'images', 'before'})

    def test_no_registered_or_extreme_profile_changes_a_single_gate(self):
        """The same fakes, the same flow: commands, checks, re-verifications and the one-start rule are identical."""
        baseline = None
        for profile in (M5, S2, synthetic_profile(), synthetic_profile(capture_seconds=1), synthetic_profile(capture='none', capture_seconds=0)):
            with self.subTest(profile=profile.name, capture=profile.capture):
                self.setUp()
                spec = program.capture_spec(profile, 'program')
                fake = self.spec(seconds=spec.seconds) if spec else None
                self.program(fake, series_seconds=900)
                observed = ([c.replace(str(self.root), '') for c in self.commands], self.checks, self.reverified)
                baseline = baseline or observed
                self.assertEqual(observed, baseline)
                self.assertEqual(len([c for c in self.commands if 'loadfile' in c]), 1)

    def test_a_profile_cannot_reach_the_gates_through_the_capture_spec(self):
        spec = program.capture_spec(S2, 'program')
        self.assertEqual(set(vars(spec)), {'seconds', 'factory'})


if __name__ == '__main__':
    unittest.main()
