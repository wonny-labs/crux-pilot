"""Protect the RT1170-EVK Rev A software-only build composition."""

import os
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]


class RT1170CompositionTests(unittest.TestCase):
    def test_runner_targets_rev_a_cm7_with_project_m3_module(self):
        runner_path = ROOT / "scripts/test-m3-rt1170.sh"
        self.assertTrue(os.access(runner_path, os.X_OK))
        runner = runner_path.read_text()
        self.assertIn("mimxrt1170_evk@A/mimxrt1176/cm7", runner)
        self.assertIn("ZEPHYR_EXTRA_MODULES=", runner)
        self.assertIn("EXTRA_CONF_FILE=", runner)
        self.assertNotIn("DTC_OVERLAY_FILE", runner)
        self.assertIn("rt1170-evk-a1-m3", runner)
        self.assertNotIn("west flash", runner)
        self.assertNotIn("west debug", runner)

    def test_runner_keeps_upstream_rev_a_flash_and_memory_configuration(self):
        app = ROOT / "samples/controller/zephyr"
        runner = (ROOT / "scripts/test-m3-rt1170.sh").read_text()
        self.assertNotIn("DTC_OVERLAY_FILE", runner)
        self.assertNotIn("zephyr,flash", runner)
        self.assertNotIn("slot0_partition", runner)
        self.assertFalse((app / "app.overlay").exists())
        self.assertFalse(any(app.glob("mimxrt1170_evk*.overlay")))
        self.assertFalse(any((app / "boards").glob("mimxrt1170_evk*.overlay")))
        self.assertFalse(any((app / "socs").glob("*mimxrt1176*.overlay")))


if __name__ == "__main__":
    unittest.main()
