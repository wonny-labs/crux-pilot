"""Programming, verification and restoration driver for the M5 stage 1 physical run (hardware safety gate).

Built only on the accepted M3 helpers: J-Link Commander through ``capture_m3.commander``
(pinned executable and DLL hashes, probe serial, firmware-update guard, a bounded limit per
run with SIGKILL of the process group on expiry, no retry), ``capture_m3_network.image_commands``
(reset, go), the guard check, the UART and probe ownership inventory and the exact manifest
verification. It never retries, never erases, never touches fuses and never starts a
second probe command after a failed one.

Each Commander run is limited to 30 s, except a run that loads or reads back an image: it gets
``command_seconds(size)``, 10 s plus 10 s per started 128 KiB of the image, never less than 30 s
and never more than ``capture_m3.MAX_COMMAND_SECONDS`` (80 s). An image above that is refused
before the first probe command.

Each subcommand needs ``--execute-hardware``, a clean committed tree, and the exact plan and
approval documents of ``capture_m5_network`` (the approval must be complete). It takes the
board lock and the exclusive UART before the first probe command and holds both to the end.
A mode runs once per evidence parent directory.

  identify  identity probe, then ``verifybin`` of each candidate image with ``IsHalted``, until
            the first one that does not match (a mismatch ends the run); changes no flash and
            starts no reset, but may leave the core halted
  program   identity probe, ``verifybin`` of the identified entry image (stop if it differs),
            then the exact M5 BIN: ``loadfile``, ``verifybin``, reset, go in one run when its limit is
            30 s (M5, S2, the normal image); a larger image in three runs: ``loadfile``; a full
            ``verifybin``; reset and go only after that read-back succeeded
  restore   identity probe, ``loadfile`` + ``verifybin`` of the exact normal BIN, a second
            read-only full ``verifybin``, then reset and go with a passive UART capture open
            (raw bytes kept; its acceptance analysis is a separate offline replay)
  watch     read-only: no probe command at all. One step of checks (guard check requiring 90 s
            plus the window, ownership inventory, adapter identity, series budget, manifest
            re-verification, guard deadline re-check), then a passive raw UART capture of the
            profile's ``watch_seconds`` (at most 3600 s); for a profile that has a watch window
            only, after ``program`` has finished and released the port. Writes nothing to the board.
"""
import argparse
import fcntl
import hashlib
import json
import re
import subprocess
import sys
import termios
from pathlib import Path
import capture_m3
import capture_m3_network as m3net
import capture_m5_network as runner
import m3_clock
from capture_m3 import HardwareStop
from image_profile import MAX_WATCH_SECONDS
from m5_bench import BENCH, only_process_owns
from uart_raw_capture import CaptureSpec, WatchSpec

BASE = 0x60000000
MODES = ('identify', 'program', 'restore', 'watch')
CAPTURE_MODES = ('program', 'watch')            # the modes that may carry a capture; watch must, and runs no probe command
OBSERVE_SECONDS = 15
RESERVE_BEFORE, RESERVE_AFTER = 120, 45        # the accepted wrappers' whole-series reserves before and after the guard
COMMAND_SECONDS = capture_m3.COMMAND_SECONDS   # the default limit of one Commander run (30 s)
GUARD_SECONDS = 90                             # guard time left at each check before a 30 s run; grows with the limit
GUARD_RUN_MARGIN = 2                           # just before the run: the 1 s reap and 1 s for the tool hash check
# Limit of one run that loads or reads back an image. J-Link programs the flash in 128 KiB ranges. The real stage 1
# runs (fixtures/programmer-split) took at most 4.52 s per range for compare, erase, program and the loader's verify,
# 2.49 s fixed (prepare, restore), and at most about 4.9 s per 128 KiB for a read-back: 10 s each keeps twice that.
RANGE_BYTES, SECONDS_BASE, SECONDS_PER_RANGE = 128 * 1024, 10, 10
# The watch mode (blocker B4, option X, owner decision 2026-11-12). Its series is 300 s plus the window (decision D5): the
# acquisition checks and the one step of checks need far less than the 900 s of three Commander steps. Its one guard check
# requires 90 s plus the window (D3) and is not repeated inside the window (D4). WATCH_CLOSE_SECONDS is what the series
# and the guard deadline must still hold after the window: the reader's 1 s join and the evidence files written at close.
WATCH_SERIES_BASE = 300
WATCH_CLOSE_SECONDS = 5


