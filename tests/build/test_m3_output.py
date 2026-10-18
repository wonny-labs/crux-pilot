"""Strict native diagnostic profile admission and actual loss marker rejection."""
import sys
from pathlib import Path
import tempfile
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'target'))
from m3_image import check_record_loss, check_record_output, check_image
from test_m3_image import fixture
from capture_m3_network import phy_diagnostic_mode

ROOT = Path(__file__).resolve().parents[2]


class OutputTests(unittest.TestCase):
    def config(self):
        lines = (ROOT / 'samples/controller/zephyr/m3-deferred-output.conf').read_text().splitlines()
        return '\n'.join(lines + ['CONFIG_CRUX_M3_NETWORK_PROBE=y',
            'CONFIG_CRUX_M3_PHY_DIAGNOSTIC=y', 'CONFIG_CRUX_M3_MDIO_COMPLETION_FIX=y',
            'CONFIG_CRUX_M3_MDIO_FAILURE_TRACE=y'])

    def check(self, text):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / '.config'
            path.write_text(text)
            check_record_output(path)

    def test_strict_diagnostic(self):
        self.check(self.config())

    def test_direct_profile_retained(self):
        self.check('CONFIG_PRINTK_SYNC=y\n')
        with self.assertRaises(ValueError):self.check('CONFIG_LOG=y\nCONFIG_PRINTK_SYNC=y\n')

    def test_wrong_or_missing_settings_rejected(self):
        for required in ('CONFIG_CRUX_M3_MDIO_FAILURE_TRACE=y', 'CONFIG_LOG_MODE_DEFERRED=y',
                         'CONFIG_LOG_PRINTK=y', 'CONFIG_LOG_PROCESS_THREAD_STACK_SIZE=2048',
                         'CONFIG_LOG_PROCESS_THREAD_PRIORITY=14', 'CONFIG_LOG_BUFFER_SIZE=4096'):
            with self.assertRaises(ValueError):self.check(self.config().replace(required,''))
        for extra in ('CONFIG_LOG_MODE_IMMEDIATE=y','CONFIG_LOG_BLOCK_IN_THREAD=y',
                      'CONFIG_LOG_MODE_OVERFLOW=y','CONFIG_SHELL_LOG_BACKEND=y',
                      'CONFIG_LOG_BACKEND_RTT=y','CONFIG_CRUX_CONTROLLER_M3=y',
                      'CONFIG_LOG_BACKEND_UART_ASYNC=y', 'CONFIG_LOG_BACKEND_SHOW_COLOR=y'):
            with self.assertRaises(ValueError):self.check(self.config()+'\n'+extra)

    def test_config_does_not_substitute_for_logger_object(self):
        with tempfile.TemporaryDirectory() as directory:
            paths = fixture(Path(directory), 'network')
            config = paths[2]
            config.write_text(config.read_text() + '\nCONFIG_CRUX_M3_DEFERRED_OUTPUT=y\n')
            with self.assertRaisesRegex(ValueError, 'logger stack object'):
                check_image(*paths, profile='network')

    def test_existing_traffic_runner_rejects_deferred_diagnostic(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / '.config'
            path.write_text(self.config())
            for diagnostic in (False, True):
                with self.assertRaisesRegex(ValueError, 'startup-only runner'):
                    phy_diagnostic_mode(path, diagnostic)

    def test_native_network_profile_and_diagnostic_exclusion(self):
        text=(ROOT/'samples/controller/zephyr/m3-native-output.conf').read_text()
        text+='\nCONFIG_CRUX_M3_NETWORK_PROBE=y\nCONFIG_CRUX_M3_MDIO_COMPLETION_FIX=y\n'
        self.check(text)
        for extra in ('CONFIG_CRUX_M3_PHY_DIAGNOSTIC=y','CONFIG_CRUX_M3_MDIO_FAILURE_TRACE=y',
                      'CONFIG_CRUX_M3_DEFERRED_OUTPUT=y','CONFIG_CRUX_CONTROLLER_M3=y'):
            with self.assertRaises(ValueError):self.check(text+'\n'+extra)

    def test_native_loss_marker_rejected(self):
        check_record_loss([(1,'M3_NET_STATE link=0')])
        with self.assertRaises(ValueError):check_record_loss([(1,'--- 37 messages dropped ---')])


if __name__ == '__main__':unittest.main()
