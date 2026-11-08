"""The raw UART capture of the programming driver: exact bytes, a line view, and classification instead of stopping.

The fixtures under ``fixtures/program-capture`` are a real capture of stage 1 (the restore of the accepted normal image),
copied unchanged from ``docs/evidence``. Cut lines, non-ASCII bytes and poll gaps are synthetic: the real capture has none.
No test touches a serial port or a board; every port is a fake.
"""
import hashlib
import json
import re
import sys
import tempfile
import threading
import time
import unittest
from pathlib import Path

TARGET = Path(__file__).resolve().parents[1] / 'target'
sys.path.insert(0, str(TARGET))
import uart_raw_capture as subject
from capture_m3 import HardwareStop

FIXTURES = Path(__file__).resolve().parent / 'fixtures' / 'program-capture'
REPOSITORY = Path(__file__).resolve().parents[2]


class Clock:
    def __init__(self, now=100.):
        self.now = now

    def __call__(self):
        return self.now

    def sleep(self, seconds):
        self.now += seconds


class ScriptedPort:
    """Returns the scheduled chunks at their times and an empty read (a poll timeout) in between."""

    def __init__(self, clock, schedule, step=.025, fail_at=None):
        self.clock, self.schedule, self.step, self.fail_at = clock, list(schedule), step, fail_at
        self.reads, self.closed = 0, False

    def read(self, size):
        assert not self.closed, 'read after close'
        self.reads += 1
        if self.fail_at is not None and self.reads > self.fail_at:
            raise OSError('device disconnected')
        target = self.clock.now + self.step
        if self.schedule and self.schedule[0][0] <= target:
            when, chunk = self.schedule.pop(0)
            self.clock.now = max(self.clock.now, when)
            return chunk
        self.clock.now = target
        return b''

    def close(self):
        self.closed = True


def fixture_schedule():
    rows = [json.loads(line) for line in (FIXTURES / 'real-restore-receive.jsonl').read_text().splitlines()]
    return [(row['monotonic'], bytes.fromhex(row['hex'])) for row in rows]


class Case(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name) / 'capture'

    def capture(self, schedule, seconds=5, start=100., **port_options):
        clock = Clock(start)
        port = ScriptedPort(clock, schedule, **port_options)
        capture = subject.RawCapture(self.directory, port, clock=clock, sleep=clock.sleep, seconds=seconds, thread=False)
        return capture, port, clock

    def run_window(self, schedule, seconds=5, **options):
        capture, port, clock = self.capture(schedule, seconds, **options)
        capture.window()
        return capture, port, clock

    def json(self, name):
        return json.loads((self.directory / name).read_text())


