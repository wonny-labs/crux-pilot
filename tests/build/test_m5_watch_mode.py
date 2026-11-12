"""The read-only ``watch`` mode (blocker B4, option X of docs/rt1060-bench-freeze-and-capture-design.md, part 2).

``watch`` reads the UART that the driver owns for a window above the 300 s cap of ``program`` and runs no probe
command. Every probe command, the UART, the guard and the host are fakes. Nothing here runs on a board or authorises
a board step. The seam fixture (``fixtures/watch-seam``) is real output of the bench report code on ``native_sim``,
copied unchanged from ``docs/evidence``: host output, not a board capture, because no bench image has run on a board.
Every other input below is synthetic.
"""
import contextlib
import hashlib
import io
import json
import sys
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent / 'target'))
sys.path.append(str(HERE.parent / 'zephyr' / 'management_rt1060_compile'))
import test_m5_program as base
import test_m5_program_capture as cap
import test_m5_raw_capture as rawtest
import bench_image
import check_bench_uart as judge
import image_profile
import m5_mount_guard
import prepare_m5_network as prepare
import program_m5_image as program
import uart_raw_capture as raw
from capture_m3 import HardwareStop

WINDOW = 2400                                   # the owner's decision D2 (2026-11-12), the design's recommendation
SEAM = HERE / 'fixtures' / 'watch-seam'
REPOSITORY = HERE.parents[1]


class Case(cap.Case):
    """The fakes of the program tests. ``preconditions`` also records the guard time that a check asked for."""

    def setUp(self):
        super().setUp()
        self.remaining, self.guard_windows, self.guard_window_error = [], [], None

    def preconditions(self, directory, label, seconds, remaining=None):
        self.remaining.append(remaining)
        super().preconditions(directory, label, seconds)

    def probe(self, commands, output, seconds):
        self.commands.append(commands)
        self.events.append('probe')
        raise AssertionError('the watch mode reached a probe command')

    def guard_window(self, seconds):
        self.events.append('guard-window')
        self.guard_windows.append((seconds, self.now))
        if self.guard_window_error:
            raise self.guard_window_error

    def watch_driver(self, spec='default', **options):
        if spec == 'default':
            spec = self.spec(seconds=WINDOW)
        options.setdefault('series_seconds', program.watch_series_seconds(spec) if spec else 900)
        return self.driver('watch', capture=spec, **options)

    def watch(self, spec='default', **options):
        with self.watch_driver(spec, **options) as driver:
            return driver.watch(self.guard_window)

    def failure(self):
        return json.loads((self.directory / 'failure.json').read_text())


class Flow(Case):
    def test_it_reads_only_after_every_gate_and_never_reaches_a_probe_command(self):
        result = self.watch()
        self.assertEqual(self.commands, [])
        self.assertEqual(self.events, ['acquire', 'check 01-watch', 'reverify', 'guard-window', 'capture-open', 'window'])
        self.assertEqual(self.checks, ['01-watch'])
        self.assertEqual(self.reverified, [1])                             # the frozen manifests, once, before the open
        self.assertEqual(self.states(), [('01-watch', 'checks'), ('01-watch', 'capture-open'), ('01-watch', 'capture-closed')])
        self.assertEqual(result, dict(status='UART_WATCHED', probe_commands=0, capture=dict(raw='uart-rx.bin', window='window')))
        self.assertEqual(self.fake.capture.calls, ['window'])
        self.assertTrue(self.port.closed)

    def test_the_one_guard_check_asks_for_90_s_plus_the_window(self):
        self.watch()
        self.assertEqual(self.guard_limits, [WINDOW])
        self.assertEqual(self.remaining, [program.GUARD_SECONDS + WINDOW])  # 2,490 s (decision D3)

    def test_the_guard_deadline_is_checked_again_as_the_last_thing_before_the_open(self):
        self.check_seconds = 7
        self.watch()
        self.assertEqual(self.guard_windows, [(WINDOW, 1007.)])
        self.assertEqual(self.events.index('capture-open'), self.events.index('guard-window') + 1)

    def test_it_gets_the_port_the_driver_owns_and_a_directory_below_the_mode_directory(self):
        self.watch()
        self.assertIs(self.fake.opened[0]['port'], self.port)
        self.assertEqual(self.fake.opened[0]['directory'], self.directory / 'capture')
        self.assertEqual(self.fake.opened[0]['commands'], 0)

    def test_the_series_budget_is_300_s_plus_the_window(self):
        self.assertEqual(program.WATCH_SERIES_BASE, 300)
        self.assertEqual(program.watch_series_seconds(raw.WatchSpec(WINDOW)), 2700)
        self.assertEqual(program.watch_series_seconds(raw.WatchSpec(2700)), 3000)


