"""Deliberate invalid dependencies must fail the maintained architecture check."""
from pathlib import Path
import subprocess
import tempfile
import unittest

REPO = Path(__file__).resolve().parents[2]


class DependencyTest(unittest.TestCase):
    def test_portable_controller_rejects_native_coupling(self):
        probes = ['#include <zephyr/kernel.h>\n', '#include <board.h>\n',
                  '#include <fsl_gpio.h>\n', '#if defined(__ZEPHYR__)\n#endif\n']
        for source in probes:
            with self.subTest(source=source):
                with tempfile.NamedTemporaryFile(mode="w", suffix=".c", prefix="boundary_probe_",
                                                 dir=REPO / "samples/controller/core") as probe:
                    probe.write(source)
                    probe.flush()
                    result = subprocess.run([str(REPO / "scripts/check-dependencies.sh")],
                                            cwd=REPO, capture_output=True, text=True)
                    self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        subprocess.run([str(REPO / "scripts/check-dependencies.sh")], cwd=REPO, check=True,
                       capture_output=True)
