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

    def test_gpio_consumer_is_cross_built_for_rt1170_with_only_the_button_selected(self):
        overlay = ROOT / "tests/gpio/evkb/boards/mimxrt1170_evk_mimxrt1176_cm7.overlay"
        self.assertTrue(overlay.is_file(), "the RT1170 GPIO overlay is missing")
        text = overlay.read_text()
        self.assertIn("crux-gpio0 = &user_button;", text)
        for forbidden in ("chosen", "zephyr,flash", "zephyr,sram", "partition", "status"):
            self.assertNotIn(forbidden, text, f"the overlay must not touch {forbidden}")

        script = (ROOT / "scripts/test-gpio.sh").read_text()
        self.assertIn("mimxrt1170_evk@A/mimxrt1176/cm7", script)
        self.assertIn("gpio-rt1170", script)
        self.assertIsNone(
            re.search(r"\bwest\s+(flash|debug|debugserver|attach|reset)\b", script),
            "the GPIO runner must not program, debug or reset a board",
        )
        rt1170 = script.index("mimxrt1170_evk@A/mimxrt1176/cm7")
        self.assertIn(f"unset {INJECTION_VARIABLES}", script[:rt1170])


if __name__ == "__main__":
    unittest.main()
