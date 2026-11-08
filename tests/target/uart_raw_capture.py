"""Passive raw UART capture for an image that logs continuously (hardware safety gate support, never touches a device).

The capture reads a port that the driver already owns exclusively. It never opens, closes, configures or writes
the port, never opens a second one, and takes no lock: ownership and the board lock stay with ``Driver.acquire``.

What it keeps, exactly: every byte that the port delivered, in ``uart-rx.bin`` (flushed per chunk), the time of
every chunk (``receive.jsonl``) and of every poll (``poll.jsonl``). What it adds as a view: ``lines.json``
(timestamped lines, bytes decoded as latin-1, which is lossless, each row pointing into the raw bytes) and
``analysis.json``. The analysis CLASSIFIES a cut final line, non-ASCII bytes and poll gaps as observations. It never
stops the capture and never accepts or rejects the image: acceptance is an offline replay of the raw bytes. The
accepted ``capture_m3.Receiver`` stops on all three, which suits a known image and loses the evidence of an unknown one.

The only things that stop a capture are real failures: the port raising on a read, or a file that cannot be written.
Those end the capture, keep every byte read so far and are reported as ``HardwareStop``.
"""
import bisect
import hashlib
import json
import threading
from pathlib import Path

import m3_clock
from capture_m3 import HardwareStop
from image_profile import MAX_CAPTURE_SECONDS

GAP_THRESHOLD = .25            # seconds between two polls that the accepted Receiver tolerates; more is classified, not fatal
QUIET_THRESHOLD = 1.             # seconds without a chunk that the analysis reports (offline split of old image and new image)
READ_SIZE = 4096
LIST_LIMIT = 100                 # entries kept per classification list; the counts stay exact
READY_SECONDS = 1.
STEP_SECONDS = .1


def _line(value):
    return json.dumps(value) + '\n'


def line_view(raw, chunks):
    """Timestamped lines of the raw bytes. ``chunks`` is a list of (monotonic, offset, length).

    A row holds ``offset`` and ``length`` (terminator included) into ``raw``, ``text`` (latin-1, without LF and a
    CR before it), ``first_monotonic`` and ``monotonic`` (the chunks that carried its first and its last byte) and
    ``complete`` (False only for a final line without a terminator).
    """
    starts = [offset for _, offset, _ in chunks]

    def time_of(position):
        return chunks[bisect.bisect_right(starts, position) - 1][0]

    rows, begin = [], 0
    while begin < len(raw):
        end = raw.find(b'\n', begin)
        complete = end != -1
        stop = end + 1 if complete else len(raw)
        body = raw[begin:end if complete else stop]
        if complete and body.endswith(b'\r'):
            body = body[:-1]
        rows.append(dict(offset=begin, length=stop - begin, text=body.decode('latin-1'), complete=complete,
                         first_monotonic=time_of(begin), monotonic=time_of(stop - 1)))
        begin = stop
    return rows


def _non_ascii(raw):
    ranges, start = [], None
    count = 0
    for position, value in enumerate(raw):
        if value > 127:
            count += 1
            if start is None:
                start = position
        elif start is not None:
            ranges.append((start, position))
            start = None
    if start is not None:
        ranges.append((start, len(raw)))
    return count, ranges