def require(condition, message):
    if not condition:
        raise HardwareStop(message)


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def command_seconds(size):
    """The limit of a run that loads or reads back ``size`` bytes: per started range, at least 30 s, at most the ceiling."""
    require(type(size) is int and size > 0, 'Image size unknown; no probe command')
    seconds = max(COMMAND_SECONDS, SECONDS_BASE + SECONDS_PER_RANGE * -(-size // RANGE_BYTES))
    require(seconds <= capture_m3.MAX_COMMAND_SECONDS,
            'Image of %d bytes needs %d s per run, above the %d s ceiling; no probe command' % (size, seconds, capture_m3.MAX_COMMAND_SECONDS))
    return seconds


def image_seconds(path):
    return command_seconds(Path(path).stat().st_size)


def guard_seconds(seconds):
    """Guard time that must be left at the check before a run of ``seconds``: 90 s for 30 s, plus what a run exceeds 30 s
    (as the series reserves grow). 120 s for a 60 s run, 140 s at the 80 s ceiling; the guard lives at least 300 s."""
    return GUARD_SECONDS + max(0, seconds - COMMAND_SECONDS)


def watch_series_seconds(spec):
    """The series budget of the watch mode: 300 s plus its window (decision D5)."""
    return WATCH_SERIES_BASE + spec.seconds


def require_guard_window(guard_session, seconds, clock=m3_clock.monotonic):
    """The watch mode's last check before the capture opens: the guard deadline must outlive the window and the close.

    The counterpart of the deadline re-check before every probe command (``real_parts.probe``). The guard is not checked
    again inside the window (decision D4), so this is the only promise that it covers the window."""
    deadline = guard_session.get('deadline')
    require(type(deadline) in (int, float) and clock() + seconds + WATCH_CLOSE_SECONDS < deadline,
            'Guard deadline would lapse during the watch window; no capture')


def no_probe(commands, output, seconds):
    """The probe of the watch mode: it refuses. The mode has no probe command, and ``Driver.run`` refuses before this."""
    raise HardwareStop('The watch mode runs no probe command')


_SECONDS = r'\d+(?:\.\d+)?'


def flash_download(text):
    """The J-Link flash loader's own report of a ``loadfile``, recorded as evidence; it decides nothing.

    Any other format, including a malformed number, gives None: a report can never stop the mode."""
    if re.search(r'(?m)^J-Link: Flash download: Bank 0 @ 0x60000000: Skipped\. Contents already match', text):
        return dict(skipped=True)
    affected = re.search(r'(?m)^J-Link: Flash download: Bank 0 @ 0x60000000: (\d+) ranges? affected \((\d+) bytes\)$', text)
    part = r'\w+: %ss' % _SECONDS
    total = re.search(r'(?m)^J-Link: Flash download: Total: (%s)s \((%s(?:, %s)*)\)$' % (_SECONDS, part, part), text)
    if not affected or not total:
        return None
    try:
        return dict(skipped=False, affected_ranges=int(affected[1]), affected_bytes=int(affected[2]), total_seconds=float(total[1]),
                    parts={name: float(value) for name, value in re.findall(r'(\w+): (%s)s' % _SECONDS, total[2])})
    except ValueError:
        return None


def check_identity(text):
    """Same acceptance as the M5 lease: target core, debug port, probe serial and target voltage."""
    require('CPUID register: 0x411FC271' in text and 'DPIDR: 0x0BD11477' in text and
            re.search(r'S/N:\s*' + BENCH['probe_serial'] + r'\b', text) and 'Cortex-M7' in text,
            'Target/probe identity mismatch; no further operations')
    voltage = re.search(r'VTref\s*=\s*([\d.]+)\s*V', text)
    require(voltage and 3 <= float(voltage[1]) <= 3.6, 'Target voltage mismatch; no further operations')
    return text


def check_nic_identity(directory, label, run_checked):
    """The dedicated adapter is the reviewed one and is statically configured. Carrier is not required here:
    the running firmware may have the Ethernet down."""
    interface = run_checked(['ifconfig', BENCH['nic_interface']], directory / (label + '-nic.txt'))
    require(re.findall(r'(?m)^[ \t]*ether ([0-9a-f:]+)[ \t]*$', interface) == [BENCH['nic_mac']], 'Dedicated adapter identity changed')
    service = run_checked(['networksetup', '-getinfo', BENCH['service']], directory / (label + '-service.txt')).splitlines()
    require(all(line in service for line in ('Manual Configuration', 'IP address: ' + BENCH['host'], 'Subnet mask: 255.255.255.0',
                                              'Router: (null)', 'Ethernet Address: ' + BENCH['nic_mac'])), 'Isolated service changed')


class Driver:
    """One mode of one run. ``acquire`` returns the owned UART port; ``preconditions`` is the per-command check."""

    def __init__(self, plan, approval, mode, directory, *, probe, preconditions, acquire=None, images=None, reverify=None,
                 clock=m3_clock.monotonic, series_seconds=900, capture=None):
        require(mode in MODES, 'Unknown mode')
        require(capture is None or mode in CAPTURE_MODES, 'A capture is allowed only for the program mode and the read-only watch mode')
        require(not isinstance(capture, WatchSpec) or mode == 'watch', 'The watch window is for the watch mode only')
        require(mode != 'watch' or (capture is not None and reverify is not None),
                'The watch mode needs its capture window and the manifest re-verification')
        require(mode != 'watch' or isinstance(capture, WatchSpec), 'The watch mode needs a WatchSpec window, never a program window')
        require(mode != 'watch' or (type(capture.seconds) is int and 1 <= capture.seconds <= MAX_WATCH_SECONDS),
                'The watch window must be a whole number of seconds from 1 to %d' % MAX_WATCH_SECONDS)
        runner.validate_approval(approval, plan)       # also requires every plan operation to be approved
        self.mode, self.directory, self.clock = mode, Path(directory), clock
        self.probe_command, self.preconditions, self.acquire = probe, preconditions, acquire
        self.pinned = dict(images or {})
        self.reverify, self.capture = reverify, capture
        self.end = clock() + series_seconds
        self.step, self.log, self.port = 0, [], None
        self.directory.mkdir(parents=True, exist_ok=False)

    def __enter__(self):
        if self.acquire:
            self.port = self.acquire()                 # board lock and exclusive UART, before the first probe command
        return self

    def __exit__(self, kind, error, trace):
        try:
            if kind is not None:
                try:
                    self.fail(error)
                except OSError as secondary:
                    error.evidence_failure = repr(secondary)
                    raise
        finally:
            if self.port is not None:
                self.port.close()
                self.port = None
        return False

    def note(self, **entry):
        self.log.append(dict(entry, monotonic=self.clock()))
        (self.directory / 'operations.json').write_text(json.dumps(self.log, indent=2) + '\n')

    def fail(self, error):
        (self.directory / 'failure.json').write_text(json.dumps(dict(mode=self.mode, error=repr(error), steps=self.step,
                                                                     automatic_retry=False), indent=2) + '\n')

    @staticmethod
    def safe(*paths):
        """Refuse before the first probe command if any image path could alter a Commander script line."""
        require(all('"' not in str(path) and '\n' not in str(path) and str(path).startswith('/') for path in paths), 'Unsafe image path')

    def verify_command(self, path, halted=False):
        self.safe(path)
        return 'verifybin "%s", 0x%X\n%sq\n' % (path, BASE, 'IsHalted\n' if halted else '')

    def _checks(self, label, entry, seconds, extra, *, images=(), reverify=False, before=None, remaining=None,
                subject='probe command'):
        """The gate sequence of one step, shared by ``run`` and ``watch`` so that the two cannot drift. Returns the label.

        In this order: the series reserve before the checks, the ``checks`` record, the guard check, ownership inventory and
        adapter identity (``preconditions``), the reserve after them, the image hashes, the manifest re-verification, and
        ``before`` as the very last thing. ``extra`` is what both reserves grow by. ``remaining`` (the watch mode only) is the
        guard time the check must require; without it ``preconditions`` derives it from ``seconds`` exactly as before.
        """
        self.step += 1
        label = '%02d-%s' % (self.step, label)
        require(self.clock() + RESERVE_BEFORE + extra < self.end, 'Insufficient series budget for the next ' + subject)
        self.note(step=label, **entry)
        if remaining is None:
            self.preconditions(self.directory, label, seconds)   # the guard time it requires grows with the limit
        else:
            self.preconditions(self.directory, label, seconds, remaining=remaining)
        require(self.clock() + RESERVE_AFTER + extra < self.end, 'Series budget exhausted by the guard and ownership checks')
        for path in images:                            # the bytes about to be loaded or compared are the approved ones
            require(path in self.pinned and sha(path) == self.pinned[path], 'Image changed since approval: ' + str(path))
        if (images or reverify) and self.reverify:
            self.reverify()
        if before:                                     # only after every check above; the very next thing is the probe command
            before()
        return label

    def run(self, label, commands, *, images=(), before=None, seconds=COMMAND_SECONDS):
        """One guarded probe command, limited to ``seconds``. Anything unexpected raises HardwareStop and nothing else runs.

        Both series reserves grow by what the limit exceeds the default 30 s, so a longer run never eats the margin.
        """
        require(type(seconds) is int and 1 <= seconds <= capture_m3.MAX_COMMAND_SECONDS, 'Commander limit outside the ceiling')
        require(self.mode != 'watch', 'The watch mode runs no probe command')
        label = self._checks(label, dict(commands=commands, state='checks', limit_seconds=seconds), seconds,
                             max(0, seconds - COMMAND_SECONDS), images=images, reverify=bool(images), before=before)
        output = self.directory / (label + '.log')
        text = self.probe_command(commands, output, seconds)
        check_identity(text)
        evidence = dict(flash_download=flash_download(text)) if commands.startswith('loadfile ') else {}
        self.note(step=label, state='done', **evidence)
        return text

    def identify(self, candidates):
        """Names of the candidate images that the flash holds byte for byte.

        A verify that finds different bytes makes Commander exit non-zero, which is a fault here like any other:
        the run stops. Candidates are therefore checked only until the first one that does not match.
        """
        self.safe(*[path for _, path in candidates])
        limits = [image_seconds(path) for _, path in candidates]   # an image beyond the ceiling stops before any command
        self.run('identity-only', 'q\n')
        held = []
        for (name, path), seconds in zip(candidates, limits):
            text = self.run('candidate-' + name, self.verify_command(path, True), images=[path], seconds=seconds)
            if 'Verify successful' in text:
                held.append(name)
        return held

    def program(self, m5_binary, entry_binary):
        self.safe(m5_binary, entry_binary)
        seconds, entry_seconds = image_seconds(m5_binary), image_seconds(entry_binary)   # before the first probe command
        self.run('identity-only', 'q\n')
        entry = self.run('entry-image', self.verify_command(entry_binary, True), images=[entry_binary], seconds=entry_seconds)
        require('Verify successful' in entry, 'Current firmware differs from the identified entry image; stop before changes')
        if seconds == COMMAND_SECONDS:                 # owner decision 2026-11-10: a 30 s image keeps main's single run
            commands = m3net.image_commands(m5_binary, True)
            if self.capture is not None:
                return self.program_captured_single(m5_binary, commands)
            text = self.run('program-m5', commands, images=[m5_binary])
            require('Verify successful' in text, 'Complete M5 image verification absent')
            return dict(status='M5_PROGRAMMED_VERIFIED_STARTED', entry_image_verified=True)
        if self.capture is not None:
            return self.program_captured(m5_binary, seconds)
        self.program_runs(m5_binary, seconds)
        return dict(status='M5_PROGRAMMED_VERIFIED_STARTED', entry_image_verified=True)

    def program_captured_single(self, binary, commands):
        """main's load step for a 30 s image, with the profile's passive raw capture opened after its checks and before
        its one command (load, verify, reset and go).

        The capture only reads. It never changes what runs: the command is the one above, started once, and nothing here
        starts it again, whatever happens to the capture. A failed command aborts the capture at once (no window wait).
        """
        spec, opened = self.capture, []

        def open_capture():
            label = '%02d-program-m5' % self.step
            require(self.port is not None, 'UART port not owned; no capture and no probe command')
            require(self.clock() + COMMAND_SECONDS + spec.seconds < self.end, 'Capture window does not fit the series budget')
            try:
                opened.append(spec.open(self.port, self.directory / 'capture'))
            except Exception as error:
                raise HardwareStop('UART capture could not be opened; no probe command runs') from error
            self.note(step=label, state='capture-open')

        def abort(error, reason):
            if opened:
                try:
                    opened[0].abort(reason)
                except BaseException as secondary:
                    error.capture_failure = repr(secondary)

        try:
            text = self.run('program-m5', commands, images=[binary], before=open_capture)
        except BaseException as error:
            abort(error, 'start command failed')
            raise
        if 'Verify successful' not in text:
            error = HardwareStop('Complete M5 image verification absent')
            abort(error, 'complete image verification absent')
            raise error
        opened[0].window()                             # the step's done marker is already written; a failure here is a stop
        self.note(step='%02d-program-m5' % self.step, state='capture-closed')
        return dict(status='M5_PROGRAMMED_VERIFIED_STARTED', entry_image_verified=True, capture=opened[0].summary())

    def program_runs(self, binary, seconds, *, before_load=None, before_start=None, phase=None, window=0):
        """Load, full read-back, then reset and go: three bounded runs, each started once, in that order only.

        The start runs only after the read-back reported ``Verify successful``. A failed run ends the mode, as every
        failed command does. ``phase`` (a one-item list) names the run in progress for the capture's abort reason.
        Before the load command the series budget must cover the load run, the reserves that the read-back and start
        steps check before their own checks, and the capture window: a later budget refusal then needs a step whose
        checks overran its own reserve.
        """
        phase = phase if phase is not None else [None]

        def before():
            needed = seconds + RESERVE_BEFORE + max(0, seconds - COMMAND_SECONDS) + RESERVE_BEFORE + window
            require(self.clock() + needed < self.end,
                    'Series budget does not cover the load, read-back and start runs and the capture window; no load')
            if before_load:
                before_load()
        phase[0] = 'load step failed'
        self.run('program-m5-load', 'loadfile "%s", 0x%X\nq\n' % (binary, BASE), images=[binary], before=before, seconds=seconds)
        phase[0] = 'verify step failed'
        text = self.run('program-m5-verify', self.verify_command(binary), images=[binary], seconds=seconds)
        phase[0] = 'complete image verification absent'
        require('Verify successful' in text, 'Complete M5 image verification absent')
        phase[0] = 'start step failed'
        self.run('program-m5-start', m3net.image_commands(binary, False), before=before_start)

    def program_captured(self, binary, seconds):
        """The same three runs, with the profile's passive raw capture opened after the load step's checks and before the
        load command, and kept open through the read-back and the start.

        The capture only reads. It never changes what runs: the runs are the ones above, each started once, and nothing
        here starts one again, whatever happens to the capture. A failed run aborts the capture at once (no window wait).
        A reader that already failed before the start stops the mode before the reset: no unrecorded start.
        """
        spec, opened, phase = self.capture, [], [None]

        def open_capture():
            label = '%02d-program-m5-load' % self.step
            require(self.port is not None, 'UART port not owned; no capture and no probe command')
            try:
                opened.append(spec.open(self.port, self.directory / 'capture'))
            except Exception as error:
                raise HardwareStop('UART capture could not be opened; no probe command runs') from error
            self.note(step=label, state='capture-open')

        def reader_alive():
            if opened[0].failure is not None:
                phase[0] = 'reader failed before the start'
                raise HardwareStop('UART capture reader failed before the start; the image is not started: ' + str(opened[0].failure))

        def abort(error, reason):
            if opened:
                try:
                    opened[0].abort(reason)
                except BaseException as secondary:
                    error.capture_failure = repr(secondary)

        try:
            self.program_runs(binary, seconds, before_load=open_capture, before_start=reader_alive, phase=phase, window=spec.seconds)
        except BaseException as error:
            abort(error, phase[0])
            raise
        opened[0].window()                             # the start step's done marker is already written; a failure here is a stop
        self.note(step='%02d-program-m5-start' % self.step, state='capture-closed')
        return dict(status='M5_PROGRAMMED_VERIFIED_STARTED', entry_image_verified=True, capture=opened[0].summary())

    def restore(self, normal_binary, observe):
        self.safe(normal_binary)
        self.run('identity-only', 'q\n')
        text = self.run('load-normal', 'loadfile "%s", 0x%X\nverifybin "%s", 0x%X\nq\n' % (normal_binary, BASE, normal_binary, BASE), images=[normal_binary])
        require('Verify successful' in text, 'Complete normal image verification absent')
        again = self.run('verify-normal', self.verify_command(normal_binary), images=[normal_binary])
        require('Verify successful' in again, 'Second full normal image verification absent')
        capture = observe(self.port)                   # the capture opens just before the one reset and go
        try:
            self.run('start-normal', m3net.image_commands(normal_binary, False))
        except BaseException as error:
            try:
                capture.finish()
            except BaseException as secondary:
                error.capture_failure = repr(secondary)
            raise
        capture.finish()
        return dict(status='NORMAL_RESTORED_VERIFIED_STARTED', capture=capture.summary())

    def watch(self, guard_window):
        """The read-only watch: one step of checks, then the profile's passive raw capture for its window. No probe command.

        The step runs the same gate sequence as every probe step (``_checks``): series reserves before and after the checks
        (each grown by the whole window), the guard check requiring 90 s plus the window, the ownership inventory (this
        process alone holds the cu node, nobody the tty node, no debugger runs) and the adapter identity, and the manifest
        re-verification. Then, as the last things before the open: the window and the close fit the series budget, and
        ``guard_window(seconds)`` checks that the guard deadline outlives them. The capture opens on the port that
        ``acquire`` owns. Nothing here writes to the board or to the port, and nothing runs again after a failure: a reader
        that fails in the window ends the mode with the bytes read so far, and an interrupt propagates.
        """
        require(self.mode == 'watch', 'Driver.watch runs only in the watch mode')
        spec, opened = self.capture, []
        seconds = spec.seconds

        def open_capture():
            require(self.port is not None, 'UART port not owned; no capture')
            require(self.clock() + seconds + WATCH_CLOSE_SECONDS < self.end, 'Watch window does not fit the series budget')
            guard_window(seconds)                      # the very last check: the guard outlives the window and the close
            try:
                opened.append(spec.open(self.port, self.directory / 'capture'))
            except Exception as error:
                raise HardwareStop('UART capture could not be opened; nothing was read') from error
            self.note(step='%02d-watch' % self.step, state='capture-open')

        try:
            label = self._checks('watch', dict(state='checks', window_seconds=seconds, probe_commands=0), seconds, seconds,
                                 reverify=True, before=open_capture, remaining=GUARD_SECONDS + seconds, subject='watch window')
        except BaseException as error:
            if opened:                                 # only a failure after the open (its record) reaches this
                try:
                    opened[0].abort('watch step failed after the open')
                except BaseException as secondary:
                    error.capture_failure = repr(secondary)
            raise
        opened[0].window()                             # a reader failure or an interrupt here is a stop; nothing reopens
        self.note(step=label, state='capture-closed')
        return dict(status='UART_WATCHED', probe_commands=0, capture=opened[0].summary())


class Observation:
    """Passive UART capture around the final start: exact raw bytes, a fresh boot banner of the normal image."""

    def __init__(self, directory, source, port, receiver_factory, sleep=m3_clock.sleep, seconds=OBSERVE_SECONDS):
        self.directory, self.source, self.port, self.sleep, self.seconds = Path(directory), source, port, sleep, seconds
        self.directory.mkdir(parents=True, exist_ok=False)
        capture_m3.drain(port, self.directory)
        self.receiver = receiver_factory(port, self.directory)
        self.receiver.begin()
        self.matched = False

    def finish(self):
        self.sleep(self.seconds)
        self.receiver.finish()
        text = '\n'.join(line for _, line in self.receiver.rows)
        wanted = 'M3_BOOT source=%s profile=integrated commands=0 fault_default=off' % self.source
        require(text.count('*** Booting Zephyr') >= 1 and wanted in text, 'Fresh normal boot identity absent in the UART capture')
        (self.directory / 'lines.json').write_text(json.dumps(self.receiver.rows, indent=2))
        self.matched = True

    def summary(self):
        return dict(raw='uart-rx.bin', boot_identity_seen=self.matched, seconds=self.seconds,
                    acceptance='offline replay required; this is a raw passive capture')


# ---------------------------------------------------------------------------------------
def load_inputs(args, *, verify_m5, verify_m3, verify_tools):
    """Verify every document and image against the approval; returns what the driver needs."""
    plan = json.loads(args.plan.read_text())
    plan = plan.get('plan', plan)
    approval = json.loads(args.approval.read_text())
    runner.validate_approval(approval, plan)
    require(sha(args.entry_manifest) == plan['entry_manifest_sha256'] and
            sha(args.recovery_manifest) == plan['recovery_manifest_sha256'], 'Entry/recovery manifests differ from approval')
    m5 = verify_m5(args.manifest, plan['manifest_sha256'])
    entry, normal = verify_m3(args.entry_manifest), verify_m3(args.recovery_manifest)
    require(normal['profile'] == 'integrated-normal', 'Restoration image is not the accepted integrated normal role')
    verify_tools()
    return plan, approval, m5, entry, normal


def require_clean_tree(repo):
    status = subprocess.check_output(['git', 'status', '--porcelain'], cwd=repo, text=True)
    require(not status.strip(), 'Physical driver source must be committed and clean')


def write_inputs(repo, directory, args, plan):
    """Record what ran (commit, every tool hash, documents) like the accepted runners."""
    record = dict(runner_commit=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=repo, text=True).strip(),
                  driver_sha256=sha(__file__), scripts={p.name: sha(p) for p in sorted((repo / 'tests/target').glob('*.py'))},
                  plan_sha256=sha(args.plan), approval_sha256=sha(args.approval), manifest_sha256=plan['manifest_sha256'],
                  entry_manifest_sha256=plan['entry_manifest_sha256'], recovery_manifest_sha256=plan['recovery_manifest_sha256'],
                  clock=m3_clock.identity())
    (Path(directory) / 'inputs.json').write_text(json.dumps(record, indent=2) + '\n')
    return record


def consume(parent, mode):
    """One use per mode and evidence parent: a second run needs a new approval and a new directory."""
    marker = Path(parent) / ('m5-driver-%s.consumed' % mode)
    try:
        handle = marker.open('x')
    except FileExistsError:
        raise HardwareStop('Mode already used under this evidence directory; a new approval is required: ' + marker.name)
    handle.write(json.dumps(dict(mode=mode, monotonic=m3_clock.monotonic())))
    handle.close()


DISCONNECT_DIAGNOSTIC = '****** Error: Could not start CPU core. (ErrorCode: -1)'
_DIAGNOSTIC = re.compile(r'(?i)\b(error|failed|failure|could not|cannot|warning|unable|unknown command|timed out)\b')


def accepted_disconnect_diagnostic(text, commands):
    """The one disconnect diagnostic that the repository already documents (docs/m2-physical-verification.md).

    J-Link Commander V9.86a prints ``Could not start CPU core. (ErrorCode: -1)`` while it disconnects after a
    ``loadfile`` and ``verifybin`` command file that ends with a bare ``q``, and still exits 0 (the real run of
    2026-11-08 showed it again). The M3 wrapper stops on every diagnostic. This accepts exactly that text, and
    only when: the command file has a ``verifybin`` and nothing after the last one but ``q`` or ``IsHalted`` and ``q`` (so never the ``r``, ``g`` of a start, nor the ``program`` load run, which has no ``verifybin``: there the diagnostic stays a stop); the last ``verifybin`` reported
    ``Verify successful.`` before the quit; the line is the only error and the only diagnostic text of the log; and
    it is the last non-blank thing in the log, after ``J-Link>q``. Everything else stays a stop.
    """
    script = commands.splitlines()
    last_verify = max((i for i, line in enumerate(script) if line.startswith('verifybin ')), default=None)
    if last_verify is None or script[last_verify + 1:] not in (['q'], ['IsHalted', 'q']):
        return False                                   # nothing may follow the last read-back but an optional IsHalted and the quit
    lines = text.splitlines()
    errors = [i for i, line in enumerate(lines) if line.startswith('****** Error:')]
    if len(errors) != 1 or lines[errors[0]] != DISCONNECT_DIAGNOSTIC or any(line.strip() for line in lines[errors[0] + 1:]):
        return False
    quits = [i for i, line in enumerate(lines) if line == 'J-Link>q']
    verifies = [i for i, line in enumerate(lines) if line.startswith('J-Link>verifybin ')]
    if not quits or not verifies or 'Verify successful.' not in lines[verifies[-1]:quits[-1]]:
        return False
    rest = [line for i, line in enumerate(lines) if i != errors[0] and line.strip() != 'J-Link Commander will now exit on Error']
    return not any(_DIAGNOSTIC.search(line) for line in rest)


def real_parts(args, guard_session):
    """Bind the real accepted helpers. Imported lazily so the tests and `--help` never touch a device."""
    state = dict(owned=False)
    uart, tty = BENCH['uart'], BENCH['uart'].replace('/dev/cu.', '/dev/tty.')

    def preconditions(directory, label, seconds=COMMAND_SECONDS, remaining=None):
        # ``remaining`` is given only by the watch mode (90 s plus its window); every probe step derives it from its limit.
        m3net.guard_check(guard_session, directory, label, guard_seconds(seconds) if remaining is None else remaining, uart=uart)
        if not state['owned']:
            capture_m3.check_owners(uart, directory / (label + '-owners.txt'))
        else:                                          # this process is the only accepted owner; no debugger may run
            import os
            # The port is opened through the cu node only. lsof exits 1 when any named file is not open, so the two
            # nodes are asked one by one: the cu node must be held by this process alone, the tty node by nobody.
            held = capture_m3.inventory(['lsof', '-Fp', uart], directory / (label + '-owners-timeout.json'))
            (directory / (label + '-owners-cu.txt')).write_text('%d\n%s%s' % (held.returncode, held.stdout, held.stderr))
            other = capture_m3.inventory(['lsof', '-Fp', tty], directory / (label + '-owners-timeout.json'))
            (directory / (label + '-owners-tty.txt')).write_text('%d\n%s%s' % (other.returncode, other.stdout, other.stderr))
            require(held.returncode == 0 and not held.stderr and only_process_owns(held.stdout, os.getpid()) and
                    other.returncode == 1 and not other.stdout and not other.stderr, 'Exclusive UART ownership changed')
            result = capture_m3.inventory(['ps', '-axo', 'pid=,comm=,args='], directory / (label + '-probe-timeout.json'))
            require(result.returncode == 0 and not result.stderr and result.stdout.strip() and
                    not re.search(r'(?im)(?:/|\s)(?:JLink\S*|openocd|pyocd(?:\.\S+)?)(?:\s|$)', result.stdout), 'Probe ownership changed')
        check_nic_identity(directory, label, m3net.run_checked)

    def probe(commands, output, seconds):
        # The guard was checked at the start of this step's checks; they, the hashes and a capture open took time since.
        # Check its deadline again, as the very last thing before the run: the run and its reap must end before it.
        deadline = guard_session.get('deadline')
        require(type(deadline) in (int, float) and m3_clock.monotonic() + seconds + GUARD_RUN_MARGIN < deadline,
                'Guard deadline would lapse during the run; no probe command')
        try:
            return capture_m3.commander(runner.PROBE, commands, output, seconds)
        except HardwareStop as error:
            if str(error) != 'Probe diagnostic; stop hardware changes':
                raise
            text = Path(output).read_text(errors='replace')
            if not accepted_disconnect_diagnostic(text, commands):
                raise
            Path(output).with_suffix('.diagnostic-accepted.json').write_text(json.dumps(
                dict(diagnostic=DISCONNECT_DIAGNOSTIC, rule='exit 0, successful read-back, reported after the quit and last in the log'), indent=2))
            return text

    def acquire():
        import serial
        from serial.tools import list_ports
        preconditions(Path(args.evidence), 'acquire')
        capture_m3.select_uart([dict(device=p.device, serial_number=p.serial_number, vid=p.vid, pid=p.pid)
                                for p in list_ports.comports()], uart, BENCH['uart_usb_serial'])
        lock = (Path(args.evidence).parent / ('evkb-%s.lock' % BENCH['probe_serial'])).open('a')
        port = None
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
            port = serial.Serial(uart, 115200, timeout=.02, write_timeout=.25, exclusive=True)
            fcntl.ioctl(port.fileno(), termios.TIOCEXCL)
        except BaseException:
            if port is not None:
                port.close()
            lock.close()
            raise
        close = port.close
        port.close = lambda: (close(), lock.close())
        state['owned'] = True
        return port
    return preconditions, probe, acquire


def build_parser():
    from prepare_m5_network import M5_NAME, PROFILES
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('mode', choices=MODES)
    parser.add_argument('--execute-hardware', action='store_true', required=True)
    for name in ('plan', 'approval', 'manifest', 'entry-manifest', 'recovery-manifest', 'guard-session', 'evidence'):
        parser.add_argument('--' + name, required=True, type=Path)
    parser.add_argument('--candidate', action='append', default=[], type=Path, help='identify: manifest of an image to compare, repeatable')
    parser.add_argument('--profile', choices=sorted(PROFILES), default=M5_NAME,
                        help='image profile of --manifest (image facts only; the safety gates are the same for every profile)')
    return parser


def bind_profile(name):
    """The selected profile and the one manifest verification of it that the driver uses everywhere."""
    from prepare_m5_network import profile_named, verify
    profile = profile_named(name)
    return profile, lambda manifest, digest: verify(manifest, digest, profile)


def capture_spec(profile, mode):
    """The profile's capture for this mode: ``program`` records when the profile asks for it (its own 300 s cap), and
    ``watch`` gets the profile's watch window under the watch cap, or None for a profile without one."""
    if mode == 'watch':
        return WatchSpec(profile.watch_seconds) if profile.watch_seconds else None
    if mode != 'program' or profile.capture == 'none':
        return None
    return CaptureSpec(profile.capture_seconds)


def main(argv=None):
    args = build_parser().parse_args(argv)
    profile, verify_profile = bind_profile(args.profile)
    capture = capture_spec(profile, args.mode)
    require(args.mode != 'watch' or capture is not None, 'Profile %s has no watch window; nothing consumed' % profile.name)
    plan, approval, m5, entry, normal = load_inputs(args, verify_m5=verify_profile,
                                                    verify_m3=capture_m3.verify_manifest,
                                                    verify_tools=capture_m3.verify_jlink_tools)
    candidates = [(m['profile'], m['binary']) for m in (capture_m3.verify_manifest(path) for path in args.candidate)]
    images = {Path(m5['files']['binary']).as_posix(): sha(m5['files']['binary']), Path(entry['binary']).as_posix(): sha(entry['binary']),
              Path(normal['binary']).as_posix(): sha(normal['binary'])}
    images.update({path: sha(path) for _, path in candidates})
    repo = Path(__file__).resolve().parents[2]
    guard_session = json.loads(args.guard_session.read_text())
    preconditions, probe, acquire = real_parts(args, guard_session)

    def reverify():
        verify_profile(args.manifest, plan['manifest_sha256'])
        capture_m3.verify_manifest(args.entry_manifest)
        capture_m3.verify_manifest(args.recovery_manifest)
        require(runner.sha(args.entry_manifest) == plan['entry_manifest_sha256'] and
                runner.sha(args.recovery_manifest) == plan['recovery_manifest_sha256'], 'Recovery artefact changed')
    require_clean_tree(repo)
    consume(Path(args.evidence).parent, args.mode)
    # The watch mode gets a probe that refuses (it has no probe command) and its own series budget (decision D5).
    options = dict(probe=no_probe, series_seconds=watch_series_seconds(capture)) if args.mode == 'watch' else dict(probe=probe)
    driver = Driver(plan, approval, args.mode, args.evidence, preconditions=preconditions, acquire=acquire,
                    images=images, reverify=reverify, capture=capture, **options)
    write_inputs(repo, args.evidence, args, plan)
    with driver:
        if args.mode == 'identify':
            result = dict(status='IDENTIFIED', matching=driver.identify(candidates))
        elif args.mode == 'program':
            result = driver.program(m5['files']['binary'], entry['binary'])
        elif args.mode == 'watch':
            result = driver.watch(lambda seconds: require_guard_window(guard_session, seconds))
        else:
            from capture_m3 import Receiver
            source = normal['source']['git_commit'][:12]
            result = driver.restore(normal['binary'], lambda port: Observation(Path(args.evidence) / 'normal-capture', source, port, Receiver))
    (Path(args.evidence) / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))
    return 0


if __name__ == '__main__':
    sys.exit(main())
