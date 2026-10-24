"""Actual native_sim configuration/build failures, run by test-serial.sh."""
import os
from pathlib import Path
import shutil
import subprocess
import unittest

REPO = Path(__file__).resolve().parents[2]


@unittest.skipUnless(os.environ.get("CRUX_SERIAL_COMPOSITION_TESTS") == "1",
                     "requires pinned Zephyr environment; run scripts/test-serial.sh")
class SerialCompositionTest(unittest.TestCase):
    def test_static_bindings(self):
        root = REPO / "build/serial-composition"
        root.mkdir(parents=True, exist_ok=True)
        cases = {
            "missing": ('/ { aliases { /delete-property/ crux-serial0; }; };', '',
                        'Crux Serial requires an explicit dedicated'),
            "disabled": ('&serial_test { status = "disabled"; };', '',
                         'Crux Serial requires an explicit dedicated'),
            "unsupported_resource": ('/ { aliases { crux-serial0 = &unsupported; }; '
                                     'unsupported: unsupported { compatible = "test,unsupported"; '
                                     'status = "okay"; }; };', '', 'Crux Serial unsupported UART resource'),
            "electrical_mode": ('&serial_test { hw-flow-control; };', '',
                                'Crux Serial unsupported UART electrical mode'),
            "console": ('/ { chosen { zephyr,console = &serial_test; }; };',
                        'CONFIG_UART_CONSOLE=y\n', 'Crux Serial conflicts with UART Console'),
            "shell_polling": ('/ { chosen { zephyr,shell-uart = &serial_test; }; };',
                              'CONFIG_SHELL=y\nCONFIG_SHELL_BACKEND_SERIAL=y\n'
                              'CONFIG_SHELL_BACKEND_SERIAL_API_POLLING=y\n',
                              'Crux Serial conflicts with UART Shell'),
            "logging_fallback": ('/ { chosen { /delete-property/ zephyr,log-uart; '
                                 'zephyr,console = &serial_test; }; };',
                                 'CONFIG_LOG=y\nCONFIG_LOG_BACKEND_UART=y\n',
                                 'Crux Serial conflicts with UART Logging'),
            "logging_list": ('/ { chosen { zephyr,log-uart = &log_uarts; }; '
                             'log_uarts: log-uarts { compatible = "zephyr,log-uart"; '
                             'uarts = <&other_uart &serial_test>; }; '
                             'other_uart: other-uart { compatible = "zephyr,uart-emul"; '
                             'status = "okay"; current-speed = <115200>; }; };',
                             'CONFIG_LOG=y\nCONFIG_LOG_BACKEND_UART=y\n'
                             'CONFIG_LOG_BACKEND_UART_AUTOSTART=n\n',
                             'Crux Serial conflicts with UART Logging'),
            "child_owner": ('&serial_test { child { compatible = "zephyr,bt-hci-uart"; '
                            'status = "okay"; }; };', '',
                            'Crux Serial does not support UART child consumers'),
            "mcumgr_raw": ('/ { chosen { zephyr,uart-mcumgr = &serial_test; }; };',
                           'CONFIG_UART_MCUMGR=y\nCONFIG_UART_MCUMGR_RAW_PROTOCOL=y\n',
                           'Crux Serial conflicts with UART management'),
            "csi": ('&csi { status = "okay"; };', '',
                    'LPUART3 Serial pins conflict with enabled EVKB CSI'),
            "unsupported_lpuart": ('/ { aliases { crux-serial0 = &lpuart8; }; };'
                                   '&lpuart8 { status = "okay"; };', '',
                                   'Crux Serial unsupported UART resource'),
            "single_wire": ('&lpuart3 { single-wire; };', '',
                            'Crux Serial unsupported UART electrical mode'),
            "rs485": ('&lpuart3 { nxp,rs485-mode; };', '',
                      'Crux Serial unsupported UART electrical mode'),
            "unsupported_provider": ('', '', 'Crux Serial unsupported provider'),
            "disabled_owners": ('/ { chosen { zephyr,console = &serial_test; '
                                'zephyr,shell-uart = &serial_test; '
                                'zephyr,uart-mcumgr = &serial_test; '
                                'zephyr,log-uart = &inactive_log; }; '
                                'inactive_log: inactive-log { compatible = "zephyr,log-uart"; '
                                'uarts = <&serial_test>; }; };',
                                'CONFIG_UART_CONSOLE=n\nCONFIG_SHELL=n\n'
                                'CONFIG_UART_MCUMGR=n\nCONFIG_LOG=n\n', None),
        }
        selected = os.environ.get("CRUX_SERIAL_COMPOSITION_CASE")
        if selected and selected not in cases:
            self.fail(f'Unknown composition case: {selected}')
        for name, (overlay, config, diagnostic) in cases.items():
            if selected and name != selected:
                continue
            with self.subTest(case=name):
                case = root / name
                case.mkdir(exist_ok=True)
                overlay_path = case / "case.overlay"
                overlay_path.write_text(overlay)
                config_path = case / "case.conf"
                config_path.write_text(config)
                target = name in ('csi', 'unsupported_lpuart', 'single_wire', 'rs485')
                board = 'mimxrt1060_evk@B//qspi' if target else 'native_sim/native/64'
                app = REPO / ('tests/serial_consumer/zephyr' if target else 'tests/serial/zephyr')
                build = root / ('evkb-build' if target else 'native-build')
                modules = [str(REPO)]
                if target:
                    modules += subprocess.check_output(
                        ['west', 'list', 'cmsis', 'cmsis_6', 'hal_nxp', '-f', '{abspath}'],
                        cwd=os.environ['CRUX_WORKSPACE'], text=True).splitlines()
                command = ['west', 'build', '--cmake-only', '-p', 'never', '-b', board,
                           '-d', str(build), str(app), '--',
                           f'-DZEPHYR_MODULES={";".join(modules)}',
                           '-DCRUX_SERIAL_PROVIDER=zephyr',
                           f'-DEXTRA_DTC_OVERLAY_FILE={overlay_path}',
                           f'-DEXTRA_CONF_FILE={config_path}']
                if name == 'unsupported_provider':
                    command[command.index('-DCRUX_SERIAL_PROVIDER=zephyr')] = '-DCRUX_SERIAL_PROVIDER=unsupported'
                result = subprocess.run(command, text=True, stdout=subprocess.PIPE,
                                        stderr=subprocess.STDOUT, timeout=900)
                configured = result.returncode == 0
                if configured:
                    built = subprocess.run(['cmake', '--build', str(build),
                                            '--target', 'crux_serial_zephyr', '-j', '2'],
                                           text=True, stdout=subprocess.PIPE,
                                           stderr=subprocess.STDOUT, timeout=180)
                    result = subprocess.CompletedProcess(command, built.returncode,
                                                         result.stdout + built.stdout)
                (case / 'build.log').write_text(result.stdout)
                for artifact in ('zephyr/.config', 'zephyr/zephyr.dts'):
                    snapshot = case / Path(artifact).name
                    snapshot.unlink(missing_ok=True)
                    if configured and (build / artifact).exists():
                        shutil.copyfile(build / artifact, snapshot)
                if diagnostic:
                    self.assertNotEqual(result.returncode, 0, f'{name}: build unexpectedly succeeded')
                    self.assertIn(diagnostic, result.stdout, f'{name}: unrelated build failure')
                else:
                    self.assertEqual(result.returncode, 0, result.stdout[-6000:])
                print(f'{name}: {"rejected at build/configuration" if diagnostic else "build PASS"}',
                      flush=True)


if __name__ == '__main__':
    unittest.main(verbosity=2)
