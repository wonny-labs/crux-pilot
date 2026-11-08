"""Programming/verification/restoration driver: every probe command, UART and host command is a fake."""
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'target'))
import capture_m3
import capture_m5_network as runner
import program_m5_image as program
from capture_m3 import HardwareStop

GOOD = 'CPUID register: 0x411FC271\nDPIDR: 0x0BD11477\nS/N: 602003106\nCortex-M7\nVTref = 3.300 V\n'
OK = GOOD + 'Verify successful\n'
MISMATCH = GOOD + 'Verify failed @ address 0x60000040\n'


def plan_and_approval(entry='b' * 64, recovery='c' * 64, **flags):
    plan = runner.plan('a' * 64, [31], entry, recovery, through_phase=6)
    approval = runner.approval_template(plan)
    approval['authorization'] = 'APPROVED_FOR_PHYSICAL_EXECUTION'
    approval['operations'] = {name: flags.get(name, True) for name in approval['operations']}
    return plan, approval


class Port:
    def __init__(self):
        self.closed = False
    def close(self):
        self.closed = True


class Case(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.directory = self.root / 'run'
        self.m5, self.entry, self.normal = (str(self.root / name) for name in ('m5.bin', 'entry.bin', 'normal.bin'))
        for path in (self.m5, self.entry, self.normal):
            Path(path).write_bytes(path.encode())
        self.commands, self.checks, self.responses, self.events = [], [], [], []
        self.now, self.reverified, self.port = 1000., [], Port()
        self.guard_error = None

    def probe(self, commands, output):
        self.commands.append(commands)
        self.events.append('probe')
        response = self.responses.pop(0) if self.responses else GOOD
        if isinstance(response, Exception):
            raise response
        if response is MISMATCH:                       # Commander exits non-zero on a verify mismatch; commander() wraps it
            Path(output).write_text(response)
            raise HardwareStop('Probe command or its evidence failed; stop hardware changes')
        Path(output).write_text(response)
        return response

    def preconditions(self, directory, label):
        self.checks.append(label)
        self.now += getattr(self, 'check_seconds', 0)
        if self.guard_error:
            raise self.guard_error

    def acquire(self):
        self.events.append('acquire')
        return self.port

    def driver(self, mode='program', **options):
        plan, approval = plan_and_approval()
        pinned = {path: program.sha(path) for path in (self.m5, self.entry, self.normal)}
        defaults = dict(probe=self.probe, preconditions=self.preconditions, acquire=self.acquire, images=pinned,
                        reverify=lambda: self.reverified.append(1), clock=lambda: self.now)
        defaults.update(options)
        return program.Driver(plan, approval, mode, self.directory, **defaults)


class Approval(Case):
    def test_unapproved_or_tampered_documents_stop_before_any_directory_or_probe(self):
        plan, approval = plan_and_approval(program_and_start=False)
        with self.assertRaises(ValueError):
            program.Driver(plan, approval, 'program', self.directory, probe=self.probe, preconditions=self.preconditions)
        plan, approval = plan_and_approval()
        approval['authorization'] = 'NOT_AUTHORIZED'
        with self.assertRaises(ValueError):
            program.Driver(plan, approval, 'program', self.directory, probe=self.probe, preconditions=self.preconditions)
        self.assertFalse(self.directory.exists())
        self.assertEqual((self.commands, self.checks), ([], []))

    def test_unknown_mode_and_reused_directory_are_refused(self):
        with self.assertRaises(HardwareStop):
            self.driver('erase')
        self.directory.mkdir()
        with self.assertRaises(FileExistsError):
            self.driver()


class Identity(Case):
    def test_each_identity_part_is_required(self):
        for bad in (GOOD.replace('0x411FC271', '0x411FC270'), GOOD.replace('0x0BD11477', '0x0BD11478'),
                    GOOD.replace('602003106', '000000000'), GOOD.replace('Cortex-M7', 'Cortex-M4'),
                    GOOD.replace('3.300', '1.000'), GOOD.replace('3.300', '5.000'), GOOD.replace('VTref = 3.300 V\n', '')):
            with self.subTest(bad=bad[-30:]), self.assertRaises(HardwareStop):
                program.check_identity(bad)
        self.assertEqual(program.check_identity(GOOD), GOOD)

    def test_nic_identity_does_not_need_carrier_but_needs_the_reviewed_adapter(self):
        iface = 'ether 00:24:9b:6e:f7:7f\nstatus: inactive\n'
        service = 'Manual Configuration\nIP address: 192.168.77.1\nSubnet mask: 255.255.255.0\nRouter: (null)\nEthernet Address: 00:24:9b:6e:f7:7f\n'
        def run(i, s):
            return lambda command, path: i if command[0] == 'ifconfig' else s
        program.check_nic_identity(self.root, 'x', run(iface, service))
        for bad_iface, bad_service in ((iface.replace('f7:7f', 'f7:7e'), service), (iface + 'ether 00:24:9b:6e:f7:7f\n', service),
                                       (iface, service.replace('Manual Configuration', 'DHCP Configuration')),
                                       (iface, service.replace('192.168.77.1', '192.168.77.10')),
                                       (iface, service.replace('Router: (null)', 'Router: 1.2.3.4')),
                                       (iface, service.replace('f7:7f', 'f7:7e'))):
            with self.subTest(), self.assertRaises(HardwareStop):
                program.check_nic_identity(self.root, 'x', run(bad_iface, bad_service))


class Ownership(Case):
    def test_lock_and_uart_are_taken_before_the_first_probe_command_and_released(self):
        self.responses = [GOOD, OK, OK]
        with self.driver() as driver:
            driver.program(self.m5, self.entry)
        self.assertEqual(self.events[0], 'acquire')
        self.assertEqual(self.events.count('acquire'), 1)
        self.assertTrue(self.port.closed)

    def test_acquisition_failure_runs_no_probe_command(self):
        def broken():
            raise HardwareStop('UART already owned')
        with self.assertRaises(HardwareStop), self.driver(acquire=broken):
            self.fail('must not enter')
        self.assertEqual(self.commands, [])

    def test_port_is_released_and_failure_recorded_when_a_step_fails(self):
        self.responses = [GOOD.replace('602003106', '1')]
        with self.assertRaises(HardwareStop), self.driver() as driver:
            driver.program(self.m5, self.entry)
        self.assertTrue(self.port.closed)
        failure = json.loads((self.directory / 'failure.json').read_text())
        self.assertEqual((failure['mode'], failure['steps'], failure['automatic_retry']), ('program', 1, False))

    def test_every_mode_holds_the_port_through_all_commands(self):
        self.responses = [GOOD, GOOD, OK]
        with self.driver('identify') as driver:
            driver.identify([('integrated-normal', self.normal), ('network', self.m5)])
        self.assertFalse(self.port.closed and self.events.index('acquire') > self.events.index('probe'))
        self.assertTrue(self.port.closed)


class Program(Case):
    def test_exact_sequence_with_preconditions_before_every_probe_command(self):
        self.responses = [GOOD, OK, OK]
        with self.driver() as driver:
            result = driver.program(self.m5, self.entry)
        self.assertEqual(self.commands, ['q\n', 'verifybin "%s", 0x60000000\nIsHalted\nq\n' % self.entry,
                                         'loadfile "%s", 0x60000000\nverifybin "%s", 0x60000000\nr\ng\nq\n' % (self.m5, self.m5)])
        self.assertEqual(self.checks, ['01-identity-only', '02-entry-image', '03-program-m5'])
        self.assertEqual(result['status'], 'M5_PROGRAMMED_VERIFIED_STARTED')
        states = [row['state'] for row in json.loads((self.directory / 'operations.json').read_text())]
        self.assertEqual(states.count('checks'), 3)
        self.assertEqual(self.reverified, [1, 1])                     # before the entry compare and before the load

    def test_failure_record_write_error_still_releases_the_port(self):
        self.responses = [GOOD.replace('602003106', '1')]
        with self.assertRaises(OSError), patch.object(program.Driver, 'fail', side_effect=OSError('disk full')):
            with self.driver() as driver:
                driver.program(self.m5, self.entry)
        self.assertTrue(self.port.closed)

    def test_unknown_entry_image_stops_before_any_change(self):
        for response in (MISMATCH, GOOD):                              # a mismatch as Commander reports it, and a run without the success line
            with self.subTest(response=response is MISMATCH):
                self.setUp()
                self.responses = [GOOD, response]
                with self.assertRaises(HardwareStop), self.driver() as driver:
                    driver.program(self.m5, self.entry)
                self.assertEqual(len(self.commands), 2)
                self.assertFalse(any('loadfile' in c for c in self.commands))

    def test_other_probe_failure_propagates_unchanged(self):
        self.responses = [GOOD, HardwareStop('Probe timeout; state uncertain; no further hardware changes')]
        with self.assertRaisesRegex(HardwareStop, 'Probe timeout'), self.driver() as driver:
            driver.program(self.m5, self.entry)
        self.assertEqual(len(self.commands), 2)

    def test_wrong_identity_stops_after_the_first_command(self):
        self.responses = [GOOD.replace('602003106', '1')]
        with self.assertRaises(HardwareStop), self.driver() as driver:
            driver.program(self.m5, self.entry)
        self.assertEqual(len(self.commands), 1)

    def test_missing_programming_verification_is_a_failure(self):
        self.responses = [GOOD, OK, GOOD]
        with self.assertRaisesRegex(HardwareStop, 'Complete M5 image verification absent'), self.driver() as driver:
            driver.program(self.m5, self.entry)

    def test_unsafe_image_paths_never_reach_the_probe(self):
        for entry, image in (('/b/"x.bin', self.m5), ('/b/x\n.bin', self.m5), (self.entry, '/b/"m5.bin'), (self.entry, 'relative.bin')):
            with self.subTest(entry=entry, image=image):
                self.setUp()
                with self.assertRaisesRegex(HardwareStop, 'Unsafe image path'), self.driver() as driver:
                    driver.program(image, entry)
                self.assertEqual((self.commands, self.checks), ([], []))

    def test_guard_or_ownership_failure_precedes_the_probe(self):
        self.guard_error = HardwareStop('Current mount protection unverified')
        with self.assertRaises(HardwareStop), self.driver() as driver:
            driver.program(self.m5, self.entry)
        self.assertEqual(self.commands, [])

    def test_budget_before_the_guard_and_after_it(self):
        with self.assertRaisesRegex(HardwareStop, 'budget'), self.driver(series_seconds=120) as driver:
            driver.program(self.m5, self.entry)
        self.assertEqual((self.commands, self.checks), ([], []))
        self.setUp()
        self.check_seconds = 90                                        # the checks themselves use up the reserve (90 + 45 >= 130)
        with self.assertRaisesRegex(HardwareStop, 'exhausted by the guard'), self.driver(series_seconds=130) as driver:
            driver.program(self.m5, self.entry)
        self.assertEqual(self.commands, [])
        self.setUp()
        self.check_seconds = 84                                        # leaves exactly enough (84 + 45 < 130)
        self.responses = [GOOD]
        with self.driver(series_seconds=130) as driver:
            driver.run('identity-only', 'q\n')
        self.assertEqual(len(self.commands), 1)

    def test_an_image_that_changed_after_approval_is_never_loaded(self):
        self.responses = [GOOD, GOOD, OK]
        with self.assertRaisesRegex(HardwareStop, 'Image changed since approval'), self.driver() as driver:
            driver.run('identity-only', 'q\n')
            Path(self.m5).write_bytes(b'tampered')
            driver.program(self.m5, self.entry)
        self.assertFalse(any('loadfile' in c for c in self.commands))
        self.assertFalse(any('tampered' in c for c in self.commands))

    def test_an_unpinned_image_is_refused(self):
        other = self.root / 'other.bin'
        other.write_bytes(b'x')
        with self.assertRaisesRegex(HardwareStop, 'Image changed since approval'), self.driver() as driver:
            driver.run('identity-only', 'q\n')
            driver.run('x', 'verifybin "%s", 0x60000000\nq\n' % other, images=[str(other)])

    def test_manifest_reverification_failure_stops_before_the_load(self):
        def broken():
            raise HardwareStop('Recovery artefact changed')
        with self.assertRaisesRegex(HardwareStop, 'Recovery artefact changed'), self.driver(reverify=broken) as driver:
            driver.run('x', 'verifybin "%s", 0x60000000\nq\n' % self.entry, images=[self.entry])
        self.assertEqual(self.commands, [])


class Identify(Case):
    def test_only_read_only_commands_and_matches_are_listed(self):
        self.responses = [GOOD, OK, OK]
        with self.driver('identify') as driver:
            held = driver.identify([('integrated-normal', self.normal), ('normal', self.entry)])
        self.assertEqual(held, ['integrated-normal', 'normal'])
        # Compare the Commander words only: image paths are quoted and contain random temporary names.
        import re
        joined = re.sub(r'"[^"]*"', '""', ''.join(self.commands)).replace('verifybin', '')
        for forbidden in ('loadfile', 'erase', 'r\n', 'g\n', 'w4', 'wreg', 'unlock'):
            self.assertNotIn(forbidden, joined, forbidden)
        self.assertTrue(all('IsHalted' in c for c in self.commands[1:]))
        self.assertEqual(self.commands[0], 'q\n')

    def test_a_mismatch_or_any_fault_ends_the_run_after_that_command(self):
        for response in (MISMATCH, HardwareStop('Probe timeout; state uncertain')):
            with self.subTest(response=str(response)[:20]):
                self.setUp()
                self.responses = [GOOD, response]
                with self.assertRaises(HardwareStop), self.driver('identify') as driver:
                    driver.identify([('a', self.normal), ('b', self.entry), ('c', self.m5)])
                self.assertEqual(len(self.commands), 2)                  # nothing ran after the failed command


class FakeReceiver:
    rows = []
    def __init__(self, port, directory):
        self.finished, self.began = False, False
    def begin(self):
        self.began = True
    def finish(self):
        self.finished = True


class Restore(Case):
    BANNER = (1., '*** Booting Zephyr OS build v4.2 ***')
    BOOT = (1.1, 'M3_BOOT source=ec94fbe0123a profile=integrated commands=0 fault_default=off')

    def observation(self, rows, holder):
        def build(port):
            holder['order'] = list(self.commands)
            receiver = holder.setdefault('receiver', FakeReceiver(port, None))
            receiver.rows = rows
            with patch.object(capture_m3, 'drain') as drain:
                observation = program.Observation(self.root / 'cap', 'ec94fbe0123a', port, lambda p, d: receiver, sleep=lambda s: None, seconds=1)
            holder['drain'] = drain.called
            return observation
        return build

    def test_order_the_capture_opens_before_the_one_reset_and_go(self):
        holder = {}
        self.responses = [GOOD, OK, OK, GOOD]
        with self.driver('restore') as driver:
            result = driver.restore(self.normal, self.observation([self.BANNER, self.BOOT], holder))
        load = 'loadfile "%s", 0x60000000\nverifybin "%s", 0x60000000\nq\n' % (self.normal, self.normal)
        self.assertEqual(self.commands, ['q\n', load, 'verifybin "%s", 0x60000000\nq\n' % self.normal, 'r\ng\nq\n'])
        self.assertEqual(len(holder['order']), 3)                      # opened after both verifications, before the start
        self.assertTrue(holder['receiver'].began and holder['receiver'].finished and holder['drain'])
        self.assertTrue(result['capture']['boot_identity_seen'])
        self.assertTrue(self.port.closed)

    def test_failed_programming_never_opens_the_capture_or_starts_the_image(self):
        holder = {}
        self.responses = [GOOD, GOOD]
        with self.assertRaisesRegex(HardwareStop, 'Complete normal image verification absent'), self.driver('restore') as driver:
            driver.restore(self.normal, self.observation([], holder))
        self.assertNotIn('receiver', holder)
        self.assertFalse(any(c.startswith('r\n') for c in self.commands))

    def test_second_full_verification_is_required_before_the_start(self):
        holder = {}
        self.responses = [GOOD, OK, GOOD]
        with self.assertRaisesRegex(HardwareStop, 'Second full normal image verification absent'), self.driver('restore') as driver:
            driver.restore(self.normal, self.observation([], holder))
        self.assertNotIn('receiver', holder)

    def test_start_failure_closes_the_capture_and_keeps_the_primary_error(self):
        holder = {}
        self.responses = [GOOD, OK, OK, HardwareStop('Probe timeout; state uncertain')]
        with self.assertRaisesRegex(HardwareStop, 'Probe timeout') as caught, self.driver('restore') as driver:
            driver.restore(self.normal, self.observation([], holder))
        self.assertTrue(holder['receiver'].finished)
        self.assertIn('Fresh normal boot identity absent', caught.exception.capture_failure)

    def test_wrong_or_missing_boot_identity_fails_the_capture(self):
        wrong_source = (1.1, self.BOOT[1].replace('ec94fbe0123a', 'other'))
        commands_on = (1.1, self.BOOT[1].replace('commands=0', 'commands=1'))
        for rows in ([], [wrong_source], [self.BANNER], [self.BANNER, wrong_source], [self.BANNER, commands_on], [self.BOOT]):
            with self.subTest(rows=rows):
                self.setUp()
                holder = {}
                self.responses = [GOOD, OK, OK, GOOD]
                with self.assertRaisesRegex(HardwareStop, 'Fresh normal boot identity absent'), self.driver('restore') as driver:
                    driver.restore(self.normal, self.observation(rows, holder))
                self.assertTrue(self.port.closed)


class Inputs(Case):
    def files(self, **changes):
        manifests = {name: self.root / (name + '.json') for name in ('m5', 'entry', 'recovery')}
        for name, path in manifests.items():
            path.write_text(name)
        plan, approval = plan_and_approval(program.sha(manifests['entry']), program.sha(manifests['recovery']))
        plan['manifest_sha256'] = 'a' * 64
        (self.root / 'plan.json').write_text(json.dumps(plan))
        (self.root / 'approval.json').write_text(json.dumps(approval))
        return SimpleNamespace(plan=self.root / 'plan.json', approval=self.root / 'approval.json', manifest=manifests['m5'],
                               entry_manifest=manifests['entry'], recovery_manifest=manifests['recovery'])

    def load(self, args, normal_profile='integrated-normal', tools=None):
        calls = []
        verify_m5 = lambda path, digest: dict(files=dict(binary=self.m5))
        verify_m3 = lambda path: dict(profile=normal_profile if path == args.recovery_manifest else 'integrated-normal', binary=self.normal)
        def verify_tools():
            calls.append('tools')
            if tools:
                raise tools
        with patch.object(runner, 'validate_approval', lambda a, p: None):
            return program.load_inputs(args, verify_m5=verify_m5, verify_m3=verify_m3, verify_tools=verify_tools), calls

    def test_all_documents_images_and_tools_are_verified(self):
        args = self.files()
        _, calls = self.load(args)
        self.assertEqual(calls, ['tools'])

    def test_manifest_hash_differences_role_and_tool_identity_each_refuse(self):
        args = self.files()
        args.entry_manifest.write_text('changed')
        with self.assertRaisesRegex(HardwareStop, 'differ from approval'):
            self.load(args)
        args = self.files()
        args.recovery_manifest.write_text('changed')
        with self.assertRaisesRegex(HardwareStop, 'differ from approval'):
            self.load(args)
        args = self.files()
        for profile in ('normal', 'network', 'probe', 'integrated-probe'):
            with self.subTest(profile=profile), self.assertRaisesRegex(HardwareStop, 'integrated normal role'):
                self.load(args, normal_profile=profile)
        with self.assertRaisesRegex(HardwareStop, 'J-Link tool identity changed'):
            self.load(args, tools=HardwareStop('J-Link tool identity changed; no probe access'))

    def test_dirty_tree_is_refused_and_inputs_are_recorded_for_a_clean_one(self):
        args = self.files()
        plan = json.loads(args.plan.read_text())
        repo = Path(program.__file__).resolve().parents[2]
        with patch.object(program.subprocess, 'check_output', return_value=' M tests/target/x.py\n'):
            with self.assertRaisesRegex(HardwareStop, 'committed and clean'):
                program.require_clean_tree(repo)
        with patch.object(program.subprocess, 'check_output', return_value=''):
            program.require_clean_tree(repo)
        with patch.object(program.subprocess, 'check_output', return_value='abc123\n'):
            self.directory.mkdir()
            record = program.write_inputs(repo, self.directory, args, plan)
        self.assertEqual(json.loads((self.directory / 'inputs.json').read_text()), record)
        self.assertEqual(record['driver_sha256'], program.sha(program.__file__))
        self.assertIn('program_m5_image.py', record['scripts'])
        self.assertIn('capture_m5_network.py', record['scripts'])

    def test_each_mode_runs_once_per_evidence_parent(self):
        program.consume(self.root, 'program')
        with self.assertRaisesRegex(HardwareStop, 'already used'):
            program.consume(self.root, 'program')
        program.consume(self.root, 'restore')


class RealParts(Case):
    """The real wiring with every host call faked: ownership before and after this process holds the UART."""

    def wired(self):
        import os
        from types import ModuleType
        import capture_m3_network as m3net
        serial = ModuleType('serial'); serial.__path__ = []
        port = Port(); port.fileno = lambda: 7
        serial.Serial = lambda *a, **k: port
        tools = ModuleType('serial.tools'); tools.__path__ = []
        ports = ModuleType('serial.tools.list_ports'); ports.comports = lambda: []
        serial.tools = tools; tools.list_ports = ports
        self.modules = {'serial': serial, 'serial.tools': tools, 'serial.tools.list_ports': ports}
        self.owner_calls, self.inventories = [], {}
        iface = 'ether 00:24:9b:6e:f7:7f\nstatus: inactive\n'
        service = 'Manual Configuration\nIP address: 192.168.77.1\nSubnet mask: 255.255.255.0\nRouter: (null)\nEthernet Address: 00:24:9b:6e:f7:7f\n'
        def inventory(command, output):
            return self.inventories[command[0]]
        patches = [patch.dict(sys.modules, self.modules),
                   patch.object(m3net, 'guard_check', lambda *a, **k: self.owner_calls.append('guard')),
                   patch.object(m3net, 'run_checked', lambda command, path: iface if command[0] == 'ifconfig' else service),
                   patch.object(capture_m3, 'check_owners', lambda *a: self.owner_calls.append('owners')),
                   patch.object(capture_m3, 'select_uart', lambda *a: None),
                   patch.object(capture_m3, 'inventory', inventory),
                   patch.object(program.fcntl, 'flock', lambda *a: None), patch.object(program.fcntl, 'ioctl', lambda *a: None)]
        for item in patches:
            item.start()
            self.addCleanup(item.stop)
        self.inventories['lsof'] = subprocess.CompletedProcess([], 0, 'p%d\n' % os.getpid(), '')
        self.inventories['ps'] = subprocess.CompletedProcess([], 0, '  1 launchd /sbin/launchd\n', '')
        self.directory.mkdir()
        return program.real_parts(SimpleNamespace(evidence=self.directory), {}), port

    def test_unowned_before_and_self_owned_after_acquisition(self):
        (preconditions, probe, acquire), port = self.wired()
        acquire()
        self.assertEqual(self.owner_calls, ['guard', 'owners'])         # the first check requires that nobody owns the UART
        preconditions(self.directory, 'next')
        self.assertEqual(self.owner_calls, ['guard', 'owners', 'guard'])  # afterwards only the process itself may own it
        acquire_port = port
        acquire_port.close()

    def test_a_foreign_owner_or_a_debugger_after_acquisition_stops(self):
        (preconditions, probe, acquire), port = self.wired()
        acquire()
        import os
        self.inventories['lsof'] = subprocess.CompletedProcess([], 0, 'p%d\np999\n' % os.getpid(), '')
        with self.assertRaisesRegex(HardwareStop, 'ownership changed'):
            preconditions(self.directory, 'a')
        self.inventories['lsof'] = subprocess.CompletedProcess([], 0, '', '')
        with self.assertRaisesRegex(HardwareStop, 'ownership changed'):
            preconditions(self.directory, 'b')
        self.inventories['lsof'] = subprocess.CompletedProcess([], 0, 'p%d\n' % os.getpid(), '')
        self.inventories['ps'] = subprocess.CompletedProcess([], 0, ' 42 JLinkExe /Applications/SEGGER/JLink/JLinkExe -device x\n', '')
        with self.assertRaisesRegex(HardwareStop, 'Probe ownership changed'):
            preconditions(self.directory, 'c')

    def test_acquisition_releases_the_lock_when_the_port_cannot_be_opened(self):
        (preconditions, probe, acquire), port = self.wired()
        sys.modules['serial'].Serial = lambda *a, **k: (_ for _ in ()).throw(OSError('busy'))
        with self.assertRaises(OSError):
            acquire()
        self.assertEqual(self.owner_calls, ['guard', 'owners'])


class Command(unittest.TestCase):
    def run_cli(self, *args):
        return subprocess.run([sys.executable, str(Path(program.__file__)), *args], capture_output=True, text=True)

    def test_hardware_flag_is_mandatory_and_help_works(self):
        self.assertNotEqual(self.run_cli('program').returncode, 0)
        result = self.run_cli('--help')
        self.assertEqual(result.returncode, 0)
        for word in ('identify', 'program', 'restore', 'never retries', 'clean committed tree'):
            self.assertIn(word, result.stdout)

    def test_missing_documents_stop_before_any_device_access(self):
        with tempfile.TemporaryDirectory() as directory:
            names = ('plan', 'approval', 'manifest', 'entry-manifest', 'recovery-manifest', 'guard-session', 'evidence')
            result = self.run_cli('program', '--execute-hardware', *[a for n in names for a in ('--' + n, directory + '/missing-' + n)])
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(list(Path(directory).iterdir()), [])

    def test_importing_the_driver_loads_no_device_library(self):
        code = 'import sys; sys.path.insert(0, %r); import program_m5_image; print("serial" in sys.modules)' % str(Path(program.__file__).parent)
        self.assertEqual(subprocess.run([sys.executable, '-c', code], capture_output=True, text=True).stdout.strip(), 'False')


if __name__ == '__main__':
    unittest.main()
