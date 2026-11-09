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
            then the exact M5 BIN in three runs: ``loadfile``; a full ``verifybin``; reset and go
            only after that read-back succeeded
  restore   identity probe, ``loadfile`` + ``verifybin`` of the exact normal BIN, a second
            read-only full ``verifybin``, then reset and go with a passive UART capture open
            (raw bytes kept; its acceptance analysis is a separate offline replay)
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
from m5_bench import BENCH, only_process_owns
from uart_raw_capture import CaptureSpec

BASE = 0x60000000
MODES = ('identify', 'program', 'restore')
OBSERVE_SECONDS = 15
RESERVE_BEFORE, RESERVE_AFTER = 120, 45        # the accepted wrappers' whole-series reserves before and after the guard
COMMAND_SECONDS = capture_m3.COMMAND_SECONDS   # the default limit of one Commander run (30 s)
GUARD_SECONDS = 90                             # the mount guard must run at least this long after each check
# Limit of one run that loads or reads back an image. J-Link programs the flash in 128 KiB ranges. The real stage 1
# runs (fixtures/programmer-split) took at most 4.52 s per range for compare, erase, program and the loader's verify,
# 2.49 s fixed (prepare, restore), and at most about 4.9 s per 128 KiB for a read-back: 10 s each keeps twice that.
RANGE_BYTES, SECONDS_BASE, SECONDS_PER_RANGE = 128 * 1024, 10, 10


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


def flash_download(text):
    """The J-Link flash loader's own report of a ``loadfile``, recorded as evidence; it decides nothing."""
    if re.search(r'(?m)^J-Link: Flash download: Bank 0 @ 0x60000000: Skipped\. Contents already match', text):
        return dict(skipped=True)
    affected = re.search(r'(?m)^J-Link: Flash download: Bank 0 @ 0x60000000: (\d+) ranges? affected \((\d+) bytes\)', text)
    total = re.search(r'(?m)^J-Link: Flash download: Total: ([\d.]+)s \(([^)]*)\)', text)
    if not affected or not total:
        return None
    return dict(skipped=False, affected_ranges=int(affected[1]), affected_bytes=int(affected[2]), total_seconds=float(total[1]),
                parts={name: float(value) for name, value in re.findall(r'(\w+): ([\d.]+)s', total[2])})


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
        require(capture is None or mode == 'program', 'A capture is allowed only for the program mode')
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

    def run(self, label, commands, *, images=(), before=None, seconds=COMMAND_SECONDS):
        """One guarded probe command, limited to ``seconds``. Anything unexpected raises HardwareStop and nothing else runs.

        Both series reserves grow by what the limit exceeds the default 30 s, so a longer run never eats the margin.
        """
        require(type(seconds) is int and 1 <= seconds <= capture_m3.MAX_COMMAND_SECONDS, 'Commander limit outside the ceiling')
        extra = max(0, seconds - COMMAND_SECONDS)
        self.step += 1
        label = '%02d-%s' % (self.step, label)
        require(self.clock() + RESERVE_BEFORE + extra < self.end, 'Insufficient series budget for the next probe command')
        self.note(step=label, commands=commands, state='checks', limit_seconds=seconds)
        self.preconditions(self.directory, label)
        require(self.clock() + RESERVE_AFTER + extra < self.end, 'Series budget exhausted by the guard and ownership checks')
        for path in images:                            # the bytes about to be loaded or compared are the approved ones
            require(path in self.pinned and sha(path) == self.pinned[path], 'Image changed since approval: ' + str(path))
        if images and self.reverify:
            self.reverify()
        if before:                                     # only after every check above; the very next thing is the probe command
            before()
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
        if self.capture is not None:
            return self.program_captured(m5_binary, seconds)
        self.program_runs(m5_binary, seconds)
        return dict(status='M5_PROGRAMMED_VERIFIED_STARTED', entry_image_verified=True)

    def program_runs(self, binary, seconds, *, before_load=None, before_start=None, phase=None):
        """Load, full read-back, then reset and go: three bounded runs, each started once, in that order only.

        The start runs only after the read-back reported ``Verify successful``. A failed run ends the mode, as every
        failed command does. ``phase`` (a one-item list) names the run in progress for the capture's abort reason.
        """
        phase = phase if phase is not None else [None]
        phase[0] = 'load step failed'
        self.run('program-m5-load', 'loadfile "%s", 0x%X\nq\n' % (binary, BASE), images=[binary], before=before_load, seconds=seconds)
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
            require(self.clock() + 2 * seconds + COMMAND_SECONDS + spec.seconds < self.end, 'Capture window does not fit the series budget')
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
            self.program_runs(binary, seconds, before_load=open_capture, before_start=reader_alive, phase=phase)
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

    def preconditions(directory, label):
        m3net.guard_check(guard_session, directory, label, GUARD_SECONDS, uart=uart)
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
    """The profile's capture for this mode: only ``program`` records, and only when the profile asks for it."""
    if mode != 'program' or profile.capture == 'none':
        return None
    return CaptureSpec(profile.capture_seconds)


def main(argv=None):
    args = build_parser().parse_args(argv)
    profile, verify_profile = bind_profile(args.profile)
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
    driver = Driver(plan, approval, args.mode, args.evidence, probe=probe, preconditions=preconditions, acquire=acquire,
                    images=images, reverify=reverify, capture=capture_spec(profile, args.mode))
    write_inputs(repo, args.evidence, args, plan)
    with driver:
        if args.mode == 'identify':
            result = dict(status='IDENTIFIED', matching=driver.identify(candidates))
        elif args.mode == 'program':
            result = driver.program(m5['files']['binary'], entry['binary'])
        else:
            from capture_m3 import Receiver
            source = normal['source']['git_commit'][:12]
            result = driver.restore(normal['binary'], lambda port: Observation(Path(args.evidence) / 'normal-capture', source, port, Receiver))
    (Path(args.evidence) / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))
    return 0


if __name__ == '__main__':
    sys.exit(main())