def analyse(raw, chunks, gaps, marks, failure, opened, file_bytes=None):
    """Observations only. Nothing here raises for any content and nothing decides acceptance."""
    rows = line_view(raw, chunks)
    count, ranges = _non_ascii(raw)
    if not raw:
        final = dict(state='none')
    elif raw.endswith(b'\n'):
        final = dict(state='complete')
    else:
        last = rows[-1]
        final = dict(state='cut', offset=last['offset'], length=last['length'], text=last['text'], monotonic=last['monotonic'])
    quiet = [dict(start_monotonic=a[0], end_monotonic=b[0], seconds=b[0] - a[0], offset=b[1])
             for a, b in zip(chunks, chunks[1:]) if b[0] - a[0] >= QUIET_THRESHOLD]
    return dict(
        schema=1,
        acceptance='none: the offline analysis of the raw bytes decides',
        raw=dict(file='uart-rx.bin', bytes=len(raw), sha256=hashlib.sha256(raw).hexdigest(), chunks=len(chunks), file_bytes=file_bytes),
        lines=len(rows),
        complete_lines=sum(1 for row in rows if row['complete']),
        final_line=final,
        non_ascii=dict(count=count, ranges=[dict(offset=a, length=b - a, hex=raw[a:min(b, a + 16)].hex())
                                            for a, b in ranges[:LIST_LIMIT]], ranges_total=len(ranges)),
        poll_gaps=dict(threshold_seconds=GAP_THRESHOLD, count=len(gaps), gaps=gaps[:LIST_LIMIT],
                       maximum=max((g['gap'] for g in gaps), default=0)),
        quiet_periods=dict(threshold_seconds=QUIET_THRESHOLD, count=len(quiet), periods=quiet[:LIST_LIMIT]),
        first_byte=dict(monotonic=chunks[0][0], offset=0, seconds_after_open=round(chunks[0][0] - opened, 6)) if chunks else None,
        last_byte=dict(monotonic=chunks[-1][0]) if chunks else None,
        marks=[mark['name'] for mark in marks],
        reader_failure=failure,
    )


