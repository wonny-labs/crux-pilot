"""Focused native configuration/ownership checks; software builds only."""
import os
from pathlib import Path
import subprocess
import unittest

REPO = Path(__file__).resolve().parents[2]


@unittest.skipUnless(os.environ.get('CRUX_SYSTEM_COMPOSITION_TESTS') == '1',
                     'requires pinned Zephyr environment; run scripts/test-system.sh')
class SystemCompositionTest(unittest.TestCase):
    def test_compositions(self):
        root = Path(os.environ.get('CRUX_SYSTEM_COMPOSITION_ROOT', str(REPO / 'build/system-composition')))
        root.mkdir(parents=True, exist_ok=True)
        cases = {
            'no_reboot': ('', 'CONFIG_REBOOT=n\n', 'requires CONFIG_REBOOT=y'),
            'no_hwinfo': ('', 'CONFIG_HWINFO=n\n', 'requires CONFIG_HWINFO=y'),
            'no_watchdog': ('', 'CONFIG_WATCHDOG=n\n', 'requires CONFIG_WATCHDOG=y'),
            'm3_owner': ('', 'CONFIG_CRUX_CONTROLLER_M3=y\n', 'conflicts with native owner CONFIG_CRUX_CONTROLLER_M3'),
            'wdt_shell': ('', 'CONFIG_SHELL=y\nCONFIG_WDT_SHELL=y\n', 'conflicts with native owner CONFIG_WDT_SHELL'),
            'cause_shell': ('', 'CONFIG_SHELL=y\nCONFIG_HWINFO_SHELL=y\n', 'conflicts with native owner CONFIG_HWINFO_SHELL'),
            'task_fallback': ('', 'CONFIG_TASK_WDT=y\nCONFIG_TASK_WDT_HW_FALLBACK=y\n', 'conflicts with native owner CONFIG_TASK_WDT_HW_FALLBACK'),
            'evkb_missing': ('/ { aliases { /delete-property/ crux-watchdog0; }; };', '', 'requires explicit crux-watchdog0'),
            'evkb_disabled': ('&wdog0 { status = "disabled"; };', '', 'watchdog must be enabled'),
            'evkb_wrong': ('/ { aliases { crux-watchdog0 = &wdog1; }; };\n&wdog1 { status = "okay"; };', '', 'requires the qualified RT1060 WDOG1'),
            'evkb_revision': ('', '', 'unsupported EVKB board revision'),
        }
        selected = os.environ.get('CRUX_SYSTEM_COMPOSITION_CASE')
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
                app = REPO / ('tests/system_consumer/zephyr' if target else 'tests/system/zephyr')
                build = root / ('evkb-build' if target else 'native-build')
                if name == 'evkb_revision':
                    build = root / 'revision-build'
                result = subprocess.run([
                    'west', 'build', '--cmake-only', '-b', board, '-d', str(build), str(app), '--',
                    f'-DZEPHYR_MODULES={";".join(modules)}',
                    f'-DKCONFIG_ROOT={REPO / "samples/controller/zephyr/Kconfig"}',
                    f'-DEXTRA_DTC_OVERLAY_FILE={case / "case.overlay"}',
                    f'-DEXTRA_CONF_FILE={case / "case.conf"}'],
                    text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=240)
                if result.returncode == 0:
                    built = subprocess.run(['cmake', '--build', str(build), '--target', 'crux_system_zephyr', '-j', '2'],
                                           text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=240)
                    result.stdout += built.stdout
                    result.returncode = built.returncode
                (case / 'build.log').write_text(result.stdout)
                self.assertNotEqual(result.returncode, 0, result.stdout)
                self.assertIn(diagnostic, result.stdout)


if __name__ == '__main__':
    unittest.main()
