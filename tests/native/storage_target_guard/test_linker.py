"""Exercise the exact sample linker fence with bounded synthetic image sizes."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


@unittest.skipUnless(sys.platform == 'linux', 'GNU Linux linker gate')
class LinkerFence(unittest.TestCase):
    def test_exact_envelope_and_one_byte_growth(self):
        fragment = Path(__file__).resolve().parents[3] / 'samples/flash_target_candidate/firmware_limit.ld'
        with tempfile.TemporaryDirectory() as tmp:
            src, obj, exe = [Path(tmp) / name for name in ('entry.c', 'entry.o', 'entry')]
            src.write_text('void _start(void) {}\n')
            subprocess.run(['cc', '-c', str(src), '-o', str(obj)], check=True)
            for size, expected in ((0x620000, 0), (0x620001, 1)):
                with self.subTest(size=size):
                    run = subprocess.run(['ld', '--defsym', '_flash_used=' + str(size),
                                          '-T', str(fragment), str(obj), '-o', str(exe)],
                                         capture_output=True)
                    self.assertEqual(run.returncode, expected)
                    if expected:
                        self.assertIn(b'protected envelope', run.stderr)


if __name__ == '__main__':
    unittest.main()
