"""Bounded, split Commander runs of the ``program`` mode (blocker B1, row C7). Never touches hardware.

Real inputs: J-Link Commander V9.86a transcripts and driver operation logs of the stage 1 runs, copied unchanged from
``docs/evidence`` into ``fixtures/programmer-split`` (``source.json`` lists each source and its SHA-256). No capture of a
562,232 B load or read-back exists. That size below is the recorded size of the bench build; the load and verify times
for it are **unmeasured**, and nothing here claims them.
"""
import hashlib
import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parent / 'target'))
import test_m5_program as base
import capture_m3
import program_m5_image as program
from capture_m3 import HardwareStop

FIXTURES = HERE / 'fixtures' / 'programmer-split'
GOOD, OK = base.GOOD, base.OK
BENCH_BYTES = 562232                        # the bench image of the plan (synthetic size here: no such BIN is committed)


def fixture(name):
    return (FIXTURES / name).read_text()


class Fixtures(unittest.TestCase):
    def test_every_fixture_is_the_unchanged_evidence_file(self):
        record = json.loads(fixture('source.json'))
        repo = HERE.parents[1]
        for name, entry in record['files'].items():
            with self.subTest(name):
                data = (FIXTURES / name).read_bytes()
                self.assertEqual(hashlib.sha256(data).hexdigest(), entry['sha256'])
                self.assertEqual(data, (repo / entry['source']).read_bytes())


class CommanderLimit(unittest.TestCase):
    """The limit is a parameter of the one accepted wrapper, with a hard ceiling that no caller can pass."""

    def timed_out(self, **options):
        with tempfile.TemporaryDirectory() as d, \
             patch.object(capture_m3, 'verify_jlink_tools', return_value='/pinned/JLinkExe'), \
             patch.object(capture_m3.subprocess, 'Popen') as create, patch.object(capture_m3.os, 'killpg') as kill:
            process = create.return_value
            process.pid = 4242
            process.wait.side_effect = capture_m3.subprocess.TimeoutExpired('probe', 1)
            output = Path(d) / 'probe.log'
            with self.assertRaisesRegex(HardwareStop, 'Probe timeout'):
                capture_m3.commander('602003106', 'q\n', output, **options)
            record = json.loads(output.with_suffix('.timeout.json').read_text())
            return [c.kwargs['timeout'] for c in process.wait.call_args_list], kill.call_args_list, create.call_count, record

    def test_the_default_stays_30_s(self):
        waits, kills, created, record = self.timed_out()
        self.assertEqual(waits, [30, 1])
        self.assertEqual(record['limit_seconds'], 30)

    def test_a_given_limit_is_the_wait_and_the_kill_and_reap_are_unchanged(self):
        waits, kills, created, record = self.timed_out(seconds=60)
        self.assertEqual(waits, [60, 1])                           # SIGKILL of the group, one bounded reap, no retry
        self.assertEqual([c.args for c in kills], [(4242, capture_m3.signal.SIGKILL)])
        self.assertEqual(created, 1)
        self.assertEqual((record['status'], record['limit_seconds']), ('PROBE_STATE_UNCERTAIN', 60))

    def test_a_limit_outside_1_to_the_ceiling_is_refused_before_any_process(self):
        self.assertEqual(capture_m3.MAX_COMMAND_SECONDS, 80)
        for bad in (0, -1, capture_m3.MAX_COMMAND_SECONDS + 1, 1000, 30.0, True, None, '30'):
            with self.subTest(bad=bad), tempfile.TemporaryDirectory() as d, \
                 patch.object(capture_m3, 'verify_jlink_tools', return_value='/pinned/JLinkExe'), \
                 patch.object(capture_m3.subprocess, 'Popen') as create:
                with self.assertRaisesRegex(HardwareStop, 'Commander limit'):
                    capture_m3.commander('602003106', 'q\n', Path(d) / 'probe.log', seconds=bad)
                self.assertEqual(create.call_count, 0)



