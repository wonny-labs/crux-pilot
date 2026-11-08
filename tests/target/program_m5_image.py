"""Programming, verification and restoration driver for the M5 stage 1 physical run (hardware safety gate).

Built only on the accepted M3 helpers: J-Link Commander through ``capture_m3.commander``
(pinned executable and DLL hashes, probe serial, firmware-update guard, 30 s limit, no
retry), ``capture_m3_network.image_commands`` (``loadfile``, ``verifybin``, reset, go),
the guard check, the UART and probe ownership inventory and the exact manifest
verification. It never retries, never erases, never touches fuses and never starts a
second probe command after a failed one.

Each subcommand needs ``--execute-hardware``, a clean committed tree, and the exact plan and
approval documents of ``capture_m5_network`` (the approval must be complete). It takes the
board lock and the exclusive UART before the first probe command and holds both to the end.
A mode runs once per evidence parent directory.

  identify  identity probe, then ``verifybin`` of each candidate image with ``IsHalted``, until
            the first one that does not match (a mismatch ends the run); changes no flash and
            starts no reset, but may leave the core halted
  program   identity probe, ``verifybin`` of the identified entry image (stop if it differs),
            then the exact M5 BIN: ``loadfile``, ``verifybin``, reset, go
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

BASE = 0x60000000
MODES = ('identify', 'program', 'restore')
OBSERVE_SECONDS = 15
RESERVE_BEFORE, RESERVE_AFTER = 120, 45        # the accepted wrappers' whole-series reserves before and after the guard


def require(condition, message):
    if not condition:
        raise HardwareStop(message)


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


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
                 clock=m3_clock.monotonic, series_seconds=900):
        require(mode in MODES, 'Unknown mode')
        runner.validate_approval(approval, plan)       # also requires every plan operation to be approved
        self.mode, self.directory, self.clock = mode, Path(directory), clock
        self.probe_command, self.preconditions, self.acquire = probe, preconditions, acquire
        self.pinned = dict(images or {})
        self.reverify = reverify
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

    def run(self, label, commands, *, images=()):
        """One guarded probe command. Anything unexpected raises HardwareStop and nothing else runs."""
        self.step += 1
        label = '%02d-%s' % (self.step, label)
        require(self.clock() + RESERVE_BEFORE < self.end, 'Insufficient series budget for the next probe command')
        self.note(step=label, commands=commands, state='checks')
        self.preconditions(self.directory, label)
        require(self.clock() + RESERVE_AFTER < self.end, 'Series budget exhausted by the guard and ownership checks')
        for path in images:                            # the bytes about to be loaded or compared are the approved ones
            require(path in self.pinned and sha(path) == self.pinned[path], 'Image changed since approval: ' + str(path))
        if images and self.reverify:
            self.reverify()
        output = self.directory / (label + '.log')
        text = self.probe_command(commands, output)
        check_identity(text)
        self.note(step=label, state='done')
        return text

    def identify(self, candidates):
        """Names of the candidate images that the flash holds byte for byte.

        A verify that finds different bytes makes Commander exit non-zero, which is a fault here like any other:
        the run stops. Candidates are therefore checked only until the first one that does not match.
        """
        self.safe(*[path for _, path in candidates])
        self.run('identity-only', 'q\n')
        held = []
        for name, path in candidates:
            text = self.run('candidate-' + name, self.verify_command(path, True), images=[path])
            if 'Verify successful' in text:
                held.append(name)
        return held

    def program(self, m5_binary, entry_binary):
        self.safe(m5_binary, entry_binary)
        self.run('identity-only', 'q\n')
        entry = self.run('entry-image', self.verify_command(entry_binary, True), images=[entry_binary])
        require('Verify successful' in entry, 'Current firmware differs from the identified entry image; stop before changes')
        text = self.run('program-m5', m3net.image_commands(m5_binary, True), images=[m5_binary])
        require('Verify successful' in text, 'Complete M5 image verification absent')
        return dict(status='M5_PROGRAMMED_VERIFIED_STARTED', entry_image_verified=True)

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


def real_parts(args, guard_session):
    """Bind the real accepted helpers. Imported lazily so the tests and `--help` never touch a device."""
    state = dict(owned=False)
    uart, tty = BENCH['uart'], BENCH['uart'].replace('/dev/cu.', '/dev/tty.')

    def preconditions(directory, label):
        m3net.guard_check(guard_session, directory, label, 90, uart=uart)
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

    def probe(commands, output):
        return capture_m3.commander(runner.PROBE, commands, output)

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


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('mode', choices=MODES)
    parser.add_argument('--execute-hardware', action='store_true', required=True)
    for name in ('plan', 'approval', 'manifest', 'entry-manifest', 'recovery-manifest', 'guard-session', 'evidence'):
        parser.add_argument('--' + name, required=True, type=Path)
    parser.add_argument('--candidate', action='append', default=[], type=Path, help='identify: manifest of an image to compare, repeatable')
    args = parser.parse_args(argv)
    from prepare_m5_network import verify
    plan, approval, m5, entry, normal = load_inputs(args, verify_m5=verify, verify_m3=capture_m3.verify_manifest,
                                                    verify_tools=capture_m3.verify_jlink_tools)
    candidates = [(m['profile'], m['binary']) for m in (capture_m3.verify_manifest(path) for path in args.candidate)]
    images = {Path(m5['files']['binary']).as_posix(): sha(m5['files']['binary']), Path(entry['binary']).as_posix(): sha(entry['binary']),
              Path(normal['binary']).as_posix(): sha(normal['binary'])}
    images.update({path: sha(path) for _, path in candidates})
    repo = Path(__file__).resolve().parents[2]
    guard_session = json.loads(args.guard_session.read_text())
    preconditions, probe, acquire = real_parts(args, guard_session)

    def reverify():
        verify(args.manifest, plan['manifest_sha256'])
        capture_m3.verify_manifest(args.entry_manifest)
        capture_m3.verify_manifest(args.recovery_manifest)
        require(runner.sha(args.entry_manifest) == plan['entry_manifest_sha256'] and
                runner.sha(args.recovery_manifest) == plan['recovery_manifest_sha256'], 'Recovery artefact changed')
    require_clean_tree(repo)
    consume(Path(args.evidence).parent, args.mode)
    driver = Driver(plan, approval, args.mode, args.evidence, probe=probe, preconditions=preconditions, acquire=acquire,
                    images=images, reverify=reverify)
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
