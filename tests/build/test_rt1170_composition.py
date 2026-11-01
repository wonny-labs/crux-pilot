"""Protect the RT1170-EVK Rev A software-only build composition."""

import os
from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]
RUNNER = ROOT / "scripts/test-m3-rt1170.sh"

# Variables that Zephyr's CMake honours and that would inject configuration or overlays
# without any file in this repository showing it.
INJECTION_VARIABLES = "DTC_OVERLAY_FILE EXTRA_DTC_OVERLAY_FILE OVERLAY_CONFIG CONF_FILE BOARD_ROOT"


class RT1170CompositionTests(unittest.TestCase):
    def test_runner_targets_rev_a_cm7_with_project_m3_module(self):
        self.assertTrue(os.access(RUNNER, os.X_OK))
        runner = RUNNER.read_text()
        self.assertIn("mimxrt1170_evk@A/mimxrt1176/cm7", runner)
        self.assertIn("ZEPHYR_EXTRA_MODULES=", runner)
        self.assertIn("samples/controller/zephyr/m3.conf", runner)
        self.assertIn("EXTRA_CONF_FILE=", runner)
        self.assertIn("rt1170-evk-a1-m3", runner)

    def test_runner_only_builds(self):
        runner = RUNNER.read_text()
        self.assertIsNone(
            re.search(r"\bwest\s+(flash|debug|debugserver|attach|reset)\b", runner),
            "the runner must not program, debug or reset a board",
        )
        self.assertNotIn("--runner", runner)

    def test_runner_does_not_pass_or_inherit_overlays_or_extra_configuration(self):
        runner = RUNNER.read_text()
        self.assertNotIn("-DDTC_OVERLAY_FILE", runner)
        self.assertNotIn("-DEXTRA_DTC_OVERLAY_FILE", runner)
        self.assertIn(f"unset {INJECTION_VARIABLES}", runner)

    def test_sample_has_no_rt1170_specific_overlay_or_configuration(self):
        app = ROOT / "samples/controller/zephyr"
        for name in ("app.overlay", "app.conf"):
            self.assertFalse((app / name).exists(), name)
        for directory, pattern in (("boards", "*mimxrt1170*"), ("socs", "*mimxrt1176*")):
            found = sorted(p.name for p in (app / directory).glob(pattern))
            self.assertEqual(found, [], f"{directory} would be auto-loaded for RT1170")

    def test_gpio_overlay_selects_exactly_the_user_button(self):
        overlay = ROOT / "tests/gpio/evkb/boards/mimxrt1170_evk_mimxrt1176_cm7.overlay"
        self.assertTrue(overlay.is_file(), "the RT1170 GPIO overlay is missing")
        lines = [line.strip() for line in overlay.read_text().splitlines() if line.strip()]
        self.assertEqual(lines, ["/ {", "aliases { crux-gpio0 = &user_button; };", "};"])

    def test_gpio_runner_builds_rt1170_only_after_clearing_injected_configuration(self):
        script = (ROOT / "scripts/test-gpio.sh").read_text()
        rt1060 = script.index("-b 'mimxrt1060_evk@B//qspi'")
        guard = script.index(f"unset {INJECTION_VARIABLES}")
        rt1170 = script.index("-b 'mimxrt1170_evk@A/mimxrt1176/cm7'")
        self.assertLess(rt1060, guard, "the guard must not precede the RT1060 build")
        self.assertLess(guard, rt1170, "the guard must precede the RT1170 build")
        self.assertIn("gpio-rt1170", script[rt1170:])

    def test_gpio_runner_never_programs_debugs_or_resets_a_board(self):
        script = (ROOT / "scripts/test-gpio.sh").read_text()
        self.assertIsNone(
            re.search(r"\bwest\s+(flash|debug|debugserver|attach|reset)\b", script),
            "the GPIO runner must not program, debug or reset a board",
        )
        self.assertIsNone(re.search(r"(?:^|\s)(?:-t|--target)\s+flash\b", script))
        self.assertNotIn("--runner", script)

if __name__ == "__main__":
    unittest.main()