class Refusals(Case):
    """Every gate refuses before a capture exists, and nothing reaches a probe command."""

    def assertNothingOpened(self, spec):
        self.assertNotIn('capture-open', self.events)
        self.assertEqual(spec.opened, [])
        self.assertFalse((self.directory / 'capture').exists())
        self.assertEqual(self.commands, [])
        self.assertEqual((self.failure()['mode'], self.failure()['automatic_retry']), ('watch', False))

    def test_a_failing_guard_or_ownership_check(self):
        self.guard_error = HardwareStop('Current mount protection unverified')
        spec = self.spec(seconds=WINDOW)
        with self.assertRaisesRegex(HardwareStop, 'mount protection'):
            self.watch(spec)
        self.assertNothingOpened(spec)
        self.assertTrue(self.port.closed)

    def test_the_series_budget_before_and_after_the_checks_and_at_the_open(self):
        cases = (('before the checks', dict(series_seconds=120 + WINDOW), 0, 0, 'Insufficient series budget for the next watch window'),
                 ('after the checks', {}, 255, 0, 'Series budget exhausted by the guard'),
                 ('at the open', {}, 0, 295, 'Watch window does not fit the series budget'))
        for name, options, checks, reverify, pattern in cases:
            with self.subTest(name):
                self.setUp()
                self.check_seconds = checks
                def slow(reverify=reverify):
                    self.reverified.append(1)
                    self.now += reverify
                spec = self.spec(seconds=WINDOW)
                with self.assertRaisesRegex(HardwareStop, pattern):
                    self.watch(spec, reverify=slow, **options)
                self.assertNothingOpened(spec)
        self.setUp()                                                        # one second less at the open just fits
        def slow():
            self.now += 294
        self.watch(self.spec(seconds=WINDOW), reverify=slow)
        self.assertIn('capture-open', self.events)

    def test_a_changed_manifest(self):
        def broken():
            raise HardwareStop('Recovery artefact changed')
        spec = self.spec(seconds=WINDOW)
        with self.assertRaisesRegex(HardwareStop, 'Recovery artefact changed'):
            self.watch(spec, reverify=broken)
        self.assertNothingOpened(spec)

    def test_a_guard_deadline_that_would_lapse_inside_the_window(self):
        self.guard_window_error = HardwareStop('Guard deadline would lapse during the watch window; no capture')
        spec = self.spec(seconds=WINDOW)
        with self.assertRaisesRegex(HardwareStop, 'Guard deadline'):
            self.watch(spec)
        self.assertNothingOpened(spec)
        self.assertIn('guard-window', self.events)

    def test_a_missing_port(self):
        self.port = None
        spec = self.spec(seconds=WINDOW)
        with self.assertRaisesRegex(HardwareStop, 'UART port not owned'):
            self.watch(spec)
        self.assertNothingOpened(spec)

    def test_a_capture_that_cannot_open_reads_nothing_and_stops(self):
        spec = self.spec(seconds=WINDOW, open_error=OSError('no space left'))
        with self.assertRaisesRegex(HardwareStop, 'UART capture could not be opened') as caught:
            self.watch(spec)
        self.assertIsInstance(caught.exception.__cause__, OSError)
        self.assertEqual(self.commands, [])
        self.assertNotIn(('01-watch', 'capture-open'), self.states())
        self.assertTrue(self.port.closed)

    def test_the_constructor_refuses_a_watch_without_a_window_or_without_reverification(self):
        for name, options in (('no capture', dict(capture=None)), ('no re-verification', dict(capture=self.spec(seconds=WINDOW), reverify=None)),
                              ('window above the cap', dict(capture=self.spec(seconds=raw.MAX_WATCH_SECONDS + 1))),
                              ('no window', dict(capture=self.spec(seconds=0))), ('fractional', dict(capture=self.spec(seconds=1.5)))):
            with self.subTest(name), self.assertRaisesRegex(HardwareStop, 'watch'):
                self.driver('watch', **options)
            self.assertFalse(self.directory.exists())

    def test_the_program_mode_never_takes_the_watch_window(self):
        with self.assertRaisesRegex(HardwareStop, 'watch mode only'):
            self.driver('program', capture=raw.WatchSpec(WINDOW))
        self.assertFalse(self.directory.exists())

    def test_run_refuses_any_probe_command_in_the_watch_mode(self):
        with self.watch_driver() as driver:
            with self.assertRaisesRegex(HardwareStop, 'runs no probe command'):
                driver.run('identity-only', 'q\n')
        self.assertEqual((self.commands, self.checks), ([], []))

    def test_watch_runs_only_in_the_watch_mode(self):
        spec = self.spec(seconds=180)
        with self.assertRaisesRegex(HardwareStop, 'only in the watch mode'), self.driver('program', capture=spec) as driver:
            driver.watch(self.guard_window)
        self.assertEqual((self.commands, self.checks, spec.opened), ([], [], []))

    def test_one_use_per_evidence_parent_and_a_new_directory(self):
        program.consume(self.root, 'watch')
        with self.assertRaisesRegex(HardwareStop, 'already used'):
            program.consume(self.root, 'watch')
        program.consume(self.root, 'program')                              # program and watch each run once under one parent
        self.directory.mkdir()
        with self.assertRaises(FileExistsError):
            self.watch_driver()


