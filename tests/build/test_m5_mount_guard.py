"""Replacement mount guard: fake DiskArbitration events, fake host, fake records. No root needed."""
import copy
import json
import os
import plistlib
import stat
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'target'))
import capture_m3_network as m3net
import m3_clock
import m5_mount_guard as guard

SERIAL = guard.SERIAL
UART = guard.UART
BOOT = 'D83ABC6F-EEB9-48AF-9A24-E503879FAE62\n'
IDENTITY = ' 4242 /usr/bin/python3 Sat Nov  7 19:30:00 2026\n'
VOLUME = '/dev/disk4 on /Volumes/RT1060-EVK (msdos, local, nodev, nosuid, noowners, noatime, fskit)\n'
OTHER = '/dev/disk3s1s1 on / (apfs, sealed, local, read-only, journaled)\n'


def tree(serial=SERIAL, vid=0x0d28, pid=0x0204, disks=('disk4',), uarts=(UART,)):
    children = [{'BSD Name': d} for d in disks] + [{'IOCalloutDevice': u} for u in uarts]
    return [{'USB Serial Number': serial, 'idVendor': vid, 'idProduct': pid, 'children': children}]


class Clock:
    def __init__(self, start=None):
        self.value = m3_clock.monotonic() if start is None else start
    def __call__(self):
        return self.value


class Host:
    """Fake host. ``mounted`` tracks whether the volume is mounted."""
    def __init__(self, mounted=True, root=True, ioreg=None, umount_code=0):
        self.root, self.mounted, self.tree, self.umount_code = root, mounted, ioreg or tree(), umount_code
        self.calls, self.boot, self.identity_text = [], BOOT, IDENTITY
    def euid(self):
        return 0 if self.root else 502
    def ioreg(self):
        self.calls.append('ioreg')
        return self.tree
    def mounts(self):
        self.calls.append('mounts')
        return OTHER + (VOLUME if self.mounted else '')
    def umount(self, point):
        self.calls.append('umount ' + point)
        if self.umount_code == 0:
            self.mounted = False
        return self.umount_code, '', 'busy' if self.umount_code else ''
    def identity(self, pid):
        return self.identity_text
    def boot_session(self):
        return self.boot


class DA:
    """Fake DiskArbitration. A mount request is judged by the registered approval handler."""
    def __init__(self, clock, host, identity=None, ignore_approval=False, register_error=None, fail_run=None):
        self.clock, self.host, self.handler = clock, host, None
        self.identity = identity or dict(bsd='disk4', protocol='USB', serial=SERIAL, vid=0x0d28, pid=0x0204, error=None)
        self.ignore_approval, self.register_error, self.fail_run = ignore_approval, register_error, fail_run
        self.pending, self.calls = [], []
    def register_mount_approval(self, handler):
        self.calls.append('register')
        if self.register_error:
            raise self.register_error
        self.handler = handler
    def request_mount(self, bsd):
        self.calls.append('request_mount ' + bsd)
        self.pending.append(bsd)
    def run_for(self, seconds):
        if self.fail_run:
            raise self.fail_run
        self.clock.value += seconds
        while self.pending:
            self.pending.pop()
            if self.ignore_approval or self.handler is None or self.handler(self.identity) is None:
                self.host.mounted = True
    def appeared(self, seconds=3.0):
        return [dict(self.identity), dict(bsd='disk0', protocol='Apple Fabric', serial=None, vid=None, pid=None, error=None)]


