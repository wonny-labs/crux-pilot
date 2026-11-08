"""The host-side parsers of the stage 1 code run against real output of the read-only host commands.

The inputs are the recordings of ``fixtures/host-2026-11-08`` (see ``host_fixtures.py``) and the public J-Link
Commander logs of the repository. The first real run of the driver stopped on a difference between a hand-written
fake and the real ``lsof``; these tests keep the fakes honest. Not covered, because it needs a probe or a carrier:
the J-Link output of ``IsHalted`` (the pinned JLinkExe contains the commands text ``CPU is halted.`` and
``CPU is not halted.``, no log of it exists), and the real ``ifconfig`` and ``route`` output of an active dedicated
adapter (recorded here only as marked proxies from the active host interface).
"""
import json
import plistlib
import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'target'))
import capture_m3
import capture_m3_network as m3net
import m5_capture_adapter as adapter
import m3_clock
import host_fixtures as real
import program_m5_image as program
from capture_m3 import HardwareStop

REPO = Path(__file__).resolve().parents[2]
UART = '/dev/cu.usbmodem11402'
HOST, MAC = '192.168.77.1', '00:24:9b:6e:f7:7f'


class GuardCheck(unittest.TestCase):
    def session(self, directory):
        executable = Path(directory) / 'guard.py'
        executable.write_text('guard')
        log = Path(directory) / 'guard.log'
        log.write_text('1.0 DENIED exact_DAPLink_USB_serial=%s disk=disk4 mount_target=disk4\n' % m3net.USB_SERIAL)
        return dict(clock=m3_clock.CLOCK_NAME, serial=m3net.USB_SERIAL, disk='/dev/disk4', pid=1, log=str(log),
                    boot_session_uuid=real.text('sysctl_kern_bootsessionuuid'), executable=str(executable),
                    sha256=capture_m3.sha(executable), deadline=m3_clock.monotonic() + 1000,
                    process_identity=real.text('ps_p_guard_lstart'))

    def run_guard(self, directory, mount=None, tree=None):
        def checked(command, destination):
            name = {'/usr/sbin/sysctl': 'sysctl_kern_bootsessionuuid', 'ps': 'ps_p_guard_lstart', 'mount': 'mount_listing'}[command[0]]
            return real.text(name) if mount is None or name != 'mount_listing' else mount
        plist = plistlib.dumps(tree if tree is not None else real.daplink_tree()).decode()
        with patch.object(m3net, 'run_checked', checked), \
                patch.object(m3net, 'inventory', lambda command, output: subprocess.CompletedProcess(command, 0, plist, '')):
            m3net.guard_check(self.session(directory), Path(directory), 'g', 90, uart=UART)

    def test_the_real_guard_session_ancestry_and_mount_table_pass(self):
        with tempfile.TemporaryDirectory() as directory:
            self.run_guard(directory)

    def test_a_mounted_daplink_volume_or_a_changed_ancestry_stops(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaises(HardwareStop) as mounted:
                self.run_guard(directory, mount=real.text('mount_listing') + '/dev/disk4s1 on /Volumes/RT1060-EVK (msdos, local)\n')
            self.assertIn('DAPLink virtual media mounted', str(mounted.exception.__cause__))
            with self.assertRaises(HardwareStop) as changed:
                self.run_guard(directory, tree={})
            self.assertIn('ancestry changed', str(changed.exception.__cause__))

    def test_the_real_ancestry_names_the_disk_and_the_uart_of_the_guarded_serial(self):
        self.assertEqual(m3net.usb_association(real.daplink_tree()), (['disk4'], [UART]))

    def test_the_real_boot_session_uuid_has_the_accepted_shape(self):
        self.assertTrue(re.fullmatch(r'[0-9a-fA-F]{8}(?:-[0-9a-fA-F]{4}){3}-[0-9a-fA-F]{12}', real.text('sysctl_kern_bootsessionuuid').strip()))


class Ownership(unittest.TestCase):
    def test_the_unowned_pair_passes_the_accepted_before_acquisition_check(self):
        answers = {'lsof': real.completed('lsof_uart_pair_unowned'), 'ps': real.completed('ps_axo_system_processes')}
        with tempfile.TemporaryDirectory() as directory, patch.object(capture_m3, 'inventory', lambda command, output: answers[command[0]]):
            capture_m3.check_owners(UART, Path(directory) / 'owners.txt')

    def test_the_real_lsof_forms_are_what_the_helper_expects(self):
        import os
        for name, code in (('lsof_cu_held_alone', 0), ('lsof_cu_held_twice', 0), ('lsof_cu_held_tty_unopened', 1)):
            result = real.completed(name, 4242)
            self.assertEqual(result.returncode, code, name)
            self.assertTrue(adapter.only_process_owns(result.stdout, 4242), name)
        self.assertEqual((real.completed('lsof_tty_unopened').returncode, real.text('lsof_tty_unopened')), (1, ''))


class Nic(unittest.TestCase):
    def run_checked(self, command, destination):
        return real.text({'ifconfig': 'ifconfig_en13_link_down', 'networksetup': 'networksetup_getinfo'}[command[0]])

    def test_the_real_link_down_adapter_and_service_pass_the_identity_check(self):
        with tempfile.TemporaryDirectory() as directory:
            program.check_nic_identity(Path(directory), 'x', self.run_checked)

    def test_a_different_adapter_or_service_stops(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaisesRegex(HardwareStop, 'identity'):
                program.check_nic_identity(Path(directory), 'x', lambda c, d: real.text('ifconfig_en13_link_down').replace(MAC, '00:24:9b:00:00:01')
                                           if c[0] == 'ifconfig' else real.text('networksetup_getinfo'))
            with self.assertRaisesRegex(HardwareStop, 'service'):
                program.check_nic_identity(Path(directory), 'x', lambda c, d: real.text('ifconfig_en13_link_down') if c[0] == 'ifconfig'
                                           else real.text('networksetup_getinfo').replace('Manual Configuration', 'DHCP Configuration'))

    def test_the_real_link_down_output_has_no_address_and_is_observed_as_down(self):
        observed = adapter.parse_interface(real.text('ifconfig_en13_link_down'))
        self.assertEqual(observed, dict(source=None, active=False))

    def test_an_inactive_adapter_with_the_static_address_is_down_too_and_an_active_one_needs_it(self):
        up = real.text('ifconfig_link_up_proxy')
        self.assertEqual(adapter.parse_interface(up), dict(source=HOST, active=True))
        self.assertEqual(adapter.parse_interface(up.replace('status: active', 'status: inactive')), dict(source=HOST, active=False))
        without_address = re.sub(r'(?m)^\tinet .*\n', '', up)
        with self.assertRaisesRegex(Exception, 'mismatch'):
            adapter.parse_interface(without_address)                      # an active link without the address is not the isolated NIC
        with self.assertRaisesRegex(Exception, 'mismatch'):
            adapter.parse_interface(up.replace(MAC, '00:24:9b:00:00:01'))
        with self.assertRaisesRegex(Exception, 'mismatch'):               # a link that is down still must not show another address
            adapter.parse_interface(up.replace('status: active', 'status: inactive').replace(HOST, '192.168.99.1'))
        with self.assertRaisesRegex(Exception, 'mismatch'):
            adapter.parse_interface(real.text('ifconfig_en13_link_down').replace(MAC, '00:24:9b:00:00:01'))

    def test_the_connected_network_route_form_is_accepted_and_a_default_route_is_not(self):
        route = real.text('route_get_direct_network_proxy')
        self.assertEqual(adapter.parse_direct_route(route, HOST)['route_destination'], '192.168.77.0')
        with self.assertRaises(Exception):
            adapter.parse_direct_route(route.replace('destination: 192.168.77.0', 'destination: default').replace('interface: en13', 'interface: en0'), HOST)

    def test_the_cloned_host_route_form_is_accepted_and_a_router_flag_is_not(self):
        route = real.text('route_get_cloned_host_proxy')
        self.assertEqual(adapter.parse_direct_route(route, HOST)['route_flags'], ['UP', 'HOST', 'DONE', 'LLINFO', 'WASCLONED', 'IFSCOPE', 'IFREF'])
        with self.assertRaises(Exception):
            adapter.parse_direct_route(route.replace('<UP,', '<UP,GATEWAY,'), HOST)

    def test_the_route_and_carrier_check_accepts_the_active_forms_and_stops_without_carrier(self):
        def checked(texts):
            return lambda command, destination: real.text(texts[command[0]])
        with tempfile.TemporaryDirectory() as directory:
            with patch.object(m3net, 'run_checked', checked(dict(ifconfig='ifconfig_link_up_proxy', route='route_get_direct_network_proxy'))):
                self.assertTrue(m3net.check_route(Path(directory), 'r', interface='en13', mac=MAC))
            with patch.object(m3net, 'run_checked', checked(dict(ifconfig='ifconfig_en13_link_down', route='route_get_direct_network_proxy'))):
                with self.assertRaisesRegex(HardwareStop, 'mismatch|unverified'):
                    m3net.check_route(Path(directory), 'r', interface='en13', mac=MAC)


class UartSelection(unittest.TestCase):
    def test_the_real_port_listing_maps_the_uart_to_the_guarded_usb_serial(self):
        capture_m3.select_uart(real.comports(), UART, '02290000325182c300000000000000000000000097969905')


class JLinkOutput(unittest.TestCase):
    """Real J-Link Commander V9.86a transcripts of this probe and target (the same executable, not today's run)."""
    LOGS = REPO / 'docs' / 'evidence' / 'microsd-card-states-stack'

    def commander(self, text):
        def popen(command, stdout, stderr, start_new_session, env):
            stdout.write(text.encode())
            return SimpleNamespace(wait=lambda timeout=None: 0, pid=1)
        with tempfile.TemporaryDirectory() as directory, patch.object(capture_m3, 'verify_jlink_tools', lambda: '/x/JLinkExe'), \
                patch.object(capture_m3.subprocess, 'Popen', popen):
            return capture_m3.commander('602003106', 'q\n', Path(directory) / 'out.log')

    def test_real_connect_transcripts_pass_the_wrapper_and_the_identity_check(self):
        for name in ('blank-preflight.log', 'blank-reset.log', 'blank-flash.log'):
            with self.subTest(name):
                program.check_identity(self.commander((self.LOGS / name).read_text(errors='replace')))

    def test_a_real_loadfile_verifybin_reset_go_transcript_is_accepted_and_reports_the_verify(self):
        text = self.commander((real.DIRECTORY / 'jlink-loadfile-verifybin-restart.log').read_text())
        self.assertIn('Verify successful', text)
        program.check_identity(text)

    def test_the_real_connection_failure_transcript_is_a_diagnostic_stop(self):
        text = 'SEGGER J-Link Commander V9.86a\nJ-Link>connect\nFailed to power up DAP.\nError occurred: Could not connect to the target device.\n'
        with self.assertRaisesRegex(HardwareStop, 'diagnostic'):
            self.commander(text)


if __name__ == '__main__':
    unittest.main()
