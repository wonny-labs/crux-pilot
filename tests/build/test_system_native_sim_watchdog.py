"""The native_sim System test watchdog (ADR-0005 route A) is test-only and fails closed.

These checks run on the host, without a Zephyr SDK. They read the Kconfig symbol, run the real configure-time
refusal (components/system/ports/zephyr/native_sim_watchdog.cmake) in a small CMake project, and compile the real
binding (components/system/ports/zephyr/system_binding.c) against stub Zephyr headers. The Zephyr-level
refusal on a real board is checked by tests/build/test_system_composition.py, and the simulated device itself
by tests/system/zephyr/native_sim_watchdog, both in the Zephyr runtime shard.
"""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

REPO = Path(__file__).resolve().parents[2]
SYMBOL = "CRUX_SYSTEM_NATIVE_SIM_WATCHDOG"
KCONFIG = REPO / "zephyr/Kconfig"
PORT = REPO / "components/system/ports/zephyr"
CHECK_CMAKE = PORT / "native_sim_watchdog.cmake"
CMAKE = shutil.which("cmake")
CC = shutil.which("cc") or shutil.which("gcc") or shutil.which("clang")


def kconfig_block(name):
    """Return the text of one `config NAME` block of zephyr/Kconfig."""
    text = KCONFIG.read_text()
    match = re.search(rf"^config {name}\n(.*?)(?=^\S)", text + "\nend\n", re.S | re.M)
    return match.group(1) if match else None


class KconfigSymbolTest(unittest.TestCase):
    def setUp(self):
        self.block = kconfig_block(SYMBOL)
        self.assertIsNotNone(self.block, f"zephyr/Kconfig has no config {SYMBOL}")

    def test_it_is_a_bool_for_system_only(self):
        self.assertRegex(self.block, r"(?m)^\s+bool ", "the symbol is a bool")
        self.assertRegex(self.block, r"(?m)^\s+depends on CRUX_SYSTEM\b", "it needs the System service")

    def test_it_has_no_board_dependency(self):
        # A `depends on BOARD_...` line would only warn on another board and a CMake check could not see
        # the hidden symbol. The refusal is the CMake check and the #error of the binding instead.
        self.assertNotRegex(self.block, r"depends on[^\n]*BOARD", "no board dependency")

    def test_it_selects_the_native_sim_restart_on_native_sim_only(self):
        # S5: a missed feed restarts the simulated process. The symbol exists only in the native_sim
        # Kconfig, so the select is conditional and names no symbol of another board.
        self.assertRegex(self.block, r"(?m)^\s+select NATIVE_SIM_REBOOT if BOARD_NATIVE_SIM\s*$")

    def test_the_prompt_and_help_say_native_sim_and_test_only(self):
        prompt = re.search(r'^\s+bool "([^"]+)"', self.block, re.M).group(1)
        self.assertIn("native_sim", prompt)
        self.assertIn("test", prompt.lower())
        help_text = self.block.split("help", 1)[1].lower()
        for phrase in ("native_sim", "experimental", "test", "not hardware"):
            self.assertIn(phrase, help_text)

    def test_the_symbol_is_off_by_default(self):
        self.assertNotRegex(self.block, r"default y")


@unittest.skipUnless(CMAKE, "cmake is not installed")
class ConfigureRefusalTest(unittest.TestCase):
    """The component refuses the symbol on any board but native_sim, at configure time."""

    def configure(self, symbol, board, check=CHECK_CMAKE):
        with tempfile.TemporaryDirectory() as tmp:
            source = Path(tmp) / "src"
            source.mkdir()
            lines = ["cmake_minimum_required(VERSION 3.28)", "project(probe LANGUAGES NONE)"]
            if symbol:
                lines.append(f"set(CONFIG_{SYMBOL} y)")
            if board:
                lines.append(f"set(CONFIG_BOARD_{board} y)")
            lines.append(f'include("{check}")')
            (source / "CMakeLists.txt").write_text("\n".join(lines) + "\n")
            return subprocess.run([CMAKE, "-S", str(source), "-B", str(Path(tmp) / "build")], text=True,
                                  stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=60)

    def assertRefused(self, result):
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertIn(SYMBOL, result.stdout)
        self.assertIn("native_sim", result.stdout)

    def test_the_check_exists(self):
        self.assertTrue(CHECK_CMAKE.is_file(), f"{CHECK_CMAKE} is missing")

    def test_native_sim_is_accepted(self):
        result = self.configure(True, "NATIVE_SIM")
        self.assertEqual(result.returncode, 0, result.stdout)

    def test_other_boards_are_refused(self):
        for board in ("MIMXRT1060_EVK", "MIMXRT1170_EVK", "SOME_OTHER_BOARD"):
            with self.subTest(board=board):
                self.assertRefused(self.configure(True, board))

    def test_no_board_at_all_is_refused(self):
        self.assertRefused(self.configure(True, None))

    def test_without_the_symbol_every_board_is_unchanged(self):
        for board in ("NATIVE_SIM", "MIMXRT1060_EVK", None):
            with self.subTest(board=board):
                result = self.configure(False, board)
                self.assertEqual(result.returncode, 0, result.stdout)

    def test_the_refusal_is_not_vacuous(self):
        # The same project against an empty check configures cleanly, so the refusal above is the check's.
        with tempfile.TemporaryDirectory() as tmp:
            weak = Path(tmp) / "weak.cmake"
            weak.write_text("# no check\n")
            result = self.configure(True, "MIMXRT1060_EVK", check=weak)
            self.assertEqual(result.returncode, 0, result.stdout)

    def test_the_component_includes_the_check_and_builds_the_device_only_under_the_symbol(self):
        text = (PORT / "CMakeLists.txt").read_text()
        self.assertIn("native_sim_watchdog.cmake", text)
        self.assertRegex(text, rf"if\(CONFIG_{SYMBOL}\)[^\n]*\n(?:[^\n]*\n)*?[^\n]*system_sim_watchdog\.c")
        # The device source must not be in the always-built source list.
        always = re.search(r"add_library\(crux_system_zephyr STATIC ([^)]*)\)", text).group(1)
        self.assertNotIn("system_sim_watchdog.c", always)


