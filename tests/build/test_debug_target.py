"""Argument handling of scripts/build-debug-target.sh, with a stand-in west."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

REPO = Path(__file__).resolve().parents[2]
SCRIPT = REPO / "scripts/build-debug-target.sh"
FRAGMENT = REPO / "scripts/debug-optimizations.conf"

# Records its arguments, then makes the ELF that the script expects.
FAKE_WEST = """#!/bin/sh
printf '%s\\n' "$@" > "$FAKE_WEST_LOG"
while [ $# -gt 0 ]; do
    if [ "$1" = "-d" ]; then mkdir -p "$2/zephyr" && : > "$2/zephyr/zephyr.elf" && : > "$2/compile_commands.json"; fi
    shift
done
"""


class DebugTargetTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        root = Path(self.temp.name).resolve()
        self.workspace = root / "workspace"
        self.repo = self.workspace / "repo"
        (self.workspace / ".west").mkdir(parents=True)
        (self.workspace / ".west/config").write_text("")
        (self.workspace / "zephyr").mkdir()
        (self.workspace / "zephyr/zephyr-env.sh").write_text("")
        for application in ("samples/time", "samples/storage_sd"):
            (self.repo / application).mkdir(parents=True)
            (self.repo / application / "CMakeLists.txt").write_text("")
        (self.repo / "scripts").mkdir()
        for name in ("build-debug-target.sh", "select-clangd-database.sh", "debug-optimizations.conf"):
            (self.repo / "scripts" / name).write_text((REPO / "scripts" / name).read_text())
            (self.repo / "scripts" / name).chmod(0o755)
        subprocess.check_call(["git", "-C", str(self.repo), "init", "-q"])
        self.bin = root / "bin"
        self.bin.mkdir()
        west = self.bin / "west"
        west.write_text(FAKE_WEST)
        west.chmod(0o755)
        self.log = root / "west.log"

    def run_script(self, *arguments):
        environment = dict(os.environ, CRUX_WORKSPACE=str(self.workspace),
                           PATH=f"{self.bin}:{os.environ['PATH']}", FAKE_WEST_LOG=str(self.log))
        return subprocess.run(["bash", str(self.repo / "scripts/build-debug-target.sh"), *arguments],
                              cwd=self.repo, env=environment, capture_output=True, text=True)

    def west_arguments(self):
        return self.log.read_text().splitlines()

    def test_default_is_the_time_sample_in_the_unchanged_build_directory(self):
        result = self.run_script("mimxrt1060_evk@B//qspi")
        self.assertEqual(result.returncode, 0, result.stderr)
        arguments = self.west_arguments()
        self.assertEqual(arguments[arguments.index("-d") + 1],
                         str(self.repo / "build/zephyr/mimxrt1060_evk_B_qspi"))
        self.assertIn(str(self.repo / "samples/time"), arguments)
        self.assertFalse(any("EXTRA_CONF_FILE" in argument for argument in arguments))

    def test_application_path_selects_the_application_and_its_own_directory(self):
        result = self.run_script("mimxrt1060_evk@B//qspi", "--app", "samples/storage_sd")
        self.assertEqual(result.returncode, 0, result.stderr)
        arguments = self.west_arguments()
        self.assertEqual(arguments[arguments.index("-d") + 1],
                         str(self.repo / "build/zephyr/mimxrt1060_evk_B_qspi--samples_storage_sd"))
        self.assertIn(str(self.repo / "samples/storage_sd"), arguments)
        self.assertNotIn(str(self.repo / "samples/time"), arguments)

    def test_debug_optimizations_adds_the_fragment_and_a_separate_directory(self):
        result = self.run_script("mimxrt1060_evk@B//qspi", "--debug-optimizations")
        self.assertEqual(result.returncode, 0, result.stderr)
        arguments = self.west_arguments()
        self.assertEqual(arguments[arguments.index("-d") + 1],
                         str(self.repo / "build/zephyr/mimxrt1060_evk_B_qspi--og"))
        self.assertIn(f"-DEXTRA_CONF_FILE={self.repo / 'scripts/debug-optimizations.conf'}", arguments)

    def test_application_and_debug_optimizations_combine(self):
        result = self.run_script("mimxrt1060_evk@B//qspi", "--app", "samples/time", "--debug-optimizations")
        self.assertEqual(result.returncode, 0, result.stderr)
        arguments = self.west_arguments()
        self.assertEqual(arguments[arguments.index("-d") + 1],
                         str(self.repo / "build/zephyr/mimxrt1060_evk_B_qspi--samples_time--og"))

    def test_an_existing_build_directory_is_not_reconfigured(self):
        directory = self.repo / "build/zephyr/mimxrt1060_evk_B_qspi--og"
        directory.mkdir(parents=True)
        (directory / "build.ninja").write_text("")
        result = self.run_script("mimxrt1060_evk@B//qspi", "--debug-optimizations")
        self.assertEqual(result.returncode, 0, result.stderr)
        arguments = self.west_arguments()
        self.assertNotIn("--", arguments)
        self.assertFalse(any("EXTRA_CONF_FILE" in argument for argument in arguments))

    def test_rejects_bad_arguments_without_building(self):
        for arguments in ((), ("--app", "samples/time"), ("board", "--unknown"), ("board", "--app"),
                          ("board", "--app", "samples/missing")):
            with self.subTest(arguments=arguments):
                result = self.run_script(*arguments)
                self.assertEqual(result.returncode, 2, result.stderr)
                self.assertFalse(self.log.exists())

    def test_fragment_sets_only_the_optimisation_level(self):
        lines = [line for line in FRAGMENT.read_text().splitlines() if line and not line.startswith("#")]
        self.assertEqual(lines, ["CONFIG_DEBUG_OPTIMIZATIONS=y"])


if __name__ == "__main__":
    unittest.main()
