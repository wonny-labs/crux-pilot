"""Programming/verification/restoration driver: every probe command, UART and host command is a fake."""
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'target'))
import capture_m3
import capture_m3_network as m3net
import capture_m5_network as runner
import program_m5_image as program
from capture_m3 import HardwareStop

GOOD = 'CPUID register: 0x411FC271\nDPIDR: 0x0BD11477\nS/N: 602003106\nCortex-M7\nVTref = 3.300 V\n'
OK = GOOD + 'Verify successful\n'
M5, ENTRY, NORMAL = '/b/m5.bin', '/b/entry.bin', '/b/normal.bin'


def plan_and_approval(reduced=True, **flags):
    plan = runner.plan('a' * 64, [31] if reduced else [31, 32, 33], 'b' * 64, 'c' * 64, through_phase=6 if reduced else None)
    approval = runner.approval_template(plan)
    approval['authorization'] = 'APPROVED_FOR_PHYSICAL_EXECUTION'
    approval['operations'] = {name: flags.get(name, True) for name in approval['operations']}
    return plan, approval


class Case(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name) / 'run'
        self.commands, self.checks, self.responses = [], [], []
        self.now = 1000.

    def probe(self, commands, output):
        self.commands.append(commands)
        response = self.responses.pop(0) if self.responses else GOOD
        if isinstance(response, Exception):
            raise response
        return response

    def preconditions(self, directory, label):
        self.checks.append(label)
        if getattr(self, 'guard_error', None):
            raise self.guard_error

    def driver(self, mode='program', plan_flags=None, **options):
        plan, approval = plan_and_approval(**(plan_flags or {}))
        return program.Driver(plan, approval, mode, self.directory, probe=self.probe, preconditions=self.preconditions,
                              clock=lambda: self.now, **options)


class Approval(Case):
    def test_every_named_operation_must_be_approved(self):
        for mode, operations in program.OPERATIONS.items():
            for name in operations:
                with self.subTest(mode=mode, operation=name):
                    plan, approval = plan_and_approval(**{name: False})
                    with self.assertRaises(ValueError):                  # the approval document itself is refused
                        program.Driver(plan, approval, mode, self.directory / name, probe=self.probe, preconditions=self.preconditions)
        self.assertEqual(self.commands, [])

    def test_unapproved_or_tampered_documents_stop_before_any_directory_or_probe(self):
        plan, approval = plan_and_approval()
        approval['authorization'] = 'NOT_AUTHORIZED'
        with self.assertRaises(ValueError):
            program.Driver(plan, approval, 'program', self.directory, probe=self.probe, preconditions=self.preconditions)
        self.assertFalse(self.directory.exists())
        self.assertEqual((self.commands, self.checks), ([], []))

    def test_operation_missing_from_the_plan_is_not_approved(self):
        # identify needs verify_entry_image; a plan whose approval lacks the key must be refused by name
        plan, approval = plan_and_approval()
        approval['operations'].pop('verify_normal_image')
        with self.assertRaises(ValueError):
            program.Driver(plan, approval, 'restore', self.directory, probe=self.probe, preconditions=self.preconditions)

    def test_evidence_directory_is_never_reused(self):
        self.directory.mkdir()
        with self.assertRaises(FileExistsError):
            self.driver()


class Identity(Case):
    def test_each_identity_part_is_required(self):
        for bad in (GOOD.replace('0x411FC271', '0x411FC270'), GOOD.replace('0x0BD11477', '0x0BD11478'),
                    GOOD.replace('602003106', '000000000'), GOOD.replace('Cortex-M7', 'Cortex-M4'),
                    GOOD.replace('3.300', '1.000'), GOOD.replace('3.300', '5.000'), GOOD.replace('VTref = 3.300 V\n', '')):
            with self.subTest(bad=bad[-30:]):
                with self.assertRaises(HardwareStop):
                    program.check_identity(bad)
        self.assertEqual(program.check_identity(GOOD), GOOD)

    def test_nic_identity_does_not_need_carrier_but_needs_the_reviewed_adapter(self):
        iface = 'ether 00:24:9b:6e:f7:7f\nstatus: inactive\n'
        service = 'Manual Configuration\nIP address: 192.168.77.1\nSubnet mask: 255.255.255.0\nRouter: (null)\nEthernet Address: 00:24:9b:6e:f7:7f\n'
        def run(i, s):
            return lambda command, path: i if command[0] == 'ifconfig' else s
        program.check_nic_identity(Path(self.temp.name), 'x', run(iface, service))
        for bad_iface, bad_service in ((iface.replace('f7:7f', 'f7:7e'), service), (iface + 'ether 00:24:9b:6e:f7:7f\n', service),
                                       (iface, service.replace('Manual Configuration', 'DHCP Configuration')),
                                       (iface, service.replace('192.168.77.1', '192.168.77.10')),
                                       (iface, service.replace('Router: (null)', 'Router: 1.2.3.4')),
                                       (iface, service.replace('f7:7f', 'f7:7e'))):
            with self.subTest(), self.assertRaises(HardwareStop):
                program.check_nic_identity(Path(self.temp.name), 'x', run(bad_iface, bad_service))