class Failures(Case):
    def test_a_reader_that_fails_in_the_window_stops_the_mode_and_nothing_restarts(self):
        spec = self.spec(seconds=WINDOW, window_error=HardwareStop('UART capture reader failed: OSError(device disconnected)'))
        with self.assertRaisesRegex(HardwareStop, 'UART capture reader failed'):
            self.watch(spec)
        self.assertEqual(spec.capture.calls, ['window'])                  # not aborted, not repeated, not reopened
        self.assertEqual(spec.opened.__len__(), 1)
        self.assertEqual(self.commands, [])
        self.assertEqual((self.failure()['steps'], self.failure()['automatic_retry']), (1, False))
        self.assertIn(('01-watch', 'capture-open'), self.states())
        self.assertNotIn(('01-watch', 'capture-closed'), self.states())
        self.assertFalse((self.directory / 'result.json').exists())
        self.assertTrue(self.port.closed)

    def test_an_interrupt_in_the_window_is_not_swallowed(self):
        spec = self.spec(seconds=WINDOW, window_error=KeyboardInterrupt())
        with self.assertRaises(KeyboardInterrupt):
            self.watch(spec)
        self.assertTrue(self.port.closed)
        self.assertEqual(self.commands, [])

    def test_a_failure_after_the_open_aborts_the_capture_and_keeps_the_error(self):
        original = program.Driver.note
        def note(driver, **entry):
            if entry.get('state') == 'capture-open':
                raise OSError('disk full')
            return original(driver, **entry)
        spec = self.spec(seconds=WINDOW, abort_error=HardwareStop('UART capture reader failed: boom'))
        with patch.object(program.Driver, 'note', note), self.assertRaisesRegex(OSError, 'disk full') as caught:
            self.watch(spec)
        self.assertEqual(spec.capture.calls, [('abort', 'watch step failed after the open')])
        self.assertIn('boom', caught.exception.capture_failure)
        self.assertEqual(self.commands, [])
        self.assertTrue(self.port.closed)


