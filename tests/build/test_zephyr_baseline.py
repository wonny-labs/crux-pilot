"""Check the Zephyr version gate: its CMake function in script mode, then the real module file."""
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

REPO = Path(__file__).resolve().parents[2]
GATE = REPO / "cmake/CruxZephyrBaseline.cmake"
MOCK = REPO / "tests/consumer_boundary/mock_zephyr.cmake"
CMAKE = shutil.which("cmake")
C_COMPILER = os.environ.get("CC") or shutil.which("cc") or shutil.which("gcc") or shutil.which("clang")
OPTION = "CRUX_REQUIRE_VERIFIED_ZEPHYR"
VERIFIED_ON = "Crux was verified on Zephyr"
CANNOT_READ = "cannot read the Zephyr version"


def declared_baseline():
    match = re.search(r'set\(CRUX_ZEPHYR_BASELINE\s+"(\d+\.\d+\.\d+)"\)', GATE.read_text())
    return match.group(1)


def single_line(result):
    """Join CMake's output, because it wraps a message at any space."""
    return " ".join((result.stdout + result.stderr).split())


def other_releases():
    major, minor, patch = declared_baseline().split(".")
    return ((major, minor, str(int(patch) + 1)), (major, str(int(minor) + 1), patch),
            (str(int(major) + 1), minor, patch))


@unittest.skipUnless(CMAKE, "CMake is required")
class ZephyrBaselineTest(unittest.TestCase):
    def run_gate(self, major, minor, patch, string=None, require=False):
        """Run the check as Zephyr's version.cmake would leave the variables. None leaves one unset."""
        settings = "".join(f"set({name} {value})\n" for name, value in (
            ("KERNEL_VERSION_MAJOR", major), ("KERNEL_VERSION_MINOR", minor), ("KERNEL_PATCHLEVEL", patch))
            if value is not None)
        string = string or f"{major}.{minor}.{patch}"
        with tempfile.TemporaryDirectory() as temporary:
            driver = Path(temporary) / "driver.cmake"
            driver.write_text(
                f'include("{GATE}")\n' + settings +
                f'set(KERNEL_VERSION_STRING "{string}")\n'
                + (f'set({OPTION} ON)\n' if require else '') +
                'crux_check_zephyr_baseline()\n'
                'message(STATUS "gate passed")\n')
            return subprocess.run([CMAKE, "-P", str(driver)], capture_output=True, text=True)

    def test_the_declared_baseline_is_a_plain_version(self):
        self.assertRegex(declared_baseline(), r"^\d+\.\d+\.\d+$")

    def test_matching_version_stays_quiet_with_or_without_the_option(self):
        major, minor, patch = declared_baseline().split(".")
        for require in (False, True):
            with self.subTest(require=require):
                result = self.run_gate(major, minor, patch, require=require)
                output = single_line(result)
                self.assertEqual(0, result.returncode, output)
                self.assertIn("gate passed", output)
                self.assertNotIn("CMake Warning", output)

    def test_a_fork_suffix_stays_quiet_with_or_without_the_option(self):
        major, minor, patch = declared_baseline().split(".")
        for require in (False, True):
            with self.subTest(require=require):
                result = self.run_gate(major, minor, patch, string=f"{declared_baseline()}-fork1", require=require)
                output = single_line(result)
                self.assertEqual(0, result.returncode, output)
                self.assertNotIn("CMake Warning", output)

    def test_another_release_warns_and_continues_by_default(self):
        for other in other_releases():
            with self.subTest(version=other):
                result = self.run_gate(*other)
                output = single_line(result)
                self.assertEqual(0, result.returncode, output)
                self.assertIn("gate passed", output)
                self.assertIn("CMake Warning", output)
                self.assertIn(f"{VERIFIED_ON} {declared_baseline()}", output)
                self.assertIn(".".join(other), output)
                self.assertIn(OPTION, output)

    def test_the_option_makes_another_release_stop_the_build(self):
        for other in other_releases():
            with self.subTest(version=other):
                result = self.run_gate(*other, require=True)
                output = single_line(result)
                self.assertNotEqual(0, result.returncode, output)
                self.assertNotIn("gate passed", output)
                self.assertIn(f"{VERIFIED_ON} {declared_baseline()}", output)
                self.assertIn(".".join(other), output)
                self.assertIn(OPTION, output)

    def test_an_unreadable_version_warns_and_continues_by_default(self):
        major, minor, _ = declared_baseline().split(".")
        result = self.run_gate(major, minor, None, string="unknown")
        output = single_line(result)
        self.assertEqual(0, result.returncode, output)
        self.assertIn("gate passed", output)
        self.assertIn("CMake Warning", output)
        self.assertIn(CANNOT_READ, output)
        self.assertIn(declared_baseline(), output)
        self.assertIn(OPTION, output)

    def test_the_option_makes_an_unreadable_version_stop_the_build(self):
        major, minor, _ = declared_baseline().split(".")
        for missing in ((None, minor, "0"), (major, None, "0"), (major, minor, None)):
            with self.subTest(missing=missing):
                result = self.run_gate(*missing, string="unknown", require=True)
                output = single_line(result)
                self.assertNotEqual(0, result.returncode, output)
                self.assertNotIn("gate passed", output)
                self.assertIn(CANNOT_READ, output)
                self.assertIn(declared_baseline(), output)
                self.assertIn(OPTION, output)

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