class GuardTime(unittest.TestCase):
    """The guard time required at each check grows with the run, as the series reserves do (blocking finding B-1)."""

    def test_a_30_s_run_still_requires_exactly_90_s(self):
        self.assertEqual(program.GUARD_SECONDS, 90)
        self.assertEqual(program.guard_seconds(30), 90)

    def test_a_longer_run_requires_90_s_plus_what_it_exceeds_30_s(self):
        self.assertEqual(program.guard_seconds(60), 120)
        self.assertGreater(program.guard_seconds(60), 90)
        self.assertEqual(program.guard_seconds(capture_m3.MAX_COMMAND_SECONDS), 140)

    def test_m5_s2_and_the_normal_image_require_the_same_90_s_as_before(self):
        for size in (97544, 103180, 107872):
            self.assertEqual(program.guard_seconds(program.command_seconds(size)), 90)

    def test_the_largest_requirement_is_within_the_shortest_guard_lifetime(self):
        import m5_mount_guard
        self.assertLess(program.guard_seconds(capture_m3.MAX_COMMAND_SECONDS), m5_mount_guard.MIN_LIFETIME)


class GuardWiring(base.Case):
    def test_the_driver_passes_each_run_s_limit_to_the_checks(self):
        Path(self.m5).write_bytes(b'\xff' * BENCH_BYTES)
        self.responses = [GOOD, OK, GOOD, OK, GOOD]
        with self.driver() as driver:
            driver.program(self.m5, self.entry)
        self.assertEqual(self.guard_limits, [30, 30, 60, 60, 30])

    def test_the_real_checks_ask_the_guard_for_90_s_plus_the_extra_of_the_run(self):
        import capture_m3_network as m3net
        rp = base.RealParts('test_the_real_lsof_answers_are_recorded_as_evidence_files')
        rp.setUp()
        self.addCleanup(rp.doCleanups)
        (preconditions, probe, acquire), _ = rp.wired()
        asked = []
        with patch.object(m3net, 'guard_check', lambda session, directory, label, remaining, **k: asked.append(remaining)):
            preconditions(rp.directory, 'a', 30)
            preconditions(rp.directory, 'b', 60)
            preconditions(rp.directory, 'c', capture_m3.MAX_COMMAND_SECONDS)
        self.assertEqual(asked, [90, 120, 140])

    def test_the_deadline_is_checked_again_just_before_the_probe_command(self):
        seen = []
        def commander(probe_id, given, output, seconds):
            seen.append(seconds)
            return GOOD
        for deadline_in, allowed in ((62.5, True), (62, False), (61, False), (-5, False)):  # the run, its 1 s reap, 1 s for the tool hash
            with self.subTest(deadline_in=deadline_in):
                rp = base.RealParts('test_the_real_lsof_answers_are_recorded_as_evidence_files')
                rp.setUp()
                self.addCleanup(rp.doCleanups)
                with patch.object(program.m3_clock, 'monotonic', lambda: 1000.0):
                    (preconditions, probe, acquire), _ = rp.wired(dict(deadline=1000.0 + deadline_in))
                    seen.clear()
                    with patch.object(capture_m3, 'commander', commander):
                        if allowed:
                            probe('q\n', rp.directory / 'p.log', 60)
                        else:
                            with self.assertRaisesRegex(HardwareStop, 'Guard deadline'):
                                probe('q\n', rp.directory / 'p.log', 60)
                self.assertEqual(seen, [60] if allowed else [])


