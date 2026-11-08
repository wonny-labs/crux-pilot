"""Host checks for the native_sim apps that reach the GPIO provider's native failure paths."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
APPS = ROOT / "tests/gpio/zephyr_failure"


def lines(path):
    return [line.strip() for line in path.read_text().splitlines() if line.strip()]


class GpioFailureAppTests(unittest.TestCase):
    def test_runner_runs_the_failure_apps(self):
        script = (ROOT / "scripts/test-gpio.sh").read_text()
        self.assertIn('-T "$repo/tests/gpio/zephyr_failure"', script)

    def test_both_apps_disable_assertions_and_use_the_emulator(self):
        # Zephyr's generic GPIO layer asserts on these requests before the driver can return
        # an error, so the apps must not run with assertions enabled.
        for name in ("open_drain", "pin_range"):
            with self.subTest(app=name):
                conf = lines(APPS / name / "prj.conf")
                self.assertIn("CONFIG_ASSERT=n", conf)
                self.assertIn("CONFIG_GPIO_EMUL=y", conf)
                self.assertIn("CONFIG_CRUX_GPIO=y", conf)
                self.assertNotIn("CONFIG_ASSERT=y", conf)

    def test_open_drain_overlay_requests_open_drain_on_the_selected_line(self):
        overlay = (APPS / "open_drain/app.overlay").read_text()
        self.assertIn("crux-gpio0 = &crux_gpio0;", overlay)
        self.assertIn("GPIO_ACTIVE_LOW | GPIO_OPEN_DRAIN", overlay)

    def test_pin_range_overlay_selects_a_pin_the_controller_does_not_have(self):
        overlay = (APPS / "pin_range/app.overlay").read_text()
        self.assertIn("ngpios = <8>;", overlay)
        self.assertIn("&gpio0 31 ", overlay)

    def test_each_app_checks_the_status_and_the_unconfigured_line(self):
        expectations = {
            "open_drain": "CRUX_ERR_NOT_SUPPORTED",
            "pin_range": "CRUX_ERR_IO",
        }
        for name, status in expectations.items():
            with self.subTest(app=name):
                source = (APPS / name / "main.c").read_text()
                self.assertIn(f"crux_gpio_configure_input(CRUX_GPIO_LINE_0), {status}", source)
                self.assertIn("CRUX_ERR_NOT_READY", source)

    def test_each_app_has_a_native_sim_testcase(self):
        for name in ("open_drain", "pin_range"):
            with self.subTest(app=name):
                text = (APPS / name / "testcase.yaml").read_text()
                self.assertIn("native_sim/native/64", text)
                self.assertIn(f"crux.gpio.zephyr_failure.{name}", text)


if __name__ == "__main__":
    unittest.main()