@unittest.skipUnless(CMAKE and C_COMPILER, "CMake and a C compiler are required")
class ModuleGateTest(unittest.TestCase):
    """Evaluate the real zephyr/CMakeLists.txt, because a text check cannot see a wrong placement."""

    def configure(self, settings=""):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "CMakeLists.txt").write_text(
                'cmake_minimum_required(VERSION 3.28)\nproject(probe LANGUAGES C)\n'
                f'include("{MOCK}")\n' + settings +
                f'add_subdirectory("{REPO}/zephyr" zephyr-module)\n')
            return subprocess.run([CMAKE, "-S", str(root), "-B", str(root / "build")],
                                  capture_output=True, text=True)

    def test_the_baseline_version_is_quiet(self):
        result = self.configure()
        output = single_line(result)
        self.assertEqual(0, result.returncode, output)
        self.assertNotIn(VERIFIED_ON, output)
        self.assertNotIn(CANNOT_READ, output)

    def test_a_wrong_version_warns_and_continues_by_default(self):
        result = self.configure("set(KERNEL_PATCHLEVEL 99)\n")
        output = single_line(result)
        self.assertEqual(0, result.returncode, output)
        self.assertIn("CMake Warning", output)
        self.assertIn(f"{VERIFIED_ON} {declared_baseline()}", output)
        self.assertIn(OPTION, output)

    def test_a_wrong_version_stops_under_the_required_option(self):
        result = self.configure(f"set(KERNEL_PATCHLEVEL 99)\nset({OPTION} ON)\n")
        output = single_line(result)
        self.assertNotEqual(0, result.returncode, output)
        self.assertIn("CMake Error", output)
        self.assertIn(f"{VERIFIED_ON} {declared_baseline()}", output)

    def test_an_unset_version_stops_under_the_required_option(self):
        for name in ("KERNEL_VERSION_MAJOR", "KERNEL_VERSION_MINOR", "KERNEL_PATCHLEVEL"):
            with self.subTest(name=name):
                result = self.configure(f"unset({name})\nset({OPTION} ON)\n")
                output = single_line(result)
                self.assertNotEqual(0, result.returncode, output)
                self.assertIn("CMake Error", output)
                self.assertIn(CANNOT_READ, output)

    def test_the_module_switch_turns_the_gate_off(self):
        # Kconfig turns the capabilities off with the module, so model that too.
        off = "set(CONFIG_CRUX_PILOT_MODULE OFF)\nset(CONFIG_CRUX_TIME OFF)\nset(KERNEL_PATCHLEVEL 99)\n"
        for settings in (off, off + f"set({OPTION} ON)\n"):
            with self.subTest(settings=settings):
                result = self.configure(settings)
                output = single_line(result)
                self.assertEqual(0, result.returncode, output)
                self.assertNotIn(VERIFIED_ON, output)
                self.assertNotIn(CANNOT_READ, output)
                self.assertNotIn(OPTION, output)


if __name__ == "__main__":
    unittest.main()
