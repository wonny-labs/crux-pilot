"""Reviewed bench identity block and the reduced non-qualifying M5 run.

Software only: every device, process and host command is faked or never reached.
"""
import copy
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'target'))
import capture_m3_network as m3net
import capture_m5_network as runner
import m5_bench
import m5_capture_adapter as adapter
import m5_series

ABI = {'ETIMEDOUT': 116, 'ECONNRESET': 104, 'ENETDOWN': 115, 'ENETUNREACH': 114}
SOURCE = '123456789abc'
REDUCED_OPERATIONS = ['establish_usb_mount_guard', 'acquire_exclusive_probe_uart_and_nic', 'verify_entry_image',
                      'program_and_start', 'qualification_preflight_1', 'qualification_reset_1',
                      'restore_identified_normal_image', 'verify_normal_image', 'normal_acceptance_capture']


class Clock:
    value = 1.
    def __call__(self):
        return self.value


def approve(plan):
    approval = runner.approval_template(plan)
    approval['authorization'] = 'APPROVED_FOR_PHYSICAL_EXECUTION'
    approval['operations'] = {key: True for key in approval['operations']}
    return approval


class BenchIdentity(unittest.TestCase):
    def test_reviewed_block_is_exact(self):
        # Values come from the 2026-11-07 Phase A inventory of the current bench.
        self.assertEqual(m5_bench.BENCH, dict(
            nic_interface='en13', nic_model='AX88179', nic_mac='00:24:9b:6e:f7:7f',
            service='AX88179 3', host='192.168.77.1', target='192.168.77.2', prefix=24,
            uart='/dev/cu.usbmodem11402', uart_usb_serial='02290000325182c300000000000000000000000097969905',
            probe_serial='602003106'))

    def test_plan_binds_every_identity_field(self):
        plan = runner.plan('a' * 64, [31, 32, 33], 'b' * 64, 'c' * 64)
        self.assertEqual(plan['nic'], dict(interface='en13', model='AX88179', mac='00:24:9b:6e:f7:7f',
                                           service='AX88179 3', host='192.168.77.1', target='192.168.77.2', prefix=24))
        self.assertEqual((plan['uart_device'], plan['probe_serial'], plan['uart_usb_serial']),
                         ('/dev/cu.usbmodem11402', '602003106', m5_bench.BENCH['uart_usb_serial']))
        for mutate in (lambda a: a.update(uart_device='/dev/cu.usbmodem11302'),
                       lambda a: a['nic'].update(mac='00:0a:cd:41:89:cc'),
                       lambda a: a['nic'].update(interface='en5'),
                       lambda a: a['nic'].update(service='AX88179')):
            approval = approve(plan)
            mutate(approval)
            with self.subTest(), self.assertRaises(ValueError):
                runner.validate_approval(approval, plan)

    def test_old_bench_interface_is_refused(self):
        text = 'ether 00:0a:cd:41:89:cc\ninet 192.168.77.1 netmask 0xffffff00\nstatus: active\n'
        with self.assertRaises(ValueError):
            adapter.parse_interface(text)
        good = adapter.parse_interface(text.replace('00:0a:cd:41:89:cc', '00:24:9b:6e:f7:7f'))
        self.assertEqual(good, dict(source='192.168.77.1', active=True))
        route = '   route to: 192.168.77.2\ndestination: 192.168.77.0\n       mask: 255.255.255.0\n  interface: %s\n      flags: <UP,DONE,STATIC>\n'
        adapter.parse_direct_route(route % 'en13', '192.168.77.1')
        with self.assertRaises(ValueError):
            adapter.parse_direct_route(route % 'en5', '192.168.77.1')

    def test_nic_probe_queries_the_reviewed_interface_only(self):
        with tempfile.TemporaryDirectory() as directory, patch.object(adapter.subprocess, 'Popen') as popen:
            probe = adapter.NICProbe(Path(directory), Clock())
            probe.start()
            probe.close()
            self.assertEqual(popen.call_args.args[0], ['ifconfig', 'en13'])

    def test_series_replay_refuses_the_old_interface(self):
        from test_m5_series import complete
        series = complete()
        controls = series['boots'][0]['controls']
        next(c for c in controls if c['event'] == 'host_down')['interface'] = 'en5'
        with self.assertRaises(ValueError):
            m5_series.validate_series(series, SOURCE, ABI)


