"""Check the Zephyr version gate by running its CMake function in script mode."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

REPO = Path(__file__).resolve().parents[2]
GATE = REPO / "cmake/CruxZephyrBaseline.cmake"
CMAKE = shutil.which("cmake")


def declared_baseline():
    match = re.search(r'set\(CRUX_ZEPHYR_BASELINE\s+"(\d+\.\d+\.\d+)"\)', GATE.read_text())
    return match.group(1)


@unittest.skipUnless(CMAKE, "CMake is required")
class ZephyrBaselineTest(unittest.TestCase):
    def run_gate(self, major, minor, patch, string=None, allow=False):
        """Run the check as Zephyr's version.cmake would leave the variables."""
        string = string or f"{major}.{minor}.{patch}"
        with tempfile.TemporaryDirectory() as temporary:
            driver = Path(temporary) / "driver.cmake"
            driver.write_text(
                f'include("{GATE}")\n'
                f'set(KERNEL_VERSION_MAJOR {major})\n'
                f'set(KERNEL_VERSION_MINOR {minor})\n'
                f'set(KERNEL_PATCHLEVEL {patch})\n'
                f'set(KERNEL_VERSION_STRING "{string}")\n'
                + ('set(CRUX_ALLOW_UNVERIFIED_ZEPHYR ON)\n' if allow else '') +
                'crux_check_zephyr_baseline()\n'
                'message(STATUS "gate passed")\n')
            result = subprocess.run([CMAKE, "-P", str(driver)], capture_output=True, text=True)
        return result, result.stdout + result.stderr

    def test_the_declared_baseline_is_a_plain_version(self):
        self.assertRegex(declared_baseline(), r"^\d+\.\d+\.\d+$")

    def test_matching_version_passes_quietly(self):
        major, minor, patch = declared_baseline().split(".")
        result, output = self.run_gate(major, minor, patch)
        self.assertEqual(0, result.returncode, output)
        self.assertIn("gate passed", output)
        self.assertNotIn("CMake Warning", output)

    def test_a_fork_suffix_still_passes(self):
        major, minor, patch = declared_baseline().split(".")
        result, output = self.run_gate(major, minor, patch, string=f"{declared_baseline()}-fork1")
        self.assertEqual(0, result.returncode, output)
        self.assertNotIn("CMake Warning", output)

    def test_other_versions_stop_the_build_and_name_both(self):
        major, minor, patch = declared_baseline().split(".")
        for other in ((major, minor, str(int(patch) + 1)), (major, str(int(minor) + 1), patch),
                      (str(int(major) + 1), minor, patch)):
            with self.subTest(version=other):
                result, output = self.run_gate(*other)
                self.assertNotEqual(0, result.returncode, output)
                self.assertNotIn("gate passed", output)
                self.assertIn(declared_baseline(), output)
                self.assertIn(".".join(other), output)
                self.assertIn("CRUX_ALLOW_UNVERIFIED_ZEPHYR", output)

    def test_the_opt_out_warns_and_continues(self):
        major, minor, patch = declared_baseline().split(".")
        other = (major, minor, str(int(patch) + 1))
        result, output = self.run_gate(*other, allow=True)
        self.assertEqual(0, result.returncode, output)
        self.assertIn("gate passed", output)
        self.assertIn("CMake Warning", output)
        self.assertIn(declared_baseline(), output)
        self.assertIn(".".join(other), output)

    def test_zephyr_module_runs_the_gate(self):
        module = (REPO / "zephyr/CMakeLists.txt").read_text()
        self.assertIn("cmake/CruxZephyrBaseline.cmake", module)
        self.assertIn("crux_check_zephyr_baseline()", module)

    def test_baseline_matches_the_zephyr_pin_comment_in_the_manifest(self):
        pin = re.search(r"^\s*revision:\s*([0-9a-f]{40})\s*#\s*v(\d+\.\d+\.\d+)\s*$",
                        self._zephyr_block(), re.MULTILINE)
        self.assertIsNotNone(pin, "the zephyr revision line in west.yml needs a '# vX.Y.Z' comment")
        self.assertEqual(declared_baseline(), pin.group(2))

    @staticmethod
    def _zephyr_block():
        text = (REPO / "west.yml").read_text()
        start = text.index("    - name: zephyr\n")
        end = text.index("\n    - name:", start + 1)
        return text[start:end]


if __name__ == "__main__":
    unittest.main()