class RealCapture(Case):
    """The real capture against a scripted port and clock: a 2,400 s window without waiting 2,400 s."""

    def scripted(self, schedule, seconds=WINDOW, **port_options):
        clock = rawtest.Clock(100.)
        self.port = rawtest.ScriptedPort(clock, schedule, step=.5, **port_options)
        def factory(directory, port, **options):
            return raw.RawCapture(directory, port, clock=clock, sleep=clock.sleep, thread=False, **options)
        return raw.WatchSpec(seconds, factory=factory), clock

    def test_a_window_above_300_s_reads_to_its_end(self):
        spec, clock = self.scripted([(100.6, b'early\r\n'), (2490., b'late\r\n')])
        result = self.watch(spec)
        self.assertEqual((self.directory / 'capture' / 'uart-rx.bin').read_bytes(), b'early\r\nlate\r\n')
        self.assertGreaterEqual(clock.now, 100. + WINDOW)
        config = json.loads((self.directory / 'capture' / 'config.json').read_text())
        self.assertEqual((config['seconds'], config['passive']), (WINDOW, True))
        self.assertEqual([m['name'] for m in json.loads((self.directory / 'capture' / 'marks.json').read_text())],
                         ['opened', 'window-start', 'stopped'])
        self.assertEqual((result['capture']['window'], result['capture']['seconds']), ('window', WINDOW))
        self.assertEqual(self.commands, [])
        self.assertTrue(self.port.closed)

    def test_a_port_lost_in_the_window_is_reported_with_the_bytes_so_far(self):
        spec, clock = self.scripted([(100.6, b'kept\r\n')], fail_at=20)
        with self.assertRaisesRegex(HardwareStop, 'UART capture reader failed'):
            self.watch(spec)
        analysis = json.loads((self.directory / 'capture' / 'analysis.json').read_text())
        self.assertIn('device disconnected', analysis['reader_failure'])
        self.assertEqual((self.directory / 'capture' / 'uart-rx.bin').read_bytes(), b'kept\r\n')
        self.assertLess(clock.now, 100. + WINDOW)                          # the window ended early, nothing waited it out
        self.assertEqual(self.commands, [])
        self.assertTrue(self.port.closed)


class Limits(unittest.TestCase):
    def test_the_program_cap_stays_300_s_and_the_watch_cap_is_3600_s(self):
        self.assertEqual(image_profile.MAX_CAPTURE_SECONDS, 300)
        self.assertEqual(image_profile.MAX_WATCH_SECONDS, 3600)
        self.assertIs(raw.MAX_WATCH_SECONDS, image_profile.MAX_WATCH_SECONDS)

    def test_the_watch_spec_is_bounded_by_its_own_cap(self):
        for bad in (0, -1, raw.MAX_WATCH_SECONDS + 1, 1.5, True, None, '30'):
            with self.subTest(bad=bad), self.assertRaisesRegex(ValueError, '1 to 3600'):
                raw.WatchSpec(bad)
        self.assertEqual(raw.WatchSpec(raw.MAX_WATCH_SECONDS).seconds, 3600)
        self.assertEqual(raw.WatchSpec(1).seconds, 1)
        with self.assertRaisesRegex(ValueError, '1 to 300'):              # the program spec keeps its cap
            raw.CaptureSpec(WINDOW)
        self.assertEqual(set(vars(raw.WatchSpec(WINDOW))), {'seconds', 'factory'})

    def test_the_watch_spec_passes_its_cap_to_the_capture(self):
        seen = []
        spec = raw.WatchSpec(30, factory=lambda directory, port, **options: seen.append((directory, port, options)) or 'capture')
        self.assertEqual(spec.open('port', Path('/x/capture')), 'capture')
        self.assertEqual(seen, [(Path('/x/capture'), 'port', dict(seconds=30, limit=3600))])

    def test_the_raw_capture_accepts_a_long_window_only_under_the_watch_cap(self):
        with rawtest.tempfile.TemporaryDirectory() as directory:
            for number, (seconds, options) in enumerate(((301, {}), (WINDOW, {}), (3601, dict(limit=raw.MAX_WATCH_SECONDS)),
                                                         (30, dict(limit=10000)), (30, dict(limit=299)))):
                with self.subTest(seconds=seconds, options=options), self.assertRaises(ValueError):
                    raw.RawCapture(Path(directory) / str(number), object(), seconds=seconds, thread=False, **options)
                self.assertFalse((Path(directory) / str(number)).exists())
            capture = raw.RawCapture(Path(directory) / 'ok', object(), seconds=WINDOW, thread=False, limit=raw.MAX_WATCH_SECONDS)
            self.assertEqual(capture.seconds, WINDOW)
            capture.close('window')

    def test_the_largest_guard_requirement_fits_the_longest_guard_lifetime(self):
        self.assertLess(program.GUARD_SECONDS + raw.MAX_WATCH_SECONDS, m5_mount_guard.MAX_LIFETIME)
        self.assertGreater(program.GUARD_SECONDS + WINDOW, m5_mount_guard.MIN_LIFETIME)   # a short guard is refused, not stretched