class Program(Case):
    def test_exact_sequence_with_preconditions_before_every_probe_command(self):
        self.responses = [GOOD, OK, OK]
        result = self.driver().program(M5, ENTRY)
        self.assertEqual(self.commands, ['q\n', 'verifybin "/b/entry.bin", 0x60000000\nq\n',
                                         'loadfile "/b/m5.bin", 0x60000000\nverifybin "/b/m5.bin", 0x60000000\nr\ng\nq\n'])
        self.assertEqual(self.checks, ['01-identity-only', '02-entry-image', '03-program-m5'])
        self.assertEqual(result['status'], 'M5_PROGRAMMED_VERIFIED_STARTED')
        self.assertEqual(len(json.loads((self.directory / 'operations.json').read_text())), 3)

    def test_unknown_entry_image_stops_before_any_change(self):
        self.responses = [GOOD, GOOD]                                   # the verify did not succeed
        with self.assertRaisesRegex(HardwareStop, 'differs from the identified entry image'):
            self.driver().program(M5, ENTRY)
        self.assertEqual(len(self.commands), 2)
        self.assertFalse(any('loadfile' in c for c in self.commands))

    def test_wrong_identity_stops_after_the_first_command(self):
        self.responses = [GOOD.replace('602003106', '1')]
        with self.assertRaises(HardwareStop):
            self.driver().program(M5, ENTRY)
        self.assertEqual(len(self.commands), 1)

    def test_probe_failure_stops_everything_without_retry(self):
        self.responses = [GOOD, HardwareStop('Probe timeout; state uncertain')]
        with self.assertRaises(HardwareStop):
            self.driver().program(M5, ENTRY)
        self.assertEqual(len(self.commands), 2)

    def test_missing_programming_verification_is_a_failure(self):
        self.responses = [GOOD, OK, GOOD]
        with self.assertRaisesRegex(HardwareStop, 'Complete M5 image verification absent'):
            self.driver().program(M5, ENTRY)

    def test_unsafe_image_paths_never_reach_the_probe(self):
        for entry, image in (('/b/"x.bin', M5), ('/b/x\n.bin', M5), (ENTRY, '/b/"m5.bin'), (ENTRY, 'relative.bin')):
            with self.subTest(entry=entry, image=image):
                self.setUp()
                self.responses = [GOOD, OK]
                with self.assertRaisesRegex(HardwareStop, 'Unsafe image path'):
                    self.driver().program(image, entry)
                self.assertEqual((self.commands, self.checks), ([], []))     # refused before the identity probe too

    def test_guard_or_ownership_failure_precedes_the_probe(self):
        self.guard_error = HardwareStop('Current mount protection unverified')
        with self.assertRaises(HardwareStop):
            self.driver().program(M5, ENTRY)
        self.assertEqual(self.commands, [])

    def test_series_budget_is_enforced(self):
        driver = self.driver(series_seconds=100)
        with self.assertRaisesRegex(HardwareStop, 'budget'):
            driver.program(M5, ENTRY)
        self.assertEqual(self.commands, [])
        self.assertEqual(self.checks, [])


class Identify(Case):
    def test_only_read_only_commands(self):
        self.responses = [GOOD, GOOD, OK, GOOD]
        held = self.driver('identify').identify([('integrated-normal', '/b/a.bin'), ('normal', '/b/b.bin'), ('network', '/b/c.bin')])
        self.assertEqual(held, ['normal'])
        joined = ''.join(self.commands)
        for forbidden in ('loadfile', 'erase', 'r\n', 'g\n', 'w4', 'wreg', 'unlock'):
            self.assertNotIn(forbidden, joined.replace('verifybin', ''), forbidden)
        self.assertEqual(self.commands[0], 'q\n')


class FakeReceiver:
    rows = []
    def __init__(self, port, directory):
        self.finished = False
    def begin(self):
        pass
    def finish(self):
        self.finished = True


class FakePort:
    closed = False
    def read(self, size):
        return b''
    def close(self):
        self.closed = True