class Case(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.session = str(self.root / 'session.json')
        self.log = str(self.root / 'guard.log')
        self.executable = self.root / 'guard-under-test.py'
        self.executable.write_text('print("guard")\n')
        self.clock = Clock()
        self.host = Host()
        self.da = DA(self.clock, self.host)

    def make(self, **options):
        defaults = dict(da=self.da, host=self.host, clock=self.clock, session_path=self.session, log_path=self.log,
                        lifetime=7200, executable=str(self.executable), interpreter=sys.executable, evidence_wait=2)
        defaults.update(options)
        return guard.Guard(**defaults)

    def started(self, **options):
        instance = self.make(**options)
        instance.start()
        return instance

    def log_text(self):
        return Path(self.log).read_text()

    def record(self):
        return json.loads(Path(self.session).read_text())


class Decision(unittest.TestCase):
    def info(self, **changes):
        base = dict(bsd='disk4', protocol='USB', serial=SERIAL, vid=0x0d28, pid=0x0204, error=None)
        base.update(changes)
        return base

    def test_exact_serial_vendor_and_product_are_denied(self):
        self.assertEqual(guard.decide(self.info()), (True, 'exact'))

    def test_each_identity_part_must_match_to_count_as_exact(self):
        for change in (dict(serial='02290000325182c3'), dict(vid=0x1366), dict(pid=0x1020), dict(serial=None)):
            with self.subTest(change=change):
                self.assertEqual(guard.decide(self.info(**change)), (False, ''))

    def test_usb_disk_with_unreadable_identity_is_denied_but_other_disks_are_not(self):
        usb = self.info(serial=None, vid=None, pid=None, error='USB serial not readable')
        self.assertEqual(guard.decide(usb), (True, 'unresolved'))
        for protocol in ('Apple Fabric', 'Virtual Interface', None):
            with self.subTest(protocol=protocol):
                other = dict(usb, protocol=protocol, error='no IOMedia')
                self.assertEqual(guard.decide(other), (False, ''))

    def test_partition_names_map_to_the_whole_disk(self):
        self.assertEqual([guard.whole_disk(n) for n in ('disk4', 'disk4s1', 'disk4s1s2', 'disk40s1', None)],
                         ['disk4', 'disk4', 'disk4', 'disk40', None])

    def test_ioreg_walk_requires_all_three_identity_parts(self):
        self.assertEqual(guard.usb_disks(tree()), (['disk4'], [UART]))
        for bad in (tree(serial='x'), tree(vid=1), tree(pid=1)):
            self.assertEqual(guard.usb_disks(bad), ([], []))

    def test_mount_table_parser_does_not_confuse_disk4_with_disk40(self):
        text = '/dev/disk40s1 on /Volumes/X (msdos)\n/dev/disk4s1 on /Volumes/RT1060-EVK (msdos, local)\n'
        self.assertEqual(guard.mounted_points(text, 'disk4'), [('/dev/disk4s1', '/Volumes/RT1060-EVK')])


class Refusals(Case):
    """Every refusal happens before any file, DiskArbitration object or unmount exists."""

    def nothing_touched(self):
        self.assertFalse(os.path.exists(self.log))
        self.assertFalse(os.path.exists(self.session))
        self.assertEqual(self.da.calls, [])
        self.assertFalse(any(call.startswith('umount') for call in self.host.calls))

    def refuse(self, regex, **options):
        with self.assertRaisesRegex(guard.GuardError, regex):
            self.make(**options).start()
        self.nothing_touched()

    def test_not_root(self):
        self.host.root = False
        self.refuse('must run as root')
        self.assertEqual(self.host.calls, [])            # not even the I/O registry was read

    def test_lifetime_bounds_and_type(self):
        for lifetime in (0, 299, 14401, 7200.5, '7200', None, True):
            with self.subTest(lifetime=lifetime):
                self.refuse('lifetime', lifetime=lifetime)

    def test_paths_must_be_absolute_normalised_new_and_in_an_existing_directory(self):
        self.refuse('absolute', session_path='session.json')
        self.refuse('normalised', log_path=str(self.root / 'x' / '..' / 'g.log'))
        self.refuse('directory does not exist', log_path=str(self.root / 'missing' / 'g.log'))
        Path(self.session).write_text('{}')
        with self.assertRaisesRegex(guard.GuardError, 'already exists'):
            self.make().start()
        self.assertEqual(Path(self.session).read_text(), '{}')   # never overwritten
        self.assertFalse(os.path.exists(self.log))
        self.assertEqual(self.da.calls, [])
        os.unlink(self.session)
        Path(self.log).write_text('')
        with self.assertRaisesRegex(guard.GuardError, 'already exists'):
            self.make().start()
        self.assertEqual(Path(self.log).read_text(), '')     # never overwritten
        self.assertEqual(self.da.calls, [])

    def test_symlink_in_place_of_a_new_file_is_refused(self):
        os.symlink(str(self.root / 'elsewhere'), self.log)
        self.refuse('already exists', log_path=self.log)

    def test_no_daplink_or_wrong_ancestry(self):
        for ioreg in ([], tree(serial='other'), tree(disks=('disk4', 'disk5')), tree(disks=()),
                      tree(uarts=('/dev/cu.usbmodem11302',)), tree(uarts=()), tree(uarts=(UART, '/dev/cu.other'))):
            with self.subTest(ioreg=ioreg):
                self.host.tree = ioreg
                self.refuse('ancestry')


class Start(Case):
    def test_order_register_before_unmount_before_selftest_before_session(self):
        instance = self.started()
        text = self.log_text()
        order = [text.index(word) for word in ('APPROVAL_REGISTERED', 'UNMOUNT', 'SELFTEST', 'READY')]
        self.assertEqual(order, sorted(order))
        self.assertEqual(self.da.calls, ['register', 'request_mount disk4'])
        self.assertIn('umount /Volumes/RT1060-EVK', self.host.calls)
        self.assertFalse(self.host.mounted)
        self.assertNotIn('STOPPED', text)
        instance.retire('test')

    def test_veto_evidence_line_has_the_exact_format_the_repository_check_reads(self):
        self.started()
        self.assertIn('DENIED exact_DAPLink_USB_serial=' + SERIAL + ' disk=disk4', self.log_text())

    def test_not_mounted_volume_needs_no_unmount(self):
        self.host.mounted = False
        self.started()
        self.assertFalse(any(call.startswith('umount') for call in self.host.calls))

    def test_session_record_content_and_permissions(self):
        self.started()
        record = self.record()
        self.assertEqual(record['clock'], m3_clock.CLOCK_NAME)
        self.assertEqual((record['serial'], record['disk']), (SERIAL, '/dev/disk4'))
        self.assertEqual(record['boot_session_uuid'], BOOT)
        self.assertEqual(record['executable'], str(self.executable))
        self.assertEqual(record['sha256'], guard.sha256_file(self.executable))
        self.assertEqual(record['process_identity'], IDENTITY)
        self.assertEqual((record['pid'], record['log'], record['lifetime']), (os.getpid(), self.log, 7200))
        self.assertAlmostEqual(record['deadline'], self.clock() + 7200, delta=1)
        self.assertEqual(stat.S_IMODE(os.stat(self.session).st_mode), 0o644)
        self.assertEqual(stat.S_IMODE(os.stat(self.log).st_mode), 0o644)
        self.assertEqual([p.name for p in self.root.iterdir() if '.tmp' in p.name], [])


class StartFailures(Case):
    """A failed start leaves a STOPPED log, no session record and nothing mounted by us."""

    def fails(self, regex, **options):
        with self.assertRaisesRegex(guard.GuardError, regex):
            self.make(**options).start()
        self.assertIn('STOPPED reason=start_failed', self.log_text())
        self.assertFalse(os.path.exists(self.session))

    def test_unmount_failure(self):
        self.host.umount_code = 1
        self.fails('unmount of /Volumes/RT1060-EVK failed')
        self.assertEqual(self.da.calls, ['register'])           # no self-test mount was requested

    def test_still_mounted_after_a_successful_unmount_report(self):
        self.host.umount = lambda point: (0, '', '')
        self.fails('still mounted')

    def test_no_veto_evidence_when_the_guard_does_not_deny(self):
        self.da.ignore_approval = True
        self.fails('no veto evidence')
        self.assertFalse(self.host.mounted)                     # the stray mount was put back

    def test_denied_request_that_still_mounts_is_refused(self):
        original = self.da.run_for
        def deny_then_mount(seconds):
            original(seconds)
            self.host.mounted = True
        self.da.run_for = deny_then_mount
        self.fails('mounted')

    def test_registration_failure(self):
        self.da.register_error = guard.GuardError('framework refused the callback')
        self.fails('framework refused')
        self.assertFalse(any(call.startswith('umount') for call in self.host.calls))

    def test_session_file_that_appears_meanwhile_is_never_overwritten(self):
        original = self.host.boot_session
        def appear():
            Path(self.session).write_text('FOREIGN')
            return original()
        self.host.boot_session = appear
        with self.assertRaises(FileExistsError):
            self.make().start()
        self.assertEqual(Path(self.session).read_text(), 'FOREIGN')
        self.assertIn('STOPPED reason=start_failed', self.log_text())

    def test_unreadable_boot_session_or_identity(self):
        for name in ('boot_session', 'identity'):
            with self.subTest(name=name):
                self.setUp()
                def broken(*_):
                    raise guard.GuardError('cannot read')
                setattr(self.host, name, broken)
                self.fails('cannot read')


class Approval(Case):
    def info(self, **changes):
        return Decision.info(self, **changes)

    def test_partition_veto_logs_the_whole_disk(self):
        instance = self.make()
        instance.log = os.open(self.log, os.O_WRONLY | os.O_CREAT | os.O_APPEND)
        self.assertIsNotNone(instance.approve(self.info(bsd='disk4s1')))
        os.close(instance.log)
        self.assertIn('DENIED exact_DAPLink_USB_serial=' + SERIAL + ' disk=disk4 mount_target=disk4s1', self.log_text())

    def test_other_disks_are_approved_and_leave_no_denied_line(self):
        instance = self.make()
        instance.log = os.open(self.log, os.O_WRONLY | os.O_CREAT | os.O_APPEND)
        for other in (self.info(serial='something-else', bsd='disk6'), dict(bsd='disk0', protocol='Apple Fabric', serial=None, vid=None, pid=None, error=None)):
            self.assertIsNone(instance.approve(other))
        os.close(instance.log)
        self.assertNotIn('DENIED', self.log_text())
        self.assertEqual(instance.denied, 0)

    def test_unresolved_usb_disk_is_vetoed_without_claiming_exact_evidence(self):
        instance = self.make()
        instance.log = os.open(self.log, os.O_WRONLY | os.O_CREAT | os.O_APPEND)
        self.assertIsNotNone(instance.approve(self.info(serial=None, vid=None, pid=None, error='USB serial not readable', bsd='disk6')))
        os.close(instance.log)
        self.assertIn('DENIED unresolved_usb_identity disk=disk6', self.log_text())
        self.assertNotIn('exact_DAPLink', self.log_text())
        self.assertEqual(instance.denied, 0)       # an unresolved veto is not the veto evidence the check needs

    def test_internal_error_fails_closed(self):
        instance = self.make()
        instance.log = os.open(self.log, os.O_WRONLY | os.O_CREAT | os.O_APPEND)
        self.assertIsNotNone(instance.approve(None))
        os.close(instance.log)
        self.assertIn('DENIED internal_error', self.log_text())


class RunAndRetire(Case):
    def test_runs_until_deadline_then_retires(self):
        instance = self.started(lifetime=300)
        instance.run()
        self.assertGreaterEqual(self.clock(), instance.deadline)
        self.assertTrue(self.log_text().rstrip().endswith('STOPPED reason=deadline'))

    def test_stop_request_retires_with_its_reason(self):
        instance = self.started()
        instance.request_stop('sigterm')
        instance.run()
        self.assertTrue(self.log_text().rstrip().endswith('STOPPED reason=sigterm'))

    def test_error_while_running_retires_and_reraises(self):
        instance = self.started()
        self.da.fail_run = RuntimeError('loop broke')
        with self.assertRaises(RuntimeError):
            instance.run()
        self.assertTrue(self.log_text().rstrip().endswith('STOPPED reason=error'))

    def test_retired_guard_writes_nothing_more(self):
        instance = self.started()
        instance.retire('test')
        size = len(self.log_text())
        instance.say('late')
        instance.retire('again')
        self.assertEqual(len(self.log_text()), size)


class RepositoryCheck(Case):
    """The record and log must satisfy the repository's own guard check, and each check must refuse a defect."""

    def setUp(self):
        super().setUp()
        self.instance = self.started()
        self.mounts = OTHER

    def run_check(self, session=None, log=None, boot=None, identity=None, ioreg=None, mounts=None, remaining=90):
        session = copy.deepcopy(self.record() if session is None else session)
        if log is not None:
            Path(session['log']).write_text(log)
        outputs = {'/usr/sbin/sysctl': BOOT if boot is None else boot, 'ps': IDENTITY if identity is None else identity,
                   'mount': self.mounts if mounts is None else mounts}
        def run_checked(command, destination):
            return outputs[command[0]]
        query = subprocess.CompletedProcess([], 0, plistlib.dumps(tree() if ioreg is None else ioreg).decode(), '')
        with patch.object(m3net, 'inventory', return_value=query), patch.object(m3net, 'run_checked', side_effect=run_checked):
            m3net.guard_check(session, self.root, 'check', remaining, uart=UART)

    def refused(self, **options):
        with self.assertRaises(m3net.HardwareStop):
            self.run_check(**options)

    def test_accepted_while_running(self):
        self.run_check()

    def test_refused_after_retirement(self):
        self.instance.retire('test')
        self.refused()

    def test_refused_when_the_log_has_no_veto_evidence(self):
        self.refused(log='READY\n')

    def test_refused_when_the_log_names_another_disk_or_serial(self):
        self.refused(log='DENIED exact_DAPLink_USB_serial=' + SERIAL + ' disk=disk9\n')
        self.refused(log='DENIED exact_DAPLink_USB_serial=other disk=disk4\n')

    def test_refused_when_the_guard_file_changed(self):
        self.executable.write_text('print("tampered")\n')
        self.refused()

    def test_refused_when_the_deadline_cannot_cover_the_reserve(self):
        self.refused(remaining=7201)

    def test_refused_for_a_different_boot_session(self):
        self.refused(boot='11111111-2222-3333-4444-555555555555\n')

    def test_refused_for_a_different_process_identity(self):
        self.refused(identity=' 4242 /usr/bin/python3 Sat Nov  7 19:31:00 2026\n')

    def test_refused_for_a_different_disk_or_uart(self):
        self.refused(ioreg=tree(disks=('disk5',)))
        self.refused(ioreg=tree(uarts=('/dev/cu.usbmodem11302',)))
        self.refused(ioreg=tree(serial='other'))

    def test_refused_while_the_volume_is_mounted(self):
        self.refused(mounts=OTHER + VOLUME)

    def test_refused_for_a_wrong_clock_or_serial_in_the_record(self):
        record = self.record()
        self.refused(session=dict(record, clock='time.monotonic'))
        self.refused(session=dict(record, serial='other'))


class Command(unittest.TestCase):
    def run_cli(self, *args):
        return subprocess.run([sys.executable, str(Path(guard.__file__)), *args], capture_output=True, text=True)

    @unittest.skipIf(os.geteuid() == 0, 'asserts the non-root refusal')
    def test_start_as_a_normal_user_is_refused_before_any_side_effect(self):
        with tempfile.TemporaryDirectory() as directory:
            result = self.run_cli('start', '--session', directory + '/s.json', '--log', directory + '/l.log')
            self.assertEqual(result.returncode, 2)
            self.assertIn('REFUSED must run as root', result.stderr)
            self.assertEqual(os.listdir(directory), [])

    def test_help_documents_both_modes(self):
        result = self.run_cli('--help')
        self.assertEqual(result.returncode, 0)
        self.assertIn('probe', result.stdout)
        self.assertIn('start', result.stdout)

    def test_probe_reports_decision_and_mount_state_without_any_veto(self):
        host = Host()
        lines = []
        rows = guard.probe(host=host, da=DA(Clock(), host), out=lines.append)
        self.assertEqual([row['decision'] for row in rows], ['deny:exact', 'approve'])
        self.assertTrue(json.loads(lines[0])['ancestry_ok'])
        self.assertEqual(json.loads(lines[2])['daplink_mounted'], [['/dev/disk4', '/Volumes/RT1060-EVK']])
        self.assertFalse(any(call.startswith('umount') for call in host.calls))


@unittest.skipUnless(sys.platform == 'darwin', 'binds the macOS frameworks')
class RealBinding(unittest.TestCase):
    def test_frameworks_bind_and_describe_disks_read_only(self):
        rows = guard.DiskArbitration().appeared(1.0)
        self.assertIsInstance(rows, list)
        for row in rows:
            self.assertEqual(set(row), {'bsd', 'protocol', 'serial', 'vid', 'pid', 'error'})


if __name__ == '__main__':
    unittest.main()
