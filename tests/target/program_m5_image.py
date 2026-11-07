"""Programming, verification and restoration driver for the M5 stage 1 physical run (hardware safety gate).

Built only on the accepted M3 helpers: J-Link Commander through ``capture_m3.commander``
(pinned executable and DLL hashes, probe serial, firmware-update guard, 30 s limit, no
retry), ``capture_m3_network.image_commands`` (``loadfile``, ``verifybin``, reset, go),
the guard check, the UART and probe ownership inventory and the exact manifest
verification. It never retries, never erases, never touches fuses and never starts a
second probe command after a failed one.

Each subcommand needs ``--execute-hardware`` and the exact plan and approval documents of
``capture_m5_network``; the approval must have the named operations set to true.

  identify  read-only: identity probe, then ``verifybin`` of each candidate image
  program   identity probe, ``verifybin`` of the identified entry image (stop if it differs),
            then the exact M5 BIN: ``loadfile``, ``verifybin``, reset, go
  restore   identity probe, then the exact normal BIN: ``loadfile``, ``verifybin``, reset, go,
            a second read-only full ``verifybin``, and a passive UART capture of the fresh
            boot (raw bytes kept; its acceptance analysis is a separate offline replay)
"""
import argparse
import fcntl
import json
import re
import sys
import termios
from pathlib import Path
import capture_m3
import capture_m3_network as m3net
import capture_m5_network as runner
import m3_clock
from capture_m3 import HardwareStop
from m5_bench import BENCH

BASE = 0x60000000
OPERATIONS = dict(
    identify=('acquire_exclusive_probe_uart_and_nic', 'verify_entry_image'),
    program=('establish_usb_mount_guard', 'acquire_exclusive_probe_uart_and_nic', 'verify_entry_image', 'program_and_start'),
    restore=('establish_usb_mount_guard', 'acquire_exclusive_probe_uart_and_nic', 'restore_identified_normal_image', 'verify_normal_image', 'normal_acceptance_capture'),
)
OBSERVE_SECONDS = 15


def require(condition, message):
    if not condition:
        raise HardwareStop(message)


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
    def __init__(self, plan, approval, mode, directory, *, probe, preconditions, clock=m3_clock.monotonic, series_seconds=900):
        self.mode, self.directory, self.probe_command, self.preconditions, self.clock = mode, Path(directory), probe, preconditions, clock
        runner.validate_approval(approval, plan)
        missing = [name for name in OPERATIONS[mode] if approval['operations'].get(name) is not True]
        require(not missing, 'Operation not approved: ' + ', '.join(missing))
        self.end = clock() + series_seconds
        self.step = 0
        self.log = []
        self.directory.mkdir(parents=True, exist_ok=False)

    def run(self, label, commands):
        """One guarded probe command. Anything unexpected raises HardwareStop and nothing else runs."""
        self.step += 1
        label = '%02d-%s' % (self.step, label)
        require(self.clock() + 120 < self.end, 'Insufficient series budget for the next probe command')
        self.preconditions(self.directory, label)
        text = check_identity(self.probe_command(commands, self.directory / (label + '.log')))
        self.log.append(dict(step=label, commands=commands, monotonic=self.clock()))
        (self.directory / 'operations.json').write_text(json.dumps(self.log, indent=2) + '\n')
        return text

    @staticmethod
    def safe(*paths):
        """Refuse before the first probe command if any image path could alter a Commander script line."""
        require(all('"' not in str(path) and '\n' not in str(path) and str(path).startswith('/') for path in paths), 'Unsafe image path')

    def verify_command(self, path):
        self.safe(path)
        return 'verifybin "%s", 0x%X\nq\n' % (path, BASE)

    def identify(self, candidates):
        """Read-only. Returns the names of the candidate images that the flash holds, byte for byte."""
        self.safe(*[path for _, path in candidates])
        self.run('identity-only', 'q\n')
        held = []
        for name, path in candidates:
            if 'Verify successful' in self.run('candidate-' + name, self.verify_command(path)):
                held.append(name)
        return held

    def program(self, m5_binary, entry_binary):
        self.safe(m5_binary, entry_binary)
        self.run('identity-only', 'q\n')
        entry = self.run('entry-image', self.verify_command(entry_binary))
        require('Verify successful' in entry, 'Current firmware differs from the identified entry image; stop before changes')
        text = self.run('program-m5', m3net.image_commands(m5_binary, True))
        require('Verify successful' in text, 'Complete M5 image verification absent')
        return dict(status='M5_PROGRAMMED_VERIFIED_STARTED', entry_image_verified=True)

    def restore(self, normal_binary, observe):
        self.safe(normal_binary)
        self.run('identity-only', 'q\n')
        capture = observe()                              # opens the UART and the receiver before the reset
        try:
            text = self.run('restore-normal', m3net.image_commands(normal_binary, True))
            require('Verify successful' in text, 'Complete normal image verification absent')
            again = self.run('verify-normal', self.verify_command(normal_binary))
            require('Verify successful' in again, 'Second full normal image verification absent')
        finally:
            capture.finish()
        return dict(status='NORMAL_RESTORED_VERIFIED_STARTED', capture=capture.summary())