class RealCapture(Case):
    def setUp(self):
        super().setUp()
        self.raw = (FIXTURES / 'real-restore-uart-rx.bin.raw').read_bytes()
        schedule = fixture_schedule()
        self.capture_, self.port, self.clock = self.run_window(schedule, seconds=17, start=schedule[0][0] - .1)

    def test_fixture_is_the_unchanged_stage_1_evidence(self):
        provenance = json.loads((FIXTURES / 'source.json').read_text())
        for name, record in provenance['files'].items():
            self.assertEqual(hashlib.sha256((FIXTURES / name).read_bytes()).hexdigest(), record['sha256'])
            evidence = REPOSITORY / provenance['origin'] / record['from']
            self.assertEqual(evidence.read_bytes(), (FIXTURES / name).read_bytes())

    def test_raw_bytes_and_chunk_times_are_exactly_what_the_port_delivered(self):
        self.assertEqual((self.directory / 'uart-rx.bin').read_bytes(), self.raw)
        events = [json.loads(line) for line in (self.directory / 'receive.jsonl').read_text().splitlines()]
        expected = [json.loads(line) for line in (FIXTURES / 'real-restore-receive.jsonl').read_text().splitlines()]
        self.assertEqual([(e['monotonic'], e['hex']) for e in events], [(e['monotonic'], e['hex']) for e in expected])
        offset = 0
        for event in events:
            self.assertEqual(event['offset'], offset)
            offset += len(event['hex']) // 2
        self.assertEqual(self.json('analysis.json')['raw']['sha256'], hashlib.sha256(self.raw).hexdigest())

    def test_the_line_view_holds_the_same_lines_and_times_as_the_accepted_receiver(self):
        view = self.json('lines.json')
        shown = [(row['monotonic'], re.sub(r'\x1b\[[0-9;]*[A-Za-z]', '', row['text']).strip()) for row in view]
        shown = [(t, text) for t, text in shown if text]
        accepted = json.loads((FIXTURES / 'real-restore-lines.json').read_text())
        self.assertEqual(shown, [tuple(row) for row in accepted])
        self.assertEqual(len(accepted), 397)
        # Every row points into the raw bytes: offset and length reproduce its line exactly.
        for row in view:
            self.assertEqual(self.raw[row['offset']:row['offset'] + row['length']].decode('latin-1').rstrip('\r\n'), row['text'])
        self.assertEqual(sum(row['length'] for row in view), len(self.raw))

    def test_analysis_of_the_real_capture_records_observations_only(self):
        analysis = self.json('analysis.json')
        self.assertEqual(analysis['final_line']['state'], 'complete')
        self.assertEqual(analysis['non_ascii']['count'], 0)
        self.assertEqual(analysis['poll_gaps']['count'], 0)
        self.assertEqual(analysis['reader_failure'], None)
        quiet = analysis['quiet_periods']['periods']
        self.assertEqual(len(quiet), 1)                                  # the one real silence of 1.07 s between two chunks
        self.assertAlmostEqual(quiet[0]['seconds'], 1.0749, places=3)
        self.assertIn('acceptance', analysis)
        self.assertEqual(analysis['acceptance'], 'none: the offline analysis of the raw bytes decides')

    def test_the_port_is_never_closed_by_the_capture(self):
        self.assertFalse(self.port.closed)


