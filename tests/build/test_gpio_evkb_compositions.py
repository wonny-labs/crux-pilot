"""Protect the two RT1060-EVKB compositions of the portable GPIO consumer."""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]
RUNNER = ROOT / "scripts/test-gpio.sh"
APP = ROOT / "tests/gpio/evkb"
BOARD_OVERLAY = APP / "boards/mimxrt1060_evk_mimxrt1062_qspi.overlay"
SECOND_OVERLAY = APP / "compositions/arduino_d2.overlay"


def evkb_build_commands():
    text = RUNNER.read_text()
    return [
        block
        for block in re.findall(r"\(cd \"\$workspace\" && west build.*?\)\n", text, re.S)
        if "mimxrt1060_evk@B//qspi" in block
    ]


class GpioEvkbCompositionTests(unittest.TestCase):
    def test_board_overlay_selects_the_user_button(self):
        overlay = BOARD_OVERLAY.read_text()
        self.assertIn("crux-gpio0 = &user_button;", overlay)

    def test_second_overlay_selects_a_different_line_and_polarity(self):
        overlay = SECOND_OVERLAY.read_text()
        self.assertIn("crux-gpio0 = &crux_arduino_d2;", overlay)
        self.assertNotIn("user_button", re.sub(r"/\*.*?\*/", "", overlay, flags=re.S))
        self.assertRegex(
            overlay, r"gpios\s*=\s*<&arduino_header\s+ARDUINO_HEADER_R3_D2\s+GPIO_ACTIVE_HIGH>"
        )
        # The header pins that the I2C and SPI compositions need must stay unselected.
        for forbidden in ("_D10", "_D11", "_D12", "_D13", "_D14", "_D15", "_A4", "_A5"):
            self.assertNotIn(forbidden, overlay)

    def test_second_overlay_node_has_a_binding_for_its_gpios_property(self):
        # Without a compatible (here a gpio-keys parent) the generated Devicetree header has no
        # gpios property and the provider's DT_NODE_HAS_PROP check fails the build.
        overlay = re.sub(r"/\*.*?\*/", "", SECOND_OVERLAY.read_text(), flags=re.S)
        parent = re.search(r"(\w+)\s*\{\s*compatible\s*=\s*\"gpio-keys\";(.*?)\n\t\};", overlay, re.S)
        self.assertIsNotNone(parent, "the selected node must be a child of a gpio-keys node")
        self.assertIn("crux_arduino_d2:", parent.group(2))
        self.assertIn("gpios", parent.group(2))

    def test_runner_builds_both_compositions_from_the_same_source(self):
        commands = evkb_build_commands()
        self.assertEqual(len(commands), 2)
        for command in commands:
            self.assertIn('"$repo/tests/gpio/evkb"', command)
        first, second = commands
        self.assertNotIn("EXTRA_DTC_OVERLAY_FILE", first)
        self.assertIn(
            '-DEXTRA_DTC_OVERLAY_FILE="$repo/tests/gpio/evkb/compositions/arduino_d2.overlay"',
            second,
        )

    def test_compositions_use_separate_build_directories(self):
        directories = [
            re.search(r'-d "\$repo/(build/[^"]+)"', command).group(1)
            for command in evkb_build_commands()
        ]
        self.assertEqual(len(set(directories)), 2)

    def test_application_has_no_composition_specific_source(self):
        # One main.c and one portable consumer serve every composition.
        sources = sorted(p.name for p in APP.glob("*.c"))
        self.assertEqual(sources, ["main.c"])
        self.assertFalse((APP / "app.overlay").exists())

    def test_runner_does_not_operate_hardware(self):
        text = RUNNER.read_text()
        self.assertIsNone(re.search(r"\bwest\s+(flash|debug|debugserver|attach|reset)\b", text))
        self.assertNotIn("--runner", text)


if __name__ == "__main__":
    unittest.main()