def synthetic_profile(**changes):
    return cap.synthetic_profile(**changes)


class Profiles(unittest.TestCase):
    def test_the_watch_window_is_validated(self):
        for changes in (dict(watch_seconds=-1), dict(watch_seconds=raw.MAX_WATCH_SECONDS + 1), dict(watch_seconds=1.5),
                        dict(watch_seconds=True), dict(watch_seconds='30'), dict(watch_seconds=None),
                        dict(capture='none', capture_seconds=0, watch_seconds=WINDOW)):
            with self.subTest(changes=changes), self.assertRaisesRegex(ValueError, 'Inconsistent image profile'):
                synthetic_profile(**changes)
        self.assertEqual(synthetic_profile().watch_seconds, 0)
        self.assertEqual(synthetic_profile(watch_seconds=raw.MAX_WATCH_SECONDS).watch_seconds, 3600)
        self.assertEqual(synthetic_profile(watch_seconds=1).watch_seconds, 1)

    def test_the_registered_profiles_have_no_watch_window(self):
        for profile in (cap.M5, cap.S2):
            with self.subTest(profile.name):
                self.assertEqual(profile.watch_seconds, 0)
                self.assertIsNone(program.capture_spec(profile, 'watch'))

    def test_the_bench_profile_watches_2400_s_and_keeps_its_300_s_program_window(self):
        profile = bench_image.bench_profile()
        self.assertEqual((profile.capture, profile.capture_seconds, profile.watch_seconds), ('raw', 300, WINDOW))
        self.assertEqual(bench_image.BENCH_WATCH_SECONDS, WINDOW)
        watch = program.capture_spec(profile, 'watch')
        self.assertIs(type(watch), raw.WatchSpec)
        self.assertEqual(watch.seconds, WINDOW)
        spec = program.capture_spec(profile, 'program')
        self.assertIs(type(spec), raw.CaptureSpec)                       # program never gets the watch cap
        self.assertEqual(spec.seconds, 300)
        for mode in ('identify', 'restore'):
            self.assertIsNone(program.capture_spec(profile, mode))
        self.assertNotIn(profile.name, prepare.PROFILES)                 # still not registered

    def test_the_modes(self):
        self.assertEqual(program.MODES, ('identify', 'program', 'restore', 'watch'))


class RealWiring(unittest.TestCase):
    def wired(self, session=None):
        rp = base.RealParts('test_the_real_lsof_answers_are_recorded_as_evidence_files')
        rp.setUp()
        self.addCleanup(rp.doCleanups)
        parts, _ = rp.wired(session)
        return rp, parts

    def test_the_real_check_asks_the_guard_for_the_given_time_and_otherwise_what_it_did(self):
        import capture_m3_network as m3net
        rp, (preconditions, probe, acquire) = self.wired()
        asked = []
        with patch.object(m3net, 'guard_check', lambda session, directory, label, remaining, **k: asked.append(remaining)):
            preconditions(rp.directory, 'w', WINDOW, remaining=program.GUARD_SECONDS + WINDOW)
            preconditions(rp.directory, 'a', 30)
            preconditions(rp.directory, 'b', 60)
            preconditions(rp.directory, 'c')
        self.assertEqual(asked, [2490, 90, 120, 90])

    def test_the_guard_deadline_must_outlive_the_window_and_the_close(self):
        clock = lambda: 1000.
        margin = program.WATCH_CLOSE_SECONDS
        program.require_guard_window(dict(deadline=1000. + WINDOW + margin + .5), WINDOW, clock=clock)
        for deadline in (1000. + WINDOW + margin, 1000. + WINDOW, 1000., None, True, '9999999'):
            with self.subTest(deadline=deadline), self.assertRaisesRegex(HardwareStop, 'Guard deadline'):
                program.require_guard_window(dict(deadline=deadline), WINDOW, clock=clock)
        with self.assertRaisesRegex(HardwareStop, 'Guard deadline'):
            program.require_guard_window({}, WINDOW, clock=clock)

    def test_the_watch_mode_probe_refuses(self):
        with self.assertRaisesRegex(HardwareStop, 'runs no probe command'):
            program.no_probe('q\n', Path('/nonexistent/x.log'), 30)