class Classification(Case):
    def test_a_cut_final_line_is_classified_and_everything_before_it_is_kept(self):
        capture, _, _ = self.run_window([(100.1, b'first\r\nsecond\r\nthird li')])
        analysis = self.json('analysis.json')
        self.assertEqual(analysis['final_line'], dict(state='cut', offset=15, length=8, text='third li', monotonic=100.1))
        view = self.json('lines.json')
        self.assertEqual([(r['text'], r['complete']) for r in view], [('first', True), ('second', True), ('third li', False)])
        self.assertEqual((self.directory / 'uart-rx.bin').read_bytes(), b'first\r\nsecond\r\nthird li')
        self.assertEqual(capture.summary()['final_line'], 'cut')

    def test_complete_and_empty_final_states(self):
        self.run_window([(100.1, b'a\r\nb\n')])
        self.assertEqual(self.json('analysis.json')['final_line']['state'], 'complete')
        for name in list(p.name for p in self.directory.iterdir()):
            (self.directory / name).unlink()
        self.directory.rmdir()
        self.run_window([])
        analysis = self.json('analysis.json')
        self.assertEqual((analysis['final_line']['state'], analysis['raw']['bytes'], analysis['lines']), ('none', 0, 0))

    def test_non_ascii_bytes_are_kept_and_classified_not_rejected(self):
        raw = b'ok\r\ncaf\xc3\xa9 \xff\xfe\x80\r\nplain\r\n'
        self.run_window([(100.1, raw[:9]), (100.2, raw[9:])])
        analysis = self.json('analysis.json')
        self.assertEqual(analysis['non_ascii']['count'], 5)
        self.assertEqual(analysis['non_ascii']['ranges'], [dict(offset=7, length=2, hex='c3a9'), dict(offset=10, length=3, hex='fffe80')])
        self.assertEqual((self.directory / 'uart-rx.bin').read_bytes(), raw)
        with self.assertRaises(UnicodeDecodeError):
            raw.decode('ascii')                                          # the accepted Receiver would have stopped here
        view = self.json('lines.json')
        self.assertEqual(view[1]['text'].encode('latin-1'), b'caf\xc3\xa9 \xff\xfe\x80')

    def test_control_and_escape_bytes_stay_in_the_line_view(self):
        self.run_window([(100.1, b'\x1b[0;32m<inf> main: up\x1b[0m\r\n\x00\r\n')])
        view = self.json('lines.json')
        self.assertEqual(view[0]['text'], '\x1b[0;32m<inf> main: up\x1b[0m')
        self.assertEqual(view[1]['text'], '\x00')

    def test_a_poll_gap_is_classified_and_the_reader_keeps_reading(self):
        capture, port, clock = self.capture([(100.1, b'before\r\n'), (101.0, b'after\r\n')], seconds=3)
        original = port.read
        def gapped(size):                                                # the reader was not scheduled for 0.6 s
            if port.reads == 4:
                clock.now += .6
            return original(size)
        port.read = gapped
        capture.window()
        analysis = self.json('analysis.json')
        self.assertEqual(analysis['poll_gaps']['count'], 1)
        gap = analysis['poll_gaps']['gaps'][0]
        self.assertAlmostEqual(gap['gap'], .625, places=3)
        self.assertEqual(gap['raw_offset'], 8)                           # the position in the raw stream where the gap was seen
        self.assertEqual((self.directory / 'uart-rx.bin').read_bytes(), b'before\r\nafter\r\n')
        self.assertIsNone(analysis['reader_failure'])

    def test_the_gap_threshold_is_the_accepted_quarter_second(self):
        for step, flagged in ((.25, False), (.2501, True)):
            with self.subTest(step=step):
                self.setUp()
                capture, port, clock = self.capture([], seconds=1, step=step)
                capture.window()
                self.assertEqual(self.json('analysis.json')['poll_gaps']['count'] > 0, flagged)

    def test_a_line_that_spans_chunks_has_a_first_and_a_last_time(self):
        self.run_window([(100.1, b'M3_WORK seq'), (100.4, b'uence=1\r\n')])
        row = self.json('lines.json')[0]
        self.assertEqual((row['first_monotonic'], row['monotonic'], row['text']), (100.1, 100.4, 'M3_WORK sequence=1'))

    def test_quiet_periods_are_reported_with_their_raw_position(self):
        self.run_window([(100.1, b'a\r\n'), (102.6, b'b\r\n')], seconds=4)
        quiet = self.json('analysis.json')['quiet_periods']
        self.assertEqual(quiet['threshold_seconds'], 1.0)
        self.assertEqual([(p['offset'], round(p['seconds'], 3)) for p in quiet['periods']], [(3, 2.5)])
        self.assertEqual(self.json('analysis.json')['first_byte']['seconds_after_open'], round(.1, 3))

    def test_marks_split_the_raw_stream_by_phase_for_the_offline_analysis(self):
        capture, port, clock = self.capture([(100.1, b'old\r\n'), (100.5, b'new\r\n')], seconds=1)
        capture.window()
        marks = {m['name']: m for m in self.json('marks.json')}
        self.assertEqual(marks['opened']['raw_offset'], 0)
        self.assertIn('window-start', marks)
        self.assertIn('stopped', marks)
        self.assertLessEqual(marks['opened']['monotonic'], marks['window-start']['monotonic'])
        self.assertEqual(marks['stopped']['raw_offset'], 10)

    def test_classification_never_raises_for_any_byte_pattern(self):
        every = bytes(range(256)) * 3
        for blob in (every, b'\n' * 5, b'\r' * 5, b'\xff' * 100, every[:-1] + b'\n'):
            with self.subTest(blob=blob[:6]):
                self.setUp()
                capture, _, _ = self.run_window([(100.1, blob)])
                self.assertEqual((self.directory / 'uart-rx.bin').read_bytes(), blob)
                self.assertEqual(self.json('analysis.json')['raw']['bytes'], len(blob))
                self.assertEqual(capture.summary()['raw'], 'uart-rx.bin')


