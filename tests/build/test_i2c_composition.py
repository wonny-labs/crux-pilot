"""Actual Zephyr I2C binding checks; enabled by scripts/test-i2c.sh."""
import os
from pathlib import Path
import subprocess
import unittest

REPO = Path(__file__).resolve().parents[2]


@unittest.skipUnless(os.environ.get("CRUX_I2C_COMPOSITION_TESTS") == "1",
                     "requires pinned Zephyr environment; run scripts/test-i2c.sh")
class I2cCompositionTest(unittest.TestCase):
    def test_static_binding_cases(self):
        root = REPO / "build/i2c-composition"
        cases = {
            "missing": ('/ { aliases { /delete-property/ crux-i2c0; }; };',
                        "must provide the crux-i2c0 alias"),
            "disabled": ('/ { aliases { crux-i2c0 = &crux_i2c_disabled; }; '
                         'crux_i2c_disabled: crux-i2c-disabled { status = "disabled"; }; };',
                         "must select an enabled I2C controller"),
            "provider": ("", "unsupported provider"),
            "positive": ("", None),
        }
        selected = os.environ.get("CRUX_I2C_COMPOSITION_CASE")
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
                    "west", "build", "--cmake-only", "-p", "always", "-b", "native_sim",
                    "-d", str(build), str(REPO / "tests/i2c_zephyr"), "--",
                    f"-DZEPHYR_MODULES={REPO}",
                    f"-DCRUX_I2C_PROVIDER={provider}",
                    f"-DEXTRA_DTC_OVERLAY_FILE={overlay_path}",
                ]
                result = subprocess.run(command, cwd=workspace, text=True,
                                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                        timeout=900)
                if result.returncode == 0:
                    built = subprocess.run(["cmake", "--build", str(build),
                                            "--target", "crux_i2c_zephyr", "-j", "2"],
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