class Formula(unittest.TestCase):
    def test_per_range_limit_with_the_30_s_floor_and_the_ceiling(self):
        cases = {1: 30, 97544: 30, 103180: 30, 107872: 30, 131072: 30, 131073: 30, 262144: 30, 262145: 40,
                 BENCH_BYTES: 60, 6 * 131072: 70, 7 * 131072: 80}
        for size, seconds in cases.items():
            with self.subTest(size=size):
                self.assertEqual(program.command_seconds(size), seconds)

    def test_m5_s2_and_the_normal_image_keep_exactly_today_s_limit(self):
        for size in (97544, 103180, 107872):                     # real M5, S2 (facts.json) and normal BIN sizes
            self.assertEqual(program.command_seconds(size), capture_m3.COMMAND_SECONDS)

    def test_an_image_beyond_the_ceiling_or_without_a_size_is_refused(self):
        for size in (7 * 131072 + 1, 8 * 1024 * 1024, 0, -1, 1.5, True, None):
            with self.subTest(size=size), self.assertRaises(HardwareStop):
                program.command_seconds(size)

    def test_the_formula_keeps_at_least_twice_the_measured_time_of_every_real_load(self):
        """Per range, compare + erase + program + verify of the J-Link flash loader; the rest is fixed per run."""
        logs = ('run3-program-03-program-m5.log', 'run3-restore-02-load-normal.log')
        for name in logs:
            with self.subTest(name):
                download = program.flash_download(fixture(name))
                self.assertEqual(download['affected_ranges'], 1)
                scaled = download['parts']['Compare'] + download['parts']['Erase'] + download['parts']['Program'] + download['parts']['Verify']
                fixed = download['parts']['Prepare'] + download['parts']['Restore']
                self.assertGreaterEqual(program.SECONDS_PER_RANGE, 2 * scaled)
                self.assertGreaterEqual(program.SECONDS_BASE, 2 * fixed)

    def test_the_formula_keeps_at_least_twice_the_measured_time_of_every_real_read_back(self):
        """A read-only verify step, checks included, minus the same run's identity-only step (checks and connect only)."""
        for run in ('run2', 'run3', 'main-run'):
            with self.subTest(run):
                steps = steps_of(fixture(run + '-restore-operations.json'))
                verify = steps['03-verify-normal'] - steps['01-identity-only']
                per_range = verify * 131072 / 107872
                self.assertGreaterEqual(program.SECONDS_PER_RANGE, 2 * per_range)


def steps_of(text):
    """Seconds from each step's 'checks' row to its 'done' row (host checks plus the Commander run)."""
    rows, start, out = json.loads(text), {}, {}
    for row in rows:
        if row['state'] == 'checks':
            start[row['step']] = row['monotonic']
        elif row['state'] == 'done':
            out[row['step']] = row['monotonic'] - start[row['step']]
    return out


class FlashDownload(unittest.TestCase):
    def test_the_real_report_of_a_load_is_parsed(self):
        download = program.flash_download(fixture('run3-program-03-program-m5.log'))
        self.assertEqual(download, dict(skipped=False, affected_ranges=1, affected_bytes=131072, total_seconds=6.531,
                                        parts=dict(Prepare=1.609, Compare=0.585, Erase=0.232, Program=2.939, Verify=0.288, Restore=0.875)))

    def test_the_real_skipped_report_is_recorded_as_skipped(self):
        self.assertEqual(program.flash_download(fixture('fixed-driver-restore-02-load-normal-skipped.log')), dict(skipped=True))

    def test_a_log_without_a_report_gives_none(self):
        self.assertIsNone(program.flash_download(fixture('run3-restore-03-verify-normal.log')))