class Seam(Case):
    """Option X records two files: the 300 s window of ``program`` and the ``watch`` window that opens later.

    Real report output split at a report boundary stands for the procedure of decision D6 (no TCP client before
    ``watch`` shows ``capture-open``, so nothing prints in the gap). The judge's sequence rule then reads the
    concatenation as one stream, and shows a report printed in the gap as a lost one and a report cut by a loss as torn."""

    def setUp(self):
        super().setUp()
        self.text = (SEAM / 'real-native-reports-1-to-3.txt').read_bytes()
        self.starts = [self.text.index(b'BENCH-REPORT v=1 begin seq=%d ' % n) for n in (1, 2, 3)]

    def test_the_fixture_is_the_unchanged_evidence_file(self):
        record = json.loads((SEAM / 'source.json').read_text())
        for name, entry in record['files'].items():
            data = (SEAM / name).read_bytes()
            self.assertEqual(hashlib.sha256(data).hexdigest(), entry['sha256'])
            self.assertEqual(data, (REPOSITORY / entry['source']).read_bytes())

    def program_window(self, data):
        """The 300 s window of ``program``, through the real capture (the program spec, the program cap)."""
        clock = rawtest.Clock(100.)
        port = rawtest.ScriptedPort(clock, [(100.6, data)] if data else [], step=.5)
        directory = self.root / 'program-capture'
        raw.RawCapture(directory, port, clock=clock, sleep=clock.sleep, seconds=raw.MAX_CAPTURE_SECONDS, thread=False).window()
        return (directory / 'uart-rx.bin').read_bytes()

    def watch_window(self, data):
        clock = rawtest.Clock(5000.)
        self.port = rawtest.ScriptedPort(clock, [(5000.6, data)] if data else [], step=.5)
        spec = raw.WatchSpec(WINDOW, factory=lambda directory, port, **o: raw.RawCapture(
            directory, port, clock=clock, sleep=clock.sleep, thread=False, **o))
        self.watch(spec)
        return (self.directory / 'capture' / 'uart-rx.bin').read_bytes()

    def judged(self, first, second):
        stream = self.program_window(first) + self.watch_window(second)
        self.assertEqual(self.commands, [])
        return judge.Capture(stream.decode('latin-1'))

    def test_a_split_at_a_report_boundary_reads_as_one_stream(self):
        capture = self.judged(self.text[:self.starts[2]], self.text[self.starts[2]:])
        self.assertEqual(capture.failures, [])
        self.assertEqual([(r['seq'], r['reason']) for r in capture.reports], [(1, 'boot'), (2, 'quiet'), (3, 'idle')])

    def test_a_report_printed_in_the_gap_is_a_lost_report(self):
        capture = self.judged(self.text[:self.starts[1]], self.text[self.starts[2]:])
        self.assertTrue(any('lost report' in failure for failure in capture.failures), capture.failures)

    def test_bytes_lost_inside_a_report_at_the_seam_show_as_a_broken_report(self):
        lines = self.text[self.starts[1]:self.starts[2]].splitlines(keepends=True)
        first = self.text[:self.starts[1]] + b''.join(lines[:3])
        second = b''.join(lines[6:]) + self.text[self.starts[2]:]
        capture = self.judged(first, second)
        self.assertTrue(any('seq=2' in failure for failure in capture.failures), capture.failures)

    def test_a_line_cut_by_the_close_with_no_byte_lost_still_joins(self):
        cut = self.starts[1] + 20                                           # inside the first line of report 2
        capture = self.judged(self.text[:cut], self.text[cut:])
        self.assertEqual(capture.failures, [])
        analysis = json.loads((self.root / 'program-capture' / 'analysis.json').read_text())
        self.assertEqual(analysis['final_line']['state'], 'cut')           # the first file records the cut as an observation