class Failure(Case):
    def test_a_reader_failure_keeps_the_bytes_ends_the_window_early_and_is_reported(self):
        capture, port, clock = self.capture([(100.1, b'kept\r\n')], seconds=120, fail_at=10)
        with self.assertRaisesRegex(HardwareStop, 'UART capture reader failed'):
            capture.window()
        self.assertLess(clock.now, 105)                                  # it did not wait for the whole 120 s window
        self.assertEqual((self.directory / 'uart-rx.bin').read_bytes(), b'kept\r\n')
        analysis = self.json('analysis.json')
        self.assertIn('device disconnected', analysis['reader_failure'])
        self.assertEqual(analysis['final_line']['state'], 'complete')
        self.assertFalse(port.closed)

    def test_abort_stops_at_once_and_still_writes_every_artifact(self):
        capture, port, clock = self.capture([(100.1, b'half li')], seconds=120)
        for _ in range(8):
            capture.poll_once()
        capture.abort('start command failed')
        self.assertEqual(sorted(p.name for p in self.directory.iterdir()),
                         sorted(['analysis.json', 'clock.json', 'config.json', 'lines.json', 'marks.json', 'poll.jsonl', 'receive.jsonl', 'uart-rx.bin']))
        self.assertLess(clock.now, 101)
        marks = {m['name']: m for m in self.json('marks.json')}
        self.assertEqual(marks['aborted']['reason'], 'start command failed')
        self.assertEqual(self.json('analysis.json')['final_line']['state'], 'cut')
        self.assertEqual(capture.summary()['window'], 'aborted')
        self.assertFalse(port.closed)

    def test_abort_reports_a_reader_that_had_already_failed(self):
        capture, port, clock = self.capture([], seconds=120, fail_at=2)
        for _ in range(4):
            try:
                capture.poll_once()
            except OSError:
                pass
        with self.assertRaisesRegex(HardwareStop, 'UART capture reader failed'):
            capture.abort('x')

    def test_a_file_that_cannot_be_written_never_loses_the_chunk_that_was_read(self):
        class Broken:
            def write(self, data):
                raise OSError('disk full')
            def flush(self):
                pass
        for broken in ('polls', 'events', 'raw_file'):
            with self.subTest(broken=broken):
                self.setUp()
                capture, port, clock = self.capture([(100.01, b'kept\r\n')], seconds=5)
                setattr(capture, broken, Broken())
                with self.assertRaisesRegex(HardwareStop, 'UART capture reader failed'):
                    capture.window()
                analysis = self.json('analysis.json')
                self.assertEqual(analysis['raw']['bytes'], 6)                       # the chunk is in the analysed bytes in every case
                self.assertIn('disk full', analysis['reader_failure'])
                on_disk = (self.directory / 'uart-rx.bin').read_bytes()
                self.assertEqual(analysis['raw']['file_bytes'], len(on_disk))
                self.assertEqual(on_disk, b'' if broken == 'raw_file' else b'kept\r\n')

    def test_a_port_that_fails_on_its_first_read_is_not_an_opened_capture(self):
        class Dead:
            def read(self, size):
                raise OSError('device gone')
        before = threading.active_count()
        with self.assertRaisesRegex(HardwareStop, 'UART capture reader failed'):
            subject.RawCapture(self.directory, Dead(), seconds=1)
        self.assertEqual(threading.active_count(), before)
        self.assertEqual(sorted(p.name for p in self.directory.iterdir() if p.suffix == '.json' and p.name == 'analysis.json'), [])

    def test_a_reader_that_never_completes_its_first_read_is_not_an_opened_capture(self):
        release = threading.Event()

        class Stuck:
            def read(self, size):
                release.wait(5)
                return b''

        self.addCleanup(release.set)
        with self.assertRaisesRegex(HardwareStop, 'UART capture reader did not initialise'):
            subject.RawCapture(self.directory, Stuck(), seconds=1)

    def test_an_interrupt_during_the_window_still_stops_the_reader_and_writes_the_evidence(self):
        capture, port, clock = self.capture([(100.1, b'kept\r\n')], seconds=120)
        original = port.read
        def interrupted(size):
            if port.reads >= 6:
                raise KeyboardInterrupt()
            return original(size)
        port.read = interrupted
        with self.assertRaises(KeyboardInterrupt):
            capture.window()
        self.assertEqual((self.directory / 'uart-rx.bin').read_bytes(), b'kept\r\n')
        self.assertEqual(self.json('analysis.json')['final_line']['state'], 'complete')
        self.assertEqual(capture.summary()['window'], 'interrupted')

    def test_an_existing_capture_directory_is_never_reused(self):
        self.directory.mkdir()
        with self.assertRaises(FileExistsError):
            self.capture([])
        self.assertEqual(list(self.directory.iterdir()), [])

    def test_a_failed_open_leaves_no_reader_running(self):
        class Dead:
            def read(self, size):
                raise AssertionError('must not be read')
        before = threading.active_count()
        with self.assertRaises(OSError):
            subject.RawCapture(self.directory / 'missing' / 'deeper', Dead(), seconds=1)
        self.assertEqual(threading.active_count(), before)