STUB_HEADERS = {
    "zephyr/device.h": "#pragma once\n#include <stddef.h>\nstruct device { int unused; };\n",
    "zephyr/devicetree.h": "#pragma once\n",
    "zephyr/sys/util.h": "#pragma once\n",
}
DRIVER = r"""
#include "system_zephyr.h"
#include "system_sim_watchdog.h"
#include <stdio.h>
static struct device sim_device;
static int sim_running;
const struct device *crux_system_sim_watchdog_device(void) { return &sim_device; }
bool crux_system_sim_watchdog_running(void) { return sim_running != 0; }
int main(void)
{
    const struct device *before = crux_watchdog_zephyr_device();
    bool running_before = crux_watchdog_zephyr_running();
    sim_running = 1;
    bool running_after = crux_watchdog_zephyr_running();
    printf("%d %d %d %d\n", before == &sim_device, before == NULL, running_before, running_after);
    return 0;
}
"""


@unittest.skipUnless(CC, "no C compiler is installed")
class BindingTest(unittest.TestCase):
    """Compile the real binding against stub Zephyr headers."""

    def build(self, *defines):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        root = Path(tmp.name)
        for name, body in STUB_HEADERS.items():
            path = root / "stub" / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(body)
        (root / "driver.c").write_text(DRIVER)
        command = [CC, "-std=c17", "-Wall", "-Wextra", "-Werror", f"-I{root / 'stub'}", f"-I{PORT}"]
        command += [f"-D{define}" for define in defines]
        command += [str(PORT / "system_binding.c"), str(root / "driver.c"), "-o", str(root / "probe")]
        result = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                timeout=60)
        return result, root / "probe"

    def run_probe(self, *defines):
        result, probe = self.build(*defines)
        self.assertEqual(result.returncode, 0, result.stdout)
        output = subprocess.run([str(probe)], text=True, stdout=subprocess.PIPE, timeout=10).stdout
        return output.split()

    def test_native_sim_without_the_symbol_returns_no_device(self):
        # [bound to the simulated device, NULL, running before, running after]
        self.assertEqual(self.run_probe("CONFIG_BOARD_NATIVE_SIM=1"), ["0", "1", "0", "0"])

    def test_native_sim_with_the_symbol_returns_the_simulated_device_and_its_running_state(self):
        self.assertEqual(self.run_probe("CONFIG_BOARD_NATIVE_SIM=1", f"CONFIG_{SYMBOL}=1"),
                         ["1", "0", "0", "1"])

    def test_another_board_with_the_symbol_stops_at_an_error_naming_it(self):
        for board in ("CONFIG_BOARD_MIMXRT1060_EVK=1", None):
            with self.subTest(board=board):
                defines = [f"CONFIG_{SYMBOL}=1"] + ([board] if board else [])
                result, _ = self.build(*defines)
                self.assertNotEqual(result.returncode, 0, result.stdout)
                self.assertIn("error", result.stdout)
                self.assertIn(SYMBOL, result.stdout)

    def test_the_provider_reports_a_simulation_only_with_the_symbol(self):
        text = (PORT / "system_zephyr.c").read_text()
        body = re.search(r"bool crux_watchdog_is_emulated\(void\)\s*\{(.*?)\}", text, re.S).group(1)
        self.assertIn(f"IS_ENABLED(CONFIG_{SYMBOL})", body)
        self.assertNotRegex(body, r"return\s+true\s*;")


if __name__ == "__main__":
    unittest.main()
