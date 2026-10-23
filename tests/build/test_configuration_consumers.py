"""Independent public Configuration consumers; native/fault composition stays separate."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


@unittest.skipUnless(shutil.which("cmake"), "CMake is required")
class ConfigurationConsumers(unittest.TestCase):
    def test_independent_c_and_cpp(self):
        with tempfile.TemporaryDirectory() as directory:
            for command in (
                ["cmake", "-S", str(ROOT / "tests/configuration_consumer"), "-B", directory],
                ["cmake", "--build", directory],
                [str(Path(directory) / "configuration_c")],
                [str(Path(directory) / "configuration_cpp")],
            ):
                result = subprocess.run(command, capture_output=True, text=True, timeout=90)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
