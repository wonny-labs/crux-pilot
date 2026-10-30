"""Actual Zephyr GPIO binding checks; enabled by scripts/test-gpio.sh."""
import os
from pathlib import Path
import subprocess
import unittest

REPO = Path(__file__).resolve().parents[2]


@unittest.skipUnless(os.environ.get("CRUX_GPIO_COMPOSITION_TESTS") == "1",
                     "requires pinned Zephyr environment; run scripts/test-gpio.sh")
class GpioCompositionTest(unittest.TestCase):
    def test_static_binding_cases(self):
        root = REPO / "build/gpio-composition"
        cases = {
            "missing": ('/ { aliases { /delete-property/ crux-gpio0; }; };',
                        "requires a crux-gpio0"),
            "disabled": ('&crux_gpio0 { status = "disabled"; };',
                          "selected resource must be enabled"),
            "malformed": ('/ { aliases { crux-gpio0 = &unsupported; }; '
                          'unsupported: unsupported { status = "okay"; }; };',
                          "selected resource must provide a gpios property"),
            "provider": ("", "unsupported provider"),
            "positive": ("", None),
        }
        selected = os.environ.get("CRUX_GPIO_COMPOSITION_CASE")
        if selected and selected not in cases:
            self.fail(f"Unknown composition case: {selected}")
        workspace = Path(os.environ["CRUX_WORKSPACE"])
        for name, (overlay, diagnostic) in cases.items():
            if selected and name != selected:
                continue
            with self.subTest(case=name):
                case = root / name
                case.mkdir(parents=True, exist_ok=True)
                overlay_path = case / "case.overlay"
                overlay_path.write_text(overlay)
                build = root / f"build-{name}"
                provider = "unsupported" if name == "provider" else "zephyr"
                command = [
                    "west", "build", "--cmake-only", "-p", "always", "-b",
                    "native_sim/native/64", "-d", str(build),
                    str(REPO / "tests/gpio/zephyr"), "--",
                    f"-DZEPHYR_MODULES={REPO}",
                    f"-DCRUX_GPIO_PROVIDER={provider}",
                    f"-DEXTRA_DTC_OVERLAY_FILE={overlay_path}",
                ]
                result = subprocess.run(command, cwd=workspace, text=True,
                                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                        timeout=900)
                if result.returncode == 0:
                    built = subprocess.run(["cmake", "--build", str(build),
                                            "--target", "crux_gpio_zephyr", "-j", "2"],
                                           text=True, stdout=subprocess.PIPE,
                                           stderr=subprocess.STDOUT, timeout=240)
                    result = subprocess.CompletedProcess(command, built.returncode,
                                                         result.stdout + built.stdout)
                if diagnostic is None:
                    self.assertEqual(result.returncode, 0, result.stdout[-6000:])
                else:
                    self.assertNotEqual(result.returncode, 0,
                                        f"{name}: configuration unexpectedly succeeded")
                    self.assertIn(diagnostic, result.stdout,
                                  f"{name}: unrelated configuration failure")
                print(f"{name}: {'build PASS' if diagnostic is None else 'rejected as expected'}",
                      flush=True)


if __name__ == "__main__":
    unittest.main(verbosity=2)