class Split(base.Case):
    """The program step is three runs: load, read-back verify, reset and go. Each has its own limit; none is retried."""

    def bench(self, size=BENCH_BYTES):
        Path(self.m5).write_bytes(b'\xff' * size)

    def program(self, responses):
        self.responses = list(responses)
        with self.driver() as driver:
            return driver.program(self.m5, self.entry)

    def test_commands_labels_and_limits_of_the_bench_size(self):
        self.bench()
        load_log = fixture('run3-program-03-program-m5.log')
        result = self.program([GOOD, OK, load_log, OK, GOOD])
        self.assertEqual(self.commands, ['q\n', 'verifybin "%s", 0x60000000\nIsHalted\nq\n' % self.entry,
                                         'loadfile "%s", 0x60000000\nq\n' % self.m5,
                                         'verifybin "%s", 0x60000000\nq\n' % self.m5, 'r\ng\nq\n'])
        self.assertEqual(self.limits, [30, 30, 60, 60, 30])
        self.assertEqual(self.checks, ['01-identity-only', '02-entry-image', '03-program-m5-load', '04-program-m5-verify',
                                       '05-program-m5-start'])
        self.assertEqual(result['status'], 'M5_PROGRAMMED_VERIFIED_STARTED')
        rows = json.loads((self.directory / 'operations.json').read_text())
        self.assertEqual([r['limit_seconds'] for r in rows if r['state'] == 'checks'], [30, 30, 60, 60, 30])
        done = [r for r in rows if r['step'] == '03-program-m5-load' and r['state'] == 'done'][0]
        self.assertEqual(done['flash_download']['affected_bytes'], 131072)   # recorded from the run's own report (a real M5 one)

    def test_m5_size_gets_30_s_for_every_run(self):
        self.program([GOOD, OK, GOOD, OK, GOOD])
        self.assertEqual(self.limits, [30] * 5)

    def test_an_image_beyond_the_ceiling_is_refused_before_the_first_probe_command(self):
        self.bench(7 * 131072 + 1)
        with self.assertRaisesRegex(HardwareStop, 'ceiling'):
            self.program([])
        self.assertEqual((self.commands, self.checks), ([], []))

    def test_failure_orders_never_run_a_later_step(self):
        timeout = HardwareStop('Probe timeout; state uncertain; no further hardware changes')
        cases = {
            'load times out mid-flash': ([GOOD, OK, timeout], 3, 'Probe timeout'),
            'load diagnostic or non-zero exit': ([GOOD, OK, HardwareStop('Probe diagnostic; stop hardware changes')], 3, 'diagnostic'),
            'load ok, verify mismatch (non-zero exit)': ([GOOD, OK, GOOD, base.MISMATCH], 4, 'evidence failed'),
            'load ok, verify without success line': ([GOOD, OK, GOOD, GOOD], 4, 'verification absent'),
            'load ok, verify times out': ([GOOD, OK, GOOD, timeout], 4, 'Probe timeout'),
            'load and verify ok, start fails': ([GOOD, OK, GOOD, OK, timeout], 5, 'Probe timeout'),
        }
        for name, (responses, ran, message) in cases.items():
            with self.subTest(name):
                self.setUp()
                with self.assertRaisesRegex(HardwareStop, message):
                    self.program(responses)
                self.assertEqual(len(self.commands), ran)
                self.assertEqual(sum('r\ng\n' in c for c in self.commands), int(ran == 5))
                failure = json.loads((self.directory / 'failure.json').read_text())
                self.assertEqual((failure['steps'], failure['automatic_retry']), (ran, False))
                self.assertTrue(self.port.closed)

    def test_a_guard_refusal_between_the_runs_stops_with_nothing_more(self):
        for refused, ran in (('04-program-m5-verify', 3), ('05-program-m5-start', 4)):
            with self.subTest(refused):
                self.setUp()
                def guard(directory, label, limit, refused=refused):
                    self.checks.append(label)
                    if label == refused:
                        raise HardwareStop('Current mount protection unverified')
                self.preconditions = guard
                with self.assertRaisesRegex(HardwareStop, 'mount protection'):
                    self.program([GOOD, OK, GOOD, OK, GOOD])
                self.assertEqual(len(self.commands), ran)

    def test_both_reserves_grow_by_what_a_limit_exceeds_30_s(self):
        # Before the checks 120 s, after them 45 s, each plus (limit - 30 s): 150 s and 75 s for a 60 s run.
        for seconds, before, pattern in ((60, 149, 'Insufficient series budget'), (60, 151, 'exhausted by the guard'),
                                         (30, 151, None)):
            with self.subTest(seconds=seconds, before=before):
                self.setUp()
                self.check_seconds = 80                          # leaves 71 s after the checks: enough for 45, not for 75
                with self.driver(series_seconds=400) as driver:
                    self.now = driver.end - before
                    if pattern:
                        with self.assertRaisesRegex(HardwareStop, pattern):
                            driver.run('x', 'q\n', seconds=seconds)
                    else:
                        driver.run('x', 'q\n', seconds=seconds)
                self.assertEqual(len(self.commands), 0 if pattern else 1)


    def test_the_whole_three_run_budget_is_checked_before_the_load(self):
        # After the load step's checks: the 30 s load, then the read-back and start reserves (120 s each): 270 s.
        for series, ran in ((265, 2), (275, 5)):
            with self.subTest(series=series):
                self.setUp()
                self.responses = [GOOD, OK, GOOD, OK, GOOD]
                if ran == 2:
                    with self.assertRaisesRegex(HardwareStop, 'Series budget does not cover the load, read-back and start'):
                        with self.driver(series_seconds=series) as driver:
                            driver.program(self.m5, self.entry)
                else:
                    with self.driver(series_seconds=series) as driver:
                        driver.program(self.m5, self.entry)
                self.assertEqual(len(self.commands), ran)
                self.assertFalse(ran == 2 and any('loadfile' in c for c in self.commands))

    def test_identify_refuses_a_candidate_beyond_the_ceiling_before_any_command(self):
        self.bench(7 * 131072 + 1)
        with self.assertRaisesRegex(HardwareStop, 'ceiling'), self.driver('identify') as driver:
            driver.identify([('integrated-normal', self.normal), ('big', self.m5)])
        self.assertEqual((self.commands, self.checks), ([], []))

    def test_identify_gives_each_candidate_its_size_limit(self):
        self.bench()
        self.responses = [GOOD, OK, OK]
        with self.driver('identify') as driver:
            driver.identify([('integrated-normal', self.normal), ('bench', self.m5)])
        self.assertEqual(self.limits, [30, 30, 60])


