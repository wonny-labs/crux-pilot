"""Actual qualified native builds; opt in with the pinned Zephyr environment."""
import os
from pathlib import Path
import subprocess
import unittest

REPO = Path(__file__).resolve().parents[2]


@unittest.skipUnless(os.environ.get('CRUX_LOGGING_COMPOSITION_TESTS') == '1',
                     'requires pinned Zephyr environment; run scripts/test-logging.sh')
class LoggingCompositionTest(unittest.TestCase):
    def test_rejected_compositions(self):
        root = Path(os.environ.get('CRUX_LOGGING_COMPOSITION_ROOT', str(REPO / 'build/logging-composition')))
        root.mkdir(parents=True, exist_ok=True)
        cases = {
            'missing': ('/ { chosen { /delete-property/ zephyr,log-uart; }; };', '',
                        'requires explicit zephyr,log-uart'),
            'disabled': ('&logging_test { status = "disabled"; };', '', 'UART must be enabled'),
            'multiple': ('&logging_uarts { uarts = <&logging_test &uart0>; };', '',
                         'requires exactly one UART'),
            'unsupported': ('&logging_uarts { uarts = <&uart0>; };', '', 'unsupported UART resource'),
            'parity': ('&logging_test { parity = "even"; };', '', 'unsupported UART mode'),
            'flow_control': ('&logging_test { hw-flow-control; };', '', 'unsupported UART mode'),
            'console': ('/ { chosen { zephyr,console = &logging_test; }; };', '',
                        'conflicts with UART Console'),
            'shell': ('/ { chosen { zephyr,shell-uart = &logging_test; }; };',
                      'CONFIG_SHELL=y\nCONFIG_SHELL_BACKEND_SERIAL=y\n'
                      'CONFIG_SHELL_BACKEND_SERIAL_API_POLLING=y\nCONFIG_SHELL_LOG_BACKEND=n\n', 'conflicts with UART Shell'),
            'serial': ('/ { aliases { crux-serial0 = &logging_test; }; };',
                       'CONFIG_CRUX_SERIAL=y\nCONFIG_UART_INTERRUPT_DRIVEN=y\n'
                       'CONFIG_UART_USE_RUNTIME_CONFIGURE=y\n', 'conflicts with'),
            'child': ('&logging_test { child { compatible = "zephyr,bt-hci-uart"; '
                      'status = "okay"; }; };', '', 'rejects UART child consumers'),
            'shell_backend': ('', 'CONFIG_SHELL=y\nCONFIG_SHELL_LOG_BACKEND=y\n',
                              'unsupported CONFIG_SHELL_LOG_BACKEND'),
            'backend': ('', 'CONFIG_LOG_BACKEND_NATIVE_POSIX=y\n', 'unsupported CONFIG_LOG_BACKEND_NATIVE_POSIX'),
            'overflow': ('', 'CONFIG_LOG_MODE_OVERFLOW=y\n', 'unsupported CONFIG_LOG_MODE_OVERFLOW'),
            'filter': ('', 'CONFIG_LOG_RUNTIME_FILTERING=y\n', 'unsupported CONFIG_LOG_RUNTIME_FILTERING'),
            'ceiling': ('', 'CONFIG_LOG_MAX_LEVEL=3\n', 'requires DEBUG native ceiling'),
            'worker': ('', 'CONFIG_LOG_PROCESS_THREAD=n\n', 'requires CONFIG_LOG_PROCESS_THREAD=y'),
            'custom_macros': ('', 'CONFIG_LOG_CUSTOM_HEADER=y\n', 'unsupported CONFIG_LOG_CUSTOM_HEADER'),
            'no_severity': ('', 'CONFIG_LOG_BACKEND_SHOW_LEVEL=n\n', 'requires CONFIG_LOG_BACKEND_SHOW_LEVEL=y'),
            'no_boundary': ('', 'CONFIG_LOG_BACKEND_CRLF_NONE=y\n', 'unsupported CONFIG_LOG_BACKEND_CRLF_NONE'),
            'evkb_revision': ('', '', 'unsupported EVKB board revision'),
            'evkb_stdout': ('', 'CONFIG_STDOUT_CONSOLE=y\n', 'shared Console requires Logging-only routing'),
            'evkb_printk': ('', 'CONFIG_LOG_PRINTK=n\n', 'shared Console requires Logging-only routing'),
            'evkb_input': ('', 'CONFIG_CONSOLE_SUBSYS=y\nCONFIG_CONSOLE_GETCHAR=y\n',
                           'shared Console requires Logging-only routing'),
            'evkb_positive': ('', '', None),
            'positive': ('', '', None),
        }
        selected = os.environ.get('CRUX_LOGGING_COMPOSITION_CASE')
        if selected and selected not in cases:
            self.fail(f'Unknown case {selected}')
        for name, (overlay, config, diagnostic) in cases.items():
            if selected and name != selected:
                continue
            with self.subTest(case=name):
                case = root / name
                case.mkdir(exist_ok=True)
                (case / 'case.overlay').write_text(overlay)
                (case / 'case.conf').write_text(config)
                target = name.startswith('evkb_')
                modules = [str(REPO)]
                if target:
                    modules += subprocess.check_output(
                        ['west', 'list', 'cmsis', 'cmsis_6', 'hal_nxp', '-f', '{abspath}'],
                        cwd=os.environ['CRUX_WORKSPACE'], text=True).splitlines()
                board = 'mimxrt1060_evk@B//qspi' if target else 'native_sim/native/64'
                if name == 'evkb_revision':
                    board = 'mimxrt1060_evk@C//qspi'
                app = REPO / ('tests/logging_consumer/zephyr' if target else 'tests/logging/zephyr')
                build = root / ('evkb-build' if target else 'native-build')
                if name == 'evkb_revision':
                    build = root / 'revision-build'
                result = subprocess.run([
                    'west', 'build', '--cmake-only', '-b', board, '-d', str(build),
                    str(app), '--', f'-DZEPHYR_MODULES={";".join(modules)}',
                    f'-DEXTRA_DTC_OVERLAY_FILE={case / "case.overlay"}',
                    f'-DEXTRA_CONF_FILE={case / "case.conf"}'],
                    text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=240)
                if result.returncode == 0:
                    built = subprocess.run(['cmake', '--build', str(build),
                                            '--target', 'crux_logging_zephyr', '-j', '2'],
                                           text=True, stdout=subprocess.PIPE,
                                           stderr=subprocess.STDOUT, timeout=240)
                    result.stdout += built.stdout
                    result.returncode = built.returncode
                (case / 'build.log').write_text(result.stdout)
                if diagnostic:
                    self.assertNotEqual(result.returncode, 0, result.stdout)
                    self.assertIn(diagnostic, result.stdout)
                else:
                    self.assertEqual(result.returncode, 0, result.stdout)


if __name__ == '__main__':
    unittest.main()
