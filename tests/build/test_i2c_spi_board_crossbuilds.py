"""Protect the I2C and SPI board cross-builds for RT1060-EVKB Rev B and RT1170-EVK Rev A."""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]
BOARDS = {
    "mimxrt1060_evk@B//qspi": "mimxrt1060_evk_mimxrt1062_qspi",
    "mimxrt1170_evk@A/mimxrt1176/cm7": "mimxrt1170_evk_mimxrt1176_cm7",
}
ALIASES = {"i2c": "crux-i2c0 = &arduino_i2c;", "spi": "crux-spi0 = &crux_spi_peripheral;"}


def builds(interface):
    text = (ROOT / f"scripts/test-{interface}.sh").read_text()
    return text, re.findall(r"\(cd \"\$workspace\" && west build.*?\)\n", text, re.S)


class BoardCrossBuildTests(unittest.TestCase):
    def test_each_board_has_an_overlay_selecting_the_crux_alias(self):
        for interface, alias in ALIASES.items():
            for stem in BOARDS.values():
                overlay = ROOT / f"tests/{interface}/evkb/boards/{stem}.overlay"
                self.assertIn(alias, overlay.read_text())

    def test_runner_builds_every_board_once_from_the_same_source(self):
        for interface in ALIASES:
            text, commands = builds(interface)
            self.assertEqual(len(commands), len(BOARDS))
            for board, command in zip(BOARDS, commands):
                self.assertIn(f"-b '{board}'", command)
                self.assertIn(f'"$repo/tests/{interface}/evkb"', command)
            directories = {re.search(r'-d "\$repo/(build/[^"]+)"', c).group(1) for c in commands}
            self.assertEqual(len(directories), len(BOARDS))

    def test_injected_configuration_is_cleared_before_the_board_builds(self):
        for interface in ALIASES:
            text, commands = builds(interface)
            guard = text.index("unset DTC_OVERLAY_FILE EXTRA_DTC_OVERLAY_FILE")
            self.assertLess(guard, text.index("west build", text.index("mapfile")))
            self.assertNotIn("DTC_OVERLAY_FILE=", "".join(commands))

    def test_runners_never_operate_hardware(self):
        for interface in ALIASES:
            text, _ = builds(interface)
            self.assertNotRegex(text, r"west flash|west debug|pyocd|JLink")

    def test_the_applications_use_only_the_portable_consumers(self):
        for interface, source in (("i2c", "tmp117.c"), ("spi", "spi_flash_id.c")):
            cmake = (ROOT / f"tests/{interface}/evkb/CMakeLists.txt").read_text()
            self.assertIn(f"../consumer/{source}", cmake)
            self.assertIn("crux_assert_portable_consumer", cmake)

    def test_spi_peripheral_node_has_a_binding_and_no_chip_select_hold(self):
        binding = (ROOT / "tests/spi/evkb/dts/bindings/vnd,crux-spi-peripheral.yaml").read_text()
        self.assertIn('compatible: "vnd,crux-spi-peripheral"', binding)
        for stem in BOARDS.values():
            overlay = (ROOT / f"tests/spi/evkb/boards/{stem}.overlay").read_text()
            self.assertIn('compatible = "vnd,crux-spi-peripheral";', overlay)
            self.assertNotIn("spi-hold-cs", overlay)


if __name__ == "__main__":
    unittest.main()