class Restore(Case):
    def observation(self, rows, holder):
        def build():
            observation = program.Observation(Path(self.temp.name) / 'cap', 'ec94fbe0123a', lambda: holder.setdefault('port', FakePort()),
                                              lambda port, directory: holder.setdefault('receiver', FakeReceiver(port, directory)),
                                              sleep=lambda seconds: None, seconds=1)
            holder['receiver'].rows = rows
            holder['order'] = list(self.commands)                       # probe commands already issued when the capture opened
            return observation
        return build

    ROWS = [(1., '*** Booting Zephyr OS build v4.2 ***'), (1.1, 'M3_BOOT source=ec94fbe0123a profile=integrated commands=0 fault_default=off')]

    def test_capture_opens_before_the_reset_and_closes_after_both_verifications(self):
        holder = {}
        self.responses = [GOOD, OK, OK]
        result = self.driver('restore').restore(NORMAL, self.observation(self.ROWS, holder))
        self.assertEqual(self.commands, ['q\n', 'loadfile "/b/normal.bin", 0x60000000\nverifybin "/b/normal.bin", 0x60000000\nr\ng\nq\n',
                                         'verifybin "/b/normal.bin", 0x60000000\nq\n'])
        self.assertEqual(holder['order'], ['q\n'])                      # opened after the identity probe, before any change
        self.assertTrue(holder['receiver'].finished and holder['port'].closed)
        self.assertTrue(result['capture']['boot_identity_seen'])

    def test_capture_is_closed_when_programming_fails(self):
        holder = {}
        self.responses = [GOOD, GOOD]
        with self.assertRaisesRegex(HardwareStop, 'Complete normal image verification absent'):
            self.driver('restore').restore(NORMAL, self.observation(self.ROWS, holder))
        self.assertTrue(holder['port'].closed)
        self.assertEqual(len(self.commands), 2)

    def test_second_full_verification_is_required(self):
        holder = {}
        self.responses = [GOOD, OK, GOOD]
        with self.assertRaisesRegex(HardwareStop, 'Second full normal image verification absent'):
            self.driver('restore').restore(NORMAL, self.observation(self.ROWS, holder))

    def test_wrong_or_missing_boot_identity_fails_the_capture(self):
        for rows in ([], [(1., 'M3_BOOT source=other profile=integrated commands=0 fault_default=off')],
                     [(1., '*** Booting Zephyr OS build v4.2 ***')],
                     [(1., '*** Booting Zephyr OS build v4.2 ***'), (1.1, 'M3_BOOT source=ec94fbe0123a profile=integrated commands=1 fault_default=off')]):
            with self.subTest(rows=rows):
                self.setUp()
                holder = {}
                self.responses = [GOOD, OK, OK]
                with self.assertRaisesRegex(HardwareStop, 'Fresh normal boot identity absent'):
                    self.driver('restore').restore(NORMAL, self.observation(rows, holder))
                self.assertTrue(holder['port'].closed)

    def test_receiver_start_failure_releases_the_port(self):
        class Broken(FakeReceiver):
            def begin(self):
                raise ValueError('reader init failed')
        port = FakePort()
        with self.assertRaises(ValueError):
            program.Observation(Path(self.temp.name) / 'cap2', 'x', lambda: port, Broken, sleep=lambda s: None)
        self.assertTrue(port.closed)


class Command(unittest.TestCase):
    def run_cli(self, *args):
        return subprocess.run([sys.executable, str(Path(program.__file__)), *args], capture_output=True, text=True)

    def test_hardware_flag_is_mandatory_and_help_works(self):
        self.assertNotEqual(self.run_cli('program').returncode, 0)
        result = self.run_cli('--help')
        self.assertEqual(result.returncode, 0)
        for word in ('identify', 'program', 'restore', 'never retries'):
            self.assertIn(word, result.stdout)

    def test_missing_documents_stop_before_any_device_access(self):
        with tempfile.TemporaryDirectory() as directory:
            result = self.run_cli('program', '--execute-hardware', *[a for n in ('plan', 'approval', 'manifest', 'entry-manifest', 'recovery-manifest', 'guard-session', 'evidence')
                                                                       for a in ('--' + n, directory + '/missing-' + n)])
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(list(Path(directory).iterdir()), [])

    def test_importing_the_driver_loads_no_device_library(self):
        code = 'import sys; sys.path.insert(0, %r); import program_m5_image; print("serial" in sys.modules)' % str(Path(program.__file__).parent)
        self.assertEqual(subprocess.run([sys.executable, '-c', code], capture_output=True, text=True).stdout.strip(), 'False')


if __name__ == '__main__':
    unittest.main()