class Diagnostics(unittest.TestCase):
    def test_the_real_disconnect_diagnostic_after_a_load_only_run_is_refused(self):
        import host_fixtures
        real = (host_fixtures.DIRECTORY / 'jlink-loadfile-verifybin-quit-diagnostic.log').read_text()
        self.assertTrue(program.accepted_disconnect_diagnostic(real, 'loadfile "<image>.bin", 0x60000000\nverifybin "<image>.bin", 0x60000000\nq\n'))
        self.assertFalse(program.accepted_disconnect_diagnostic(real, 'loadfile "<image>.bin", 0x60000000\nq\n'))

    def test_an_odd_flash_report_after_a_load_is_recorded_as_unparsed_and_never_raises(self):
        real = fixture('run3-program-03-program-m5.log')
        for odd in (real.replace('Total: 6.531s', 'Total: 6.5.31s'), real.replace('(131072 bytes)', '(131,072 bytes)'),
                    real.replace('Prepare: 1.609s', 'Prepare: 1..609s')):
            with self.subTest(odd=odd[-40:]):
                self.assertIsNone(program.flash_download(odd))


class RestoreLimits(base.Restore):
    def test_restore_keeps_30_s_for_each_of_its_runs(self):
        holder = {}
        self.responses = [GOOD, OK, OK, GOOD]
        with self.driver('restore') as driver:
            driver.restore(self.normal, self.observation([self.BANNER, self.BOOT], holder))
        self.assertEqual(self.limits, [30, 30, 30, 30])


class RealTranscripts(base.Case):
    """The real verify-only and start-only transcripts have exactly the shapes of the new verify and start runs."""

    def commander(self, text):
        from types import SimpleNamespace
        def popen(command, stdout, stderr, start_new_session, env):
            stdout.write(text.encode())
            return SimpleNamespace(wait=lambda timeout=None: 0, pid=1)
        with tempfile.TemporaryDirectory() as directory, patch.object(capture_m3, 'verify_jlink_tools', lambda: '/x/JLinkExe'), \
                patch.object(capture_m3.subprocess, 'Popen', popen):
            return capture_m3.commander('602003106', 'q\n', Path(directory) / 'out.log', seconds=60)

    def test_the_real_read_back_and_start_runs_pass_the_wrapper_and_the_identity_check(self):
        verify = self.commander(fixture('run3-restore-03-verify-normal.log'))
        program.check_identity(verify)
        self.assertIn('Verify successful', verify)
        start = self.commander(fixture('run3-restore-04-start-normal.log'))
        program.check_identity(start)
        self.assertIn('J-Link>g', start)


if __name__ == '__main__':
    unittest.main()