class LeaseIdentity(unittest.TestCase):
    def lease(self, directory, uart=None):
        lease = adapter.M3UARTLease({}, Path(directory)) if uart is None else adapter.M3UARTLease({}, Path(directory), uart)
        return lease

    def test_guard_and_route_checks_receive_the_reviewed_identity(self):
        outputs = {'ifconfig': 'ether 00:24:9b:6e:f7:7f\ninet 192.168.77.1 netmask 0xffffff00\nstatus: active\n',
                   'route': '   route to: 192.168.77.2\ndestination: 192.168.77.0\n       mask: 255.255.255.0\n  interface: en13\n      flags: <UP,DONE,STATIC>\n',
                   'networksetup': 'Manual Configuration\nIP address: 192.168.77.1\nSubnet mask: 255.255.255.0\nRouter: (null)\nIPv6: Automatic\nEthernet Address: 00:24:9b:6e:f7:7f\n'}
        calls = []
        def run_checked(command, path):
            calls.append(command)
            path.write_text(outputs[command[0]])
            return outputs[command[0]]
        import capture_m3
        with tempfile.TemporaryDirectory() as directory, \
                patch.object(m3net, 'guard_check') as guard, \
                patch.object(m3net, 'check_route', return_value=True) as route, \
                patch.object(m3net, 'run_checked', side_effect=run_checked), \
                patch.object(capture_m3, 'check_owners'):
            (Path(directory) / 'lease-1-route.txt').write_text(outputs['route'])
            self.lease(directory).verify('acquire_exclusive_probe_uart_and_nic')
        self.assertEqual(guard.call_args.kwargs, dict(uart='/dev/cu.usbmodem11402'))
        self.assertEqual(route.call_args.kwargs, dict(interface='en13', mac='00:24:9b:6e:f7:7f'))
        self.assertIn(['ifconfig', 'en13'], calls)
        self.assertIn(['networksetup', '-getinfo', 'AX88179 3'], calls)

    def test_service_with_old_mac_or_missing_static_address_is_refused(self):
        import capture_m3
        good = 'Manual Configuration\nIP address: 192.168.77.1\nSubnet mask: 255.255.255.0\nRouter: (null)\nEthernet Address: 00:24:9b:6e:f7:7f\n'
        iface = 'ether 00:24:9b:6e:f7:7f\ninet 192.168.77.1 netmask 0xffffff00\nstatus: active\n'
        route = '   route to: 192.168.77.2\ndestination: 192.168.77.0\n       mask: 255.255.255.0\n  interface: en13\n      flags: <UP,DONE,STATIC>\n'
        for bad in (good.replace('00:24:9b:6e:f7:7f', '00:0a:cd:41:89:cc'), good.replace('Manual Configuration', 'DHCP Configuration'),
                    good.replace('IP address: 192.168.77.1', 'IP address: 192.168.77.10'),
                    good.replace('Router: (null)', 'Router: 192.168.77.254'), good.replace('Ethernet Address: 00:24:9b:6e:f7:7f', 'Ethernet Address: (null)')):
            def run_checked(command, path, bad=bad):
                text = {'ifconfig': iface, 'route': route, 'networksetup': bad}[command[0]]
                path.write_text(text)
                return text
            with self.subTest(bad=bad), tempfile.TemporaryDirectory() as directory, \
                    patch.object(m3net, 'guard_check'), patch.object(m3net, 'check_route', return_value=True), \
                    patch.object(m3net, 'run_checked', side_effect=run_checked), patch.object(capture_m3, 'check_owners'):
                (Path(directory) / 'lease-1-route.txt').write_text(route)
                with self.assertRaises(ValueError):
                    self.lease(directory).verify('x')

    def test_missing_or_stale_guard_stops_before_any_other_query(self):
        import capture_m3
        with tempfile.TemporaryDirectory() as directory, \
                patch.object(m3net, 'guard_check', side_effect=m3net.HardwareStop('Current mount protection unverified')), \
                patch.object(m3net, 'check_route') as route, patch.object(m3net, 'run_checked') as run_checked, \
                patch.object(capture_m3, 'check_owners') as owners:
            with self.assertRaises(m3net.HardwareStop):
                self.lease(directory).verify('x')
            route.assert_not_called(); run_checked.assert_not_called(); owners.assert_not_called()

    def test_empty_guard_session_is_refused_by_the_real_guard_check(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaises(m3net.HardwareStop):
                m3net.guard_check({}, Path(directory), 'none', uart='/dev/cu.usbmodem11402')

    def test_lease_refuses_a_different_uart_before_opening_anything(self):
        from types import ModuleType
        def never(*_, **__):
            raise AssertionError('serial port must not open')
        serial = ModuleType('serial'); serial.__path__ = []; serial.Serial = never
        tools = ModuleType('serial.tools'); tools.__path__ = []
        ports = ModuleType('serial.tools.list_ports'); ports.comports = never
        serial.tools = tools; tools.list_ports = ports
        modules = {'serial': serial, 'serial.tools': tools, 'serial.tools.list_ports': ports}
        with tempfile.TemporaryDirectory() as directory, patch.dict(sys.modules, modules):
            for uart in ('/dev/cu.usbmodem11302', '/dev/cu.usbmodem1102'):
                lease = self.lease(directory, uart)
                lease.verify = lambda *_: self.fail('verify must not run')
                with self.subTest(uart=uart), self.assertRaises(ValueError):
                    lease()


class M3DefaultsUnchanged(unittest.TestCase):
    """The shared M3 helpers keep their accepted M3 bench defaults."""
    def session(self, directory):
        executable = Path(directory) / 'guard'
        executable.write_bytes(b'guard')
        log = Path(directory) / 'guard.log'
        log.write_text('DENIED exact_DAPLink_USB_serial=' + m3net.USB_SERIAL + ' disk=disk4\n')
        return dict(clock=m3net.time.CLOCK_NAME, serial=m3net.USB_SERIAL, disk='/dev/disk4', executable=str(executable),
                    sha256=m3net.sha(executable), deadline=m3net.time.monotonic() + 5000, pid=123,
                    process_identity='123 guard start', log=str(log))

    def check(self, directory, device, **options):
        tree = [{'USB Serial Number': m3net.USB_SERIAL, 'idVendor': 0x0d28, 'idProduct': 0x0204,
                 'children': [{'BSD Name': 'disk4'}, {'IOCalloutDevice': device}]}]
        query = subprocess.CompletedProcess([], 0, m3net.plistlib.dumps(tree).decode(), '')
        with patch.object(m3net, 'inventory', return_value=query), \
                patch.object(m3net, 'run_checked', side_effect=['123 guard start', '/dev/disk5 on /Volumes/OTHER (x)']):
            m3net.guard_check(self.session(directory), Path(directory), 'g', **options)

    def test_uart_selector_is_exact_in_both_directions(self):
        with tempfile.TemporaryDirectory() as directory:
            self.check(directory, '/dev/cu.usbmodem11302')                               # M3 default
            with self.assertRaises(m3net.HardwareStop):
                self.check(directory, '/dev/cu.usbmodem11402')                           # M3 default refuses the new device
            self.check(directory, '/dev/cu.usbmodem11402', uart='/dev/cu.usbmodem11402')
            with self.assertRaises(m3net.HardwareStop):
                self.check(directory, '/dev/cu.usbmodem11302', uart='/dev/cu.usbmodem11402')

    def test_route_selector_defaults_and_overrides(self):
        old = 'ether 00:0a:cd:41:89:cc\ninet 192.168.77.1 netmask 0xffffff00\nstatus: active\n'
        new = old.replace('00:0a:cd:41:89:cc', '00:24:9b:6e:f7:7f')
        route = 'interface: %s\n'
        def run(interface_text, route_text, **options):
            with patch.object(m3net, 'run_checked', side_effect=[interface_text, route_text]):
                return m3net.check_route(Path('/unused'), 'r', **options)
        self.assertTrue(run(old, route % 'en5'))
        with self.assertRaises(m3net.HardwareStop):
            run(new, route % 'en13')
        self.assertTrue(run(new, route % 'en13', interface='en13', mac='00:24:9b:6e:f7:7f'))
        with self.assertRaises(m3net.HardwareStop):
            run(old, route % 'en5', interface='en13', mac='00:24:9b:6e:f7:7f')


class ReducedPlan(unittest.TestCase):
    def plan(self, through=6, nonces=(31,)):
        return runner.plan('a' * 64, list(nonces), 'b' * 64, 'c' * 64, through_phase=through)

    def test_reduced_plan_is_labelled_exact_and_has_no_cable_operations(self):
        plan = self.plan()
        self.assertEqual(plan['status'], 'SOFTWARE_PREPARED_REDUCED_NON_QUALIFYING_HARDWARE_NOT_AUTHORIZED')
        self.assertEqual(plan['reduced'], dict(through_phase=6, qualifying=False, satisfies_m5_series=False,
                                               boots=1, excluded_phases=[7, 8, 9], cable_operations=False))
        self.assertEqual(plan['operations'], REDUCED_OPERATIONS)
        self.assertEqual([c['sequence'] for c in plan['boots'][0]['commands']], [1, 2, 3, 4, 5, 6])
        self.assertEqual(len(plan['boots']), 1)
        self.assertFalse(any(c['template'].startswith('L ') for c in plan['boots'][0]['commands']))
        self.assertEqual(plan['evidence']['control_events'], [])
        self.assertTrue(plan['procedure'][0].startswith('REDUCED NON-QUALIFYING'))

    def test_reduced_plan_inputs_are_refused_when_inexact(self):
        for through, nonces in ((7, (31,)), (8, (31,)), (1, (31,)), (0, (31,)), (6, (31, 32)), (6, (31, 32, 33)), (6, ()), (6, (0,)), (2.0, (31,))):
            with self.subTest(through=through, nonces=nonces), self.assertRaises(ValueError):
                self.plan(through, nonces)
        for through in (2, 3, 4, 5, 6):
            self.assertEqual(self.plan(through)['operations'], REDUCED_OPERATIONS)
            self.assertEqual(len(self.plan(through)['boots'][0]['commands']), through)

    def test_full_plan_is_unchanged_by_the_new_option(self):
        full = runner.plan('a' * 64, [31, 32, 33], 'b' * 64, 'c' * 64)
        self.assertNotIn('reduced', full)
        self.assertEqual(len(full['operations']), 19)
        self.assertEqual(full, runner.plan('a' * 64, [31, 32, 33], 'b' * 64, 'c' * 64, through_phase=None))

    def test_approval_binds_reduced_operation_list_exactly(self):
        plan = self.plan()
        approval = approve(plan)
        self.assertEqual(sorted(approval['operations']), sorted(REDUCED_OPERATIONS))
        self.assertEqual(approval['reduced'], plan['reduced'])
        runner.validate_approval(approval, plan)
        full = runner.plan('a' * 64, [31, 32, 33], 'b' * 64, 'c' * 64)
        with self.assertRaises(ValueError):
            runner.validate_approval(approve(full), plan)          # 19-operation approval against a reduced plan
        with self.assertRaises(ValueError):
            runner.validate_approval(approval, full)               # reduced approval against the full plan
        extra = copy.deepcopy(approval); extra['operations']['boot_1_cable_disconnect'] = True
        with self.assertRaises(ValueError):
            runner.validate_approval(extra, plan)
        missing = copy.deepcopy(approval); del missing['operations']['qualification_reset_1']
        with self.assertRaises(ValueError):
            runner.validate_approval(missing, plan)
        for mutate in (lambda p: p['reduced'].update(qualifying=True), lambda p: p['reduced'].update(through_phase=7),
                       lambda p: p['reduced'].update(satisfies_m5_series=True), lambda p: p.pop('reduced'),
                       lambda p: p['operations'].append('boot_1_cable_disconnect'),
                       lambda p: p['boots'][0]['commands'].append(copy.deepcopy(p['boots'][0]['commands'][0]))):
            tampered = copy.deepcopy(plan); mutate(tampered)
            with self.subTest(), self.assertRaises(ValueError):
                runner.validate_approval(approve(tampered), tampered)
        unapproved = copy.deepcopy(approval); unapproved['operations']['program_and_start'] = False
        with self.assertRaises(ValueError):
            runner.validate_approval(unapproved, plan)


class ReducedSupervisor(unittest.TestCase):
    def drive(self, through):
        """Feed the first through_phase phases of a complete fixture boot until the core finishes."""
        from test_m5_series import complete
        boot = complete()['boots'][0]
        supervisor = runner.Supervisor(boot['nonce'], SOURCE, 0, ABI, through_phase=through)
        timeline = [(e['monotonic'], 'target', e['record']) for e in boot['uart']]
        timeline += [(e['monotonic'], 'write', e) for e in boot['writes']]
        timeline += [(e['monotonic'], 'peer', e) for rows in boot['peers'].values() for e in rows if e['event'] == 'ready']
        arm = None
        issued = []
        for now, kind, row in sorted(timeline, key=lambda e: e[0]):
            if supervisor.state == 'finished':
                break
            if kind == 'target':
                if row['event'] == 'BOOT':
                    arm = supervisor.boot(row['source'], now)
                else:
                    supervisor.target(row, now)
            elif kind == 'write':
                if row['line'].startswith('A '):
                    self.assertEqual(row['line'], arm)
                else:
                    issued.append(supervisor.next_command(now))
            elif row['phase'] <= through:
                if supervisor.peer_phase is not None and supervisor.stopped_phase != supervisor.peer_phase:
                    supervisor.peer_stopped(supervisor.peer_phase, True, now)
                supervisor.peer_ready(row, now)
        return supervisor, issued

    def test_finishes_after_the_ready_that_follows_the_last_reduced_phase(self):
        for through in (2, 6):
            with self.subTest(through=through):
                supervisor, issued = self.drive(through)
                self.assertEqual(supervisor.state, 'finished')
                self.assertEqual([line.split()[2] for line in issued], [str(n) for n in range(1, through + 1)])
                self.assertFalse(any(line.startswith('L ') for line in issued))
                with self.assertRaises(ValueError):
                    supervisor.next_command(1e6)
                self.assertTrue(supervisor.aborted)

    def test_through_phase_is_validated_and_full_default_is_unchanged(self):
        for bad in (0, 1, 7, 9, 10, 6.0, True, '6'):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                runner.Supervisor(31, SOURCE, 0, ABI, through_phase=bad)
        self.assertIsNone(runner.Supervisor(31, SOURCE, 0, ABI).through_phase)


class ReducedCapture(unittest.TestCase):
    def files(self, directory, plan, approval):
        root = Path(directory)
        (root / 'plan.json').write_text(json.dumps(plan))
        (root / 'approval.json').write_text(json.dumps(approval))
        return SimpleNamespace(execute_hardware=True, perform_approved_reset=True, plan=root / 'plan.json',
                               approval=root / 'approval.json', manifest=root / 'm', entry_manifest=root / 'e',
                               recovery_manifest=root / 'r', guard_session=root / 'g.json', evidence=root / 'ev', boot=1)

    def test_flag_and_plan_must_agree_before_any_device_or_manifest_access(self):
        reduced = runner.plan('a' * 64, [31], 'b' * 64, 'c' * 64, through_phase=6)
        full = runner.plan('a' * 64, [31, 32, 33], 'b' * 64, 'c' * 64)
        cases = [(full, 6), (reduced, None), (reduced, 5)]
        for plan, flag in cases:
            with self.subTest(flag=flag), tempfile.TemporaryDirectory() as directory:
                args = self.files(directory, plan, approve(plan))
                args.through_phase = flag
                # Manifest files do not exist: reaching them would raise FileNotFoundError, not ValueError.
                with self.assertRaises(ValueError):
                    runner.capture_approved_boot(args)
                self.assertFalse(args.evidence.exists())

    def test_reduced_capture_accepts_only_boot_one(self):
        reduced = runner.plan('a' * 64, [31], 'b' * 64, 'c' * 64, through_phase=6)
        for boot in (2, 3):
            with self.subTest(boot=boot), tempfile.TemporaryDirectory() as directory:
                args = self.files(directory, reduced, approve(reduced))
                args.through_phase, args.boot = 6, boot
                # Manifest files do not exist: reaching them would raise FileNotFoundError, not ValueError.
                with self.assertRaises(ValueError):
                    runner.capture_approved_boot(args)
                self.assertFalse(args.evidence.exists())

    def test_cli_exposes_through_phase_for_plan_and_capture(self):
        output = subprocess.run([sys.executable, str(Path(runner.__file__)), 'capture', '--help'], capture_output=True, text=True)
        self.assertEqual(output.returncode, 0)
        self.assertIn('--through-phase', output.stdout)
        output = subprocess.run([sys.executable, str(Path(runner.__file__)), 'plan', '--help'], capture_output=True, text=True)
        self.assertIn('--through-phase', output.stdout)

    def test_result_and_series_session_are_labelled_non_qualifying(self):
        plan = runner.plan('a' * 64, [31], 'b' * 64, 'c' * 64, through_phase=6)
        result = runner.boot_result(runner.Supervisor(31, SOURCE, 0, ABI, through_phase=6), plan.get('reduced'))
        self.assertEqual(result['status'], 'REDUCED_BOOT_PROTOCOL_COMPLETE_NON_QUALIFYING')
        self.assertIs(result['qualifying'], False)
        self.assertIs(result['satisfies_m5_series'], False)
        self.assertEqual(result['through_phase'], 6)
        full = runner.boot_result(runner.Supervisor(31, SOURCE, 0, ABI), None)
        self.assertEqual(full['status'], 'BOOT_PROTOCOL_COMPLETE_PENDING_RAW_REPLAY')
        self.assertNotIn('qualifying', full)
        with tempfile.TemporaryDirectory() as directory:
            path, record = runner.series_session(directory, plan, 1, 10., 'host-boot-A')
            self.assertEqual(record['reduced'], plan['reduced'])
            with self.assertRaisesRegex(ValueError, 'only boot 1'):
                runner.series_session(directory, plan, 2, 20., 'host-boot-A')       # no second reduced boot
            # Same manifest and nonce, only the reduced marker differs: the marker alone must refuse it.
            looks_full = {key: value for key, value in plan.items() if key != 'reduced'}
            with self.assertRaisesRegex(ValueError, 'foreign capture session'):
                runner.series_session(directory, looks_full, 1, 20., 'host-boot-A')
        with tempfile.TemporaryDirectory() as directory:
            path, record = runner.series_session(directory, looks_full, 1, 10., 'host-boot-A')
            self.assertNotIn('reduced', record)
            with self.assertRaisesRegex(ValueError, 'foreign capture session'):
                runner.series_session(directory, plan, 1, 20., 'host-boot-A')       # and a full session never accepts a reduced plan

    def test_replay_refuses_a_reduced_bundle(self):
        with tempfile.TemporaryDirectory() as directory:
            index = Path(directory) / 'index.json'
            index.write_text(json.dumps(dict(schema=1, source=SOURCE, clock=m5_series.CLOCK, boots=[], reduced=dict(through_phase=6))))
            # The message pins the refusal to the reduced-marker check, not to the empty boot list.
            with self.assertRaisesRegex(ValueError, 'Reduced non-qualifying evidence'):
                runner.replay_bundle(index, SOURCE, ABI)


class ReducedAdapter(unittest.TestCase):
    def make(self, root, boot=1):
        from test_m5_adapter import Port, Receiver, FakeNIC
        plan = runner.plan('a' * 64, [31], 'b' * 64, 'c' * 64, through_phase=6)
        port = Port()
        live = adapter.CaptureAdapter(plan, approve(plan), boot, root / 'boot', lambda: port, receiver_factory=Receiver,
                                      nic_factory=FakeNIC, software_only=True, clock=Clock())
        return live, port

    def test_only_boot_one_exists(self):
        with tempfile.TemporaryDirectory() as directory:
            for boot in (2, 3, 4):
                # ValueError by message: an IndexError from nonces[boot-1] would mean the explicit check is gone.
                with self.subTest(boot=boot), self.assertRaisesRegex(ValueError, 'Invalid fresh boot number'):
                    self.make(Path(directory) / str(boot), boot)

    def test_any_operator_cable_receipt_is_refused(self):
        with tempfile.TemporaryDirectory() as directory:
            live, _ = self.make(Path(directory))
            with live.operator_path.open('a') as output:
                output.write(json.dumps(dict(event='cable_disconnect_receipt', monotonic=.5)) + '\n')
            with self.assertRaises(ValueError):
                live.poll(0)
            live.close()


class OwnerInventory(unittest.TestCase):
    """`lsof -Fp` prints an f<fd> line after every p<pid> line; the first real run stopped on that."""
    REAL = 'p%d\nf12\n'

    def test_only_this_process_with_its_descriptor_lines_is_accepted(self):
        import os
        self.assertTrue(m5_bench.only_process_owns(self.REAL % os.getpid(), os.getpid()))
        self.assertTrue(m5_bench.only_process_owns('p%d\nf12\nf13\n' % os.getpid(), os.getpid()))   # cu and tty both open

    def test_another_pid_an_empty_listing_or_unknown_lines_are_refused(self):
        import os
        me = os.getpid()
        for text in ('', 'f12\n', 'p%d\nf12\np999\nf3\n' % me, 'p999\nf3\n', 'p%d\nn/dev/x\n' % me, 'p%dx\nf3\n' % me,
                     'p%d\n\nf3\n' % me):
            with self.subTest(text=text):
                self.assertFalse(m5_bench.only_process_owns(text, me))

    def test_the_capture_adapter_accepts_the_real_lsof_answers_and_refuses_a_foreign_owner(self):
        import os
        import capture_m3
        import host_fixtures
        with tempfile.TemporaryDirectory() as directory:
            lease = adapter.M3UARTLease({}, Path(directory))
            lease.port = object()
            answers = {'cu': host_fixtures.completed('lsof_cu_held_alone'), 'tty': host_fixtures.completed('lsof_tty_unopened'),
                       'ps': host_fixtures.completed('ps_axo_system_processes')}
            def inventory(command, output):
                if command[0] == 'lsof':
                    self.assertEqual(len(command), 3)         # one node per call: lsof exits 1 when a named file is not open
                    return answers['tty' if '/dev/tty.' in command[2] else 'cu']
                return answers[command[0]]
            with patch.object(m3net, 'guard_check', lambda *a, **k: None), patch.object(capture_m3, 'inventory', inventory):
                lease.verify('acquire_exclusive_probe_uart_and_nic')
                for name, key, answer in (
                        ('a second owner', 'cu', subprocess.CompletedProcess([], 0, (self.REAL % os.getpid()) + 'p999\nf3\n', '')),
                        ('exit 1 from the cu listing', 'cu', subprocess.CompletedProcess([], 1, self.REAL % os.getpid(), '')),
                        ('someone holds the tty node', 'tty', subprocess.CompletedProcess([], 0, 'p999\nf3\n', '')),
                        ('stderr from the tty listing', 'tty', subprocess.CompletedProcess([], 1, '', 'lsof: WARNING'))):
                    with self.subTest(name):
                        saved = dict(answers)
                        answers[key] = answer
                        with self.assertRaisesRegex(ValueError, 'ownership changed'):
                            lease.verify('acquire_exclusive_probe_uart_and_nic')
                        answers.update(saved)


if __name__ == '__main__':
    unittest.main()