class Threaded(Case):
    """The real reader thread against a fake port with real time."""

    class Port:
        def __init__(self, chunks):
            self.chunks, self.closed, self.reads = list(chunks), False, 0

        def read(self, size):
            if self.closed:
                raise AssertionError('read after the driver closed the port')
            self.reads += 1
            time.sleep(.002)
            return self.chunks.pop(0) if self.chunks else b''

        def close(self):
            self.closed = True

    def test_thread_records_bytes_and_stops_before_the_window_call_returns(self):
        port = self.Port([b'one\r\n', b'two\xff\r\nthree'])
        capture = subject.RawCapture(self.directory, port, seconds=1)
        started = capture.thread
        capture.window()
        self.assertFalse(started.is_alive())
        reads = port.reads
        time.sleep(.05)
        self.assertEqual(port.reads, reads)                              # nothing reads the port once the window call has returned
        port.close()
        self.assertEqual((self.directory / 'uart-rx.bin').read_bytes(), b'one\r\ntwo\xff\r\nthree')
        self.assertEqual(self.json('analysis.json')['final_line']['state'], 'cut')


    def test_a_reader_stuck_in_a_read_is_reported_and_never_waited_for_twice(self):
        release = threading.Event()

        class Stuck:
            reads = 0

            def read(self, size):
                self.reads += 1
                if self.reads > 1:                      # the first read completes, so the capture opens; the next one hangs
                    release.wait(5)
                return b''

        self.addCleanup(release.set)
        capture = subject.RawCapture(self.directory, Stuck(), seconds=1)
        started = time.monotonic()
        with self.assertRaisesRegex(HardwareStop, 'UART capture reader did not stop'):
            capture.abort('test')
        self.assertLess(time.monotonic() - started, 3)
        self.assertEqual(self.json('analysis.json')['final_line']['state'], 'none')    # the evidence is still written


class Spec(unittest.TestCase):
    def test_the_window_is_a_bounded_whole_number_of_seconds(self):
        for bad in (0, -1, subject.MAX_CAPTURE_SECONDS + 1, 1.5, True, None, '30'):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                subject.CaptureSpec(bad)
        self.assertEqual(subject.CaptureSpec(subject.MAX_CAPTURE_SECONDS).seconds, subject.MAX_CAPTURE_SECONDS)
        self.assertEqual(subject.CaptureSpec(1).seconds, 1)

    def test_open_hands_the_owned_port_to_the_factory_and_nothing_else(self):
        seen = []
        spec = subject.CaptureSpec(30, factory=lambda directory, port, **options: seen.append((directory, port, options)) or 'capture')
        port = object()
        self.assertEqual(spec.open(port, Path('/x/capture')), 'capture')
        self.assertEqual(seen, [(Path('/x/capture'), port, dict(seconds=30))])


if __name__ == '__main__':
    unittest.main()