class FakeDriver:
    made = []

    def __init__(self, plan, approval, mode, directory, **options):
        self.mode, self.directory, self.options = mode, Path(directory), options
        self.directory.mkdir(parents=True)
        FakeDriver.made.append(self)

    def __enter__(self):
        return self

    def __exit__(self, *error):
        return False

    def watch(self, guard_window):
        self.guard_window = guard_window
        return dict(status='UART_WATCHED')

    def program(self, binary, entry):
        return dict(status='M5_PROGRAMMED_VERIFIED_STARTED')


class Main(unittest.TestCase):
    """``main`` for ``watch``: the read-only probe, the series budget, the guard window and the refusal before ``consume``."""

    def setUp(self):
        self.temp = rawtest.tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        FakeDriver.made = []
        self.parts = (lambda *a, **k: None, lambda *a: 'probe', lambda: None)

    def run_main(self, mode, profile, deadline_in=10000.):
        files = {}
        for name in ('m5.bin', 'entry.bin', 'normal.bin'):
            files[name] = self.root / name
            files[name].write_bytes(name.encode())
        guard = self.root / 'guard.json'
        guard.write_text(json.dumps(dict(deadline=program.m3_clock.monotonic() + deadline_in)))
        plan = dict(manifest_sha256='a' * 64, entry_manifest_sha256='b' * 64, recovery_manifest_sha256='c' * 64)
        inputs = (plan, {}, dict(files=dict(binary=str(files['m5.bin']))), dict(binary=str(files['entry.bin'])),
                  dict(binary=str(files['normal.bin']), source=dict(git_commit='0' * 40)))
        argv = [mode, '--execute-hardware', '--guard-session', str(guard), '--evidence', str(self.root / 'evidence' / mode)]
        for name in ('plan', 'approval', 'manifest', 'entry-manifest', 'recovery-manifest'):
            argv += ['--' + name, str(self.root / name)]
        (self.root / 'evidence').mkdir(exist_ok=True)
        with patch.object(program, 'bind_profile', lambda name: (profile, lambda manifest, digest: None)), \
             patch.object(program, 'load_inputs', lambda args, **k: inputs), \
             patch.object(program, 'real_parts', lambda args, session: self.parts), \
             patch.object(program, 'require_clean_tree', lambda repo: None), \
             patch.object(program, 'write_inputs', lambda *a: None), \
             patch.object(program, 'Driver', FakeDriver), contextlib.redirect_stdout(io.StringIO()):
            return program.main(argv)

    def test_watch_gets_the_refusing_probe_its_budget_its_window_and_the_bound_guard_session(self):
        self.assertEqual(self.run_main('watch', bench_image.bench_profile(), deadline_in=100.), 0)
        driver, = FakeDriver.made
        options = driver.options
        self.assertIs(options['probe'], program.no_probe)
        self.assertIs(type(options['capture']), raw.WatchSpec)
        self.assertEqual((options['capture'].seconds, options['series_seconds']), (WINDOW, 2700))
        self.assertIs(options['preconditions'], self.parts[0])
        self.assertIs(options['acquire'], self.parts[2])
        self.assertTrue(callable(options['reverify']))
        with self.assertRaisesRegex(HardwareStop, 'Guard deadline'):       # the guard of the session runs 100 s, not 2,405 s
            driver.guard_window(WINDOW)
        driver.guard_window(10)
        self.assertTrue((self.root / 'evidence' / 'm5-driver-watch.consumed').exists())
        self.assertEqual(json.loads((self.root / 'evidence' / 'watch' / 'result.json').read_text()), dict(status='UART_WATCHED'))

    def test_a_profile_without_a_watch_window_is_refused_before_anything_is_consumed(self):
        with self.assertRaisesRegex(HardwareStop, 'no watch window'):
            self.run_main('watch', cap.S2)
        self.assertEqual(FakeDriver.made, [])
        self.assertFalse((self.root / 'evidence' / 'm5-driver-watch.consumed').exists())

    def test_program_keeps_its_probe_its_900_s_budget_and_its_300_s_window(self):
        self.run_main('program', bench_image.bench_profile())
        options = FakeDriver.made[0].options
        self.assertEqual(options['probe'](), 'probe')
        self.assertNotIn('series_seconds', options)                        # the Driver default, 900 s
        self.assertIs(type(options['capture']), raw.CaptureSpec)
        self.assertEqual(options['capture'].seconds, 300)


if __name__ == '__main__':
    unittest.main()
