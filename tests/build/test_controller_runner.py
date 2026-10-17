"""A success-looking marker must not mask a host process or sanitizer failure."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

REPO = Path(__file__).resolve().parents[2]


class ControllerRunnerTest(unittest.TestCase):
    def test_host_failure_after_marker_is_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            stub = root / "failed-controller"
            stub.write_text(f"#!{sys.executable}\nprint('CONTROLLER_COMPLETE cycles=20')\n"
                            "raise SystemExit(1)\n")
            stub.chmod(0o700)
            result = subprocess.run([sys.executable, str(REPO / "scripts/check-controller.py"),
                                     str(stub), "--log", str(root / "scenario.log")],
                                    capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("exited with status 1", result.stderr)
