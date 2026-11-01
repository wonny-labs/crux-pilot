"""Actual Zephyr SPI binding checks; enabled by scripts/test-spi.sh."""
import os
from pathlib import Path
import subprocess
import unittest

REPO = Path(__file__).resolve().parents[2]


@unittest.skipUnless(os.environ.get("CRUX_SPI_COMPOSITION_TESTS") == "1",
                     "requires pinned Zephyr environment; run scripts/test-spi.sh")
class SpiCompositionTest(unittest.TestCase):
    def test_static_binding_cases(self):
        root = REPO / "build/spi-composition"
        cases = {
            "missing": ('/ { aliases { /delete-property/ crux-spi0; }; };',
                        "must provide the crux-spi0 alias"),
            "disabled": ('/ { aliases { crux-spi0 = &crux_spi_disabled; }; '
                         'crux_spi_disabled: crux-spi-disabled { status = "disabled"; }; };',
                         "must select an enabled SPI peripheral"),
            "hold_cs": ('&crux_spi_test_target { spi-hold-cs; };',
                        "must not request spi-hold-cs"),
            "provider": ("", "unsupported provider"),
            "positive": ("", None),
        }
        selected = os.environ.get("CRUX_SPI_COMPOSITION_CASE")
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
                    "-d", str(build), str(REPO / "tests/spi_zephyr"), "--",
                    f"-DZEPHYR_MODULES={REPO}",
                    f"-DCRUX_SPI_PROVIDER={provider}",
                    f"-DEXTRA_DTC_OVERLAY_FILE={overlay_path}",
                ]
                result = subprocess.run(command, cwd=workspace, text=True,
                                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                        timeout=900)
                if result.returncode == 0:
                    built = subprocess.run(["cmake", "--build", str(build),
                                            "--target", "crux_spi_zephyr", "-j", "2"],
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