class Observation:
    """Passive UART capture around a restore: exact raw bytes, a fresh boot banner of the normal image."""
    def __init__(self, directory, source, open_port, receiver_factory, sleep=m3_clock.sleep, seconds=OBSERVE_SECONDS):
        self.directory, self.source, self.sleep, self.seconds = Path(directory), source, sleep, seconds
        self.directory.mkdir(parents=True, exist_ok=False)
        self.port = open_port()
        try:
            capture_m3.drain(self.port, self.directory)
            self.receiver = receiver_factory(self.port, self.directory)
            self.receiver.begin()
        except BaseException:
            self.port.close()
            raise

    def finish(self):
        try:
            self.sleep(self.seconds)
            self.receiver.finish()
        finally:
            self.port.close()
        text = '\n'.join(line for _, line in self.receiver.rows)
        wanted = 'M3_BOOT source=%s profile=integrated commands=0 fault_default=off' % self.source
        require(text.count('*** Booting Zephyr') >= 1 and wanted in text, 'Fresh normal boot identity absent in the UART capture')
        (self.directory / 'lines.json').write_text(json.dumps(self.receiver.rows, indent=2))
        self.matched = True

    def summary(self):
        return dict(raw='uart-rx.bin', boot_identity_seen=getattr(self, 'matched', False), seconds=self.seconds,
                    acceptance='offline replay required; this is a raw passive capture')


# ---------------------------------------------------------------------------------------
def real_parts(args, plan, guard_session):
    """Bind the real accepted helpers. Imported lazily so the tests and `--help` never touch a device."""
    def preconditions(directory, label):
        m3net.guard_check(guard_session, directory, label, 90, uart=BENCH['uart'])
        capture_m3.check_owners(BENCH['uart'], directory / (label + '-owners.txt'))
        check_nic_identity(directory, label, m3net.run_checked)

    def probe(commands, output):
        return capture_m3.commander(runner.PROBE, commands, output)

    def open_uart():
        import serial
        from serial.tools import list_ports
        capture_m3.select_uart([dict(device=p.device, serial_number=p.serial_number, vid=p.vid, pid=p.pid)
                                for p in list_ports.comports()], BENCH['uart'], BENCH['uart_usb_serial'])
        lock = (Path(args.evidence).parent / ('evkb-%s.lock' % BENCH['probe_serial'])).open('a')
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        port = serial.Serial(BENCH['uart'], 115200, timeout=.02, write_timeout=.25, exclusive=True)
        fcntl.ioctl(port.fileno(), termios.TIOCEXCL)
        close = port.close
        port.close = lambda: (close(), lock.close())
        return port
    return preconditions, probe, open_uart


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('mode', choices=sorted(OPERATIONS))
    parser.add_argument('--execute-hardware', action='store_true', required=True)
    for name in ('plan', 'approval', 'manifest', 'entry-manifest', 'recovery-manifest', 'guard-session', 'evidence'):
        parser.add_argument('--' + name, required=True, type=Path)
    parser.add_argument('--candidate', action='append', default=[], type=Path, help='identify: manifest of an image to compare, repeatable')
    args = parser.parse_args(argv)
    plan = json.loads(args.plan.read_text())
    plan = plan.get('plan', plan)
    approval = json.loads(args.approval.read_text())
    runner.validate_approval(approval, plan)
    require(runner.sha(args.entry_manifest) == plan['entry_manifest_sha256'] and
            runner.sha(args.recovery_manifest) == plan['recovery_manifest_sha256'], 'Entry/recovery manifests differ from approval')
    from prepare_m5_network import verify
    m5 = verify(args.manifest, plan['manifest_sha256'])
    entry, normal = capture_m3.verify_manifest(args.entry_manifest), capture_m3.verify_manifest(args.recovery_manifest)
    require(normal['profile'] in ('normal', 'integrated-normal'), 'Restoration image is not an accepted normal role')
    capture_m3.verify_jlink_tools()
    guard_session = json.loads(args.guard_session.read_text())
    preconditions, probe, open_uart = real_parts(args, plan, guard_session)
    driver = Driver(plan, approval, args.mode, args.evidence, probe=probe, preconditions=preconditions)
    if args.mode == 'identify':
        candidates = [(manifest['profile'], manifest['binary']) for manifest in (capture_m3.verify_manifest(path) for path in args.candidate)]
        result = dict(status='IDENTIFIED', matching=driver.identify(candidates))
    elif args.mode == 'program':
        result = driver.program(m5['files']['binary'], entry['binary'])
    else:
        from capture_m3 import Receiver
        source = normal['source']['git_commit'][:12]
        result = driver.restore(normal['binary'], lambda: Observation(Path(args.evidence) / 'normal-capture', source, open_uart, Receiver))
    (Path(args.evidence) / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))
    return 0


if __name__ == '__main__':
    sys.exit(main())
