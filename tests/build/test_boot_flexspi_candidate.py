"""Check the files of the #165 candidate (FlexSPI configuration object moved to the XIP memory).

NOT BUILT, NOT VERIFIED, NOT FOR MERGE. These rules read files and run the one CMake fragment with
stub Zephyr functions, so they show only that the fragment is off by default, asks for the one
relocation that docs/plans/mcuboot-flexspi-write-path-closure.md (PR #491, not yet on main) proposes, and refuses a
configuration that cannot honour it. They show nothing about a Zephyr build, the generated linker
script or the board. They use the standard library, because `host.yml` runs this directory without
west or PyYAML. The CMake rules are skipped where `cmake` is not installed.
"""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

REPO = Path(__file__).resolve().parents[2]
BOOT = REPO / "zephyr/boot"
KCONFIG = BOOT / "Kconfig"
FRAGMENT = BOOT / "mcuboot-flexspi-config-in-ram.conf"
CMAKE_FILE = BOOT / "flexspi_config_relocation.cmake"
SYMBOL = "CRUX_BOOT_FLEXSPI_CONFIG_IN_RAM"


def read(path):
    return path.read_text(encoding="utf-8")


class SymbolTest(unittest.TestCase):
    def test_the_symbol_is_off_by_default_and_says_it_is_a_candidate(self):
        text = read(KCONFIG)
        match = re.search(r"config %s\n((?:\t.*\n|\n)+)" % SYMBOL, text)
        self.assertIsNotNone(match, "the symbol is not defined")
        body = match.group(1)
        self.assertIn("bool", body)
        self.assertIn("default n", body)
        self.assertIn("depends on CRUX_PILOT_MODULE && MEMC_MCUX_FLEXSPI", body)
        self.assertIn("Not built", body)

    def test_the_module_kconfig_sources_the_boot_file(self):
        self.assertIn('rsource "boot/Kconfig"', read(REPO / "zephyr/Kconfig"))

    def test_the_fragment_only_turns_the_symbol_on(self):
        lines = [l for l in read(FRAGMENT).splitlines() if l.strip() and not l.lstrip().startswith("#")]
        self.assertEqual(["CONFIG_%s=y" % SYMBOL], lines)

    def test_the_module_cmake_includes_the_relocation_file(self):
        self.assertIn("boot/flexspi_config_relocation.cmake", read(REPO / "zephyr/CMakeLists.txt"))


@unittest.skipUnless(shutil.which("cmake"), "needs cmake to run the fragment with stubs")
class FragmentTest(unittest.TestCase):
    def run_fragment(self, **variables):
        stubs = (
            "function(zephyr_code_relocate)\n"
            '  message(STATUS "RELOCATE:${ARGV}")\n'
            "endfunction()\n"
        )
        sets = "".join('set(%s "%s")\n' % (k, v) for k, v in variables.items())
        with tempfile.TemporaryDirectory() as tmp:
            script = Path(tmp) / "driver.cmake"
            script.write_text(
                'set(ZEPHYR_BASE "/zephyr")\n' + stubs + sets + 'include("%s")\n' % CMAKE_FILE,
                encoding="utf-8",
            )
            return subprocess.run(["cmake", "-P", str(script)], capture_output=True, text=True)

    def test_nothing_is_relocated_when_the_symbol_is_off(self):
        result = self.run_fragment()
        self.assertEqual(0, result.returncode, result.stderr)
        self.assertNotIn("RELOCATE:", result.stderr + result.stdout)

    def test_one_relocation_of_the_flexspi_config_object_when_it_is_on(self):
        result = self.run_fragment(
            **{
                "CONFIG_" + SYMBOL: "y",
                "CONFIG_FLASH": "y",
                "CONFIG_CODE_DATA_RELOCATION": "y",
                "CONFIG_MEMC_MCUX_FLEXSPI": "y",
                "CONFIG_FLASH_MCUX_FLEXSPI_XIP": "y",
                "CONFIG_FLASH_MCUX_FLEXSPI_XIP_MEM": "ITCM",
            }
        )
        self.assertEqual(0, result.returncode, result.stderr)
        output = result.stderr + result.stdout
        self.assertEqual(1, output.count("RELOCATE:"), output)
        line = next(l for l in output.splitlines() if "RELOCATE:" in l)
        self.assertIn("/zephyr/drivers/memc/memc_mcux_flexspi.c", line)
        self.assertIn("LOCATION;ITCM_RODATA", line)
        self.assertIn("FILTER;", line)
        self.assertIn("memc_flexspi_config_", line)

    def test_it_refuses_a_configuration_without_the_xip_driver(self):
        complete = {
            "CONFIG_" + SYMBOL: "y",
            "CONFIG_FLASH": "y",
            "CONFIG_CODE_DATA_RELOCATION": "y",
            "CONFIG_MEMC_MCUX_FLEXSPI": "y",
            "CONFIG_FLASH_MCUX_FLEXSPI_XIP": "y",
            "CONFIG_FLASH_MCUX_FLEXSPI_XIP_MEM": "ITCM",
        }
        for missing in (
            "CONFIG_FLASH_MCUX_FLEXSPI_XIP", "CONFIG_FLASH", "CONFIG_CODE_DATA_RELOCATION", "CONFIG_MEMC_MCUX_FLEXSPI"
        ):
            variables = {k: v for k, v in complete.items() if k != missing}
            result = self.run_fragment(**variables)
            self.assertNotEqual(0, result.returncode, missing)
            self.assertIn(SYMBOL, result.stderr + result.stdout, missing)


class ScopeTest(unittest.TestCase):
    def test_the_board_directory_still_holds_only_the_four_files(self):
        board = REPO / "zephyr/boot/boards/mimxrt1060_evk_mimxrt1062_qspi_B"
        self.assertEqual(
            ["app.conf", "app.overlay", "mcuboot.conf", "mcuboot.overlay"],
            sorted(p.name for p in board.iterdir()),
        )


if __name__ == "__main__":
    unittest.main()