class RawCapture:
    """One bounded capture. ``window`` is the normal end, ``abort`` the end after a failure of the step it watches."""

    def __init__(self, directory, port, *, seconds, clock=m3_clock.monotonic, sleep=m3_clock.sleep, thread=True):
        self.directory, self.port, self.clock, self.sleep = Path(directory), port, clock, sleep
        self.seconds = CaptureSpec(seconds).seconds
        self.failure, self.stopped_by = None, None
        self.chunks, self.gaps, self.marks = [], [], []
        self.raw = bytearray()
        self.thread, self.stop, self.finished = None, threading.Event(), False
        self.directory.mkdir(exist_ok=False)
        self.files = []
        try:
            for name in ('uart-rx.bin', 'receive.jsonl', 'poll.jsonl'):
                self.files.append((self.directory / name).open('wb' if name.endswith('.bin') else 'w'))
            self.raw_file, self.events, self.polls = self.files
            (self.directory / 'clock.json').write_text(json.dumps(m3_clock.identity(), indent=2) + '\n')
            (self.directory / 'config.json').write_text(json.dumps(dict(
                seconds=self.seconds, gap_threshold_seconds=GAP_THRESHOLD, quiet_threshold_seconds=QUIET_THRESHOLD,
                read_size=READ_SIZE, passive=True), indent=2) + '\n')
            self.previous = self.opened = self.clock()
            self.mark('opened')
            if thread:
                self.ready = threading.Event()
                self.thread = threading.Thread(target=self.read, daemon=True)
                self.thread.start()
                if not self.ready.wait(READY_SECONDS):
                    raise HardwareStop('UART capture reader did not initialise')
                if self.failure is not None:
                    raise HardwareStop('UART capture reader failed: ' + self.failure)
        except BaseException:
            self.stop.set()
            if self.thread is not None:
                self.thread.join(1)
            for handle in self.files:
                handle.close()
            raise

    def mark(self, name, **fields):
        self.marks.append(dict(name=name, monotonic=self.clock(), raw_offset=len(self.raw), **fields))

    def poll_once(self):
        """One read and its record. A failure of the port or of a file ends the capture's reading: it is recorded, not raised."""
        try:
            chunk = self.port.read(READ_SIZE)
            now = self.clock()
            gap = now - self.previous
            if gap > GAP_THRESHOLD:
                self.gaps.append(dict(monotonic=now, gap=gap, raw_offset=len(self.raw)))
            self.previous = now
            offset = len(self.raw)
            if chunk:                                  # in memory first: no file failure can lose a chunk that was read
                chunk = bytes(chunk)
                self.raw += chunk
                self.chunks.append((now, offset, len(chunk)))
                self.raw_file.write(chunk)             # the raw file before any record about it
                self.raw_file.flush()
                self.events.write(_line(dict(monotonic=now, offset=offset, hex=chunk.hex())))
                self.events.flush()
            self.polls.write(_line(dict(monotonic=now, gap=gap)))
        except Exception as error:
            self.failure = repr(error)
            return False
        return True

    def read(self):
        try:
            first = self.poll_once()                   # a port that cannot be read is found before the capture counts as open
            self.ready.set()
            while first and not self.stop.is_set() and self.poll_once():
                pass
        except BaseException as error:
            self.failure = repr(error)

    def window(self):
        """Keep reading until the window has passed or the reader failed, then stop and write the analysis."""
        try:
            self.mark('window-start')
            deadline = self.clock() + self.seconds
            while self.clock() < deadline and self.failure is None:
                if self.thread is None:
                    self.poll_once()
                else:
                    self.sleep(min(STEP_SECONDS, max(deadline - self.clock(), 0)))
        except BaseException as error:                 # an interrupt: the primary error stays, the capture is still closed
            try:
                self.close('interrupted')
            except HardwareStop as secondary:
                error.capture_failure = repr(secondary)
            raise
        self.close('window')

    def abort(self, reason):
        """End now, after the step that this capture watches failed. Reports a reader failure like ``window``."""
        self.mark('aborted', reason=reason)
        self.close('aborted')

    def close(self, how):
        problems = []
        try:
            self.stop.set()
            if self.thread is not None:
                self.thread.join(1)
                if self.thread.is_alive():
                    problems.append('UART capture reader did not stop')
            self.mark('stopped')
            self.stopped_by = how
            raw = bytes(self.raw)
            try:
                self.polls.flush()
            finally:
                for handle in self.files:
                    handle.close()
            analysis = analyse(raw, self.chunks, self.gaps, self.marks, self.failure, self.opened,
                               (self.directory / 'uart-rx.bin').stat().st_size)
            rows = line_view(raw, self.chunks)
            (self.directory / 'marks.json').write_text(json.dumps(self.marks, indent=2) + '\n')
            (self.directory / 'lines.json').write_text(json.dumps(rows, indent=2) + '\n')
            (self.directory / 'analysis.json').write_text(json.dumps(analysis, indent=2) + '\n')
            self.analysis = analysis
        except BaseException as error:
            problems.append('UART capture evidence not written: ' + repr(error))
        finally:
            self.finished = True
        if self.failure is not None:
            raise HardwareStop('UART capture reader failed: ' + self.failure)
        if problems:
            raise HardwareStop('; '.join(problems))

    def summary(self):
        analysis = getattr(self, 'analysis', None)
        return dict(raw='uart-rx.bin', seconds=self.seconds, window=self.stopped_by or 'open',
                    bytes=len(self.raw), final_line=analysis['final_line']['state'] if analysis else None,
                    non_ascii_bytes=analysis['non_ascii']['count'] if analysis else None,
                    poll_gaps=len(self.gaps), reader_failure=self.failure,
                    acceptance='offline replay required; this is a raw passive capture')


class CaptureSpec:
    """What a profile asks for: a raw capture with a bounded window. Opening it is the driver's decision."""

    def __init__(self, seconds, factory=RawCapture):
        if type(seconds) is not int or not 1 <= seconds <= MAX_CAPTURE_SECONDS:
            raise ValueError('Capture window must be a whole number of seconds from 1 to %d' % MAX_CAPTURE_SECONDS)
        self.seconds, self.factory = seconds, factory

    def open(self, port, directory):
        return self.factory(directory, port, seconds=self.seconds)
