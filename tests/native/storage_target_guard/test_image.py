"""The sample build gate rejects ELF/BIN mismatch as well as bounds errors."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'build'))
from test_flash_bounds import fixture
from verify_image import verify


class ImageGate(unittest.TestCase):
    def test_matching_ram_load_image(self):
        elf, binary = fixture()
        self.assertEqual(verify(elf, binary)['image_bytes'], 0x2040)

    def test_changed_code_in_bin_rejected(self):
        elf, binary = fixture()
        binary[0x2008] = 1
        with self.assertRaisesRegex(ValueError, 'contents'):
            verify(elf, binary)

    def test_changed_ram_initial_load_in_elf_rejected(self):
        elf, binary = fixture()
        elf[0x2131] = 1
        with self.assertRaisesRegex(ValueError, 'contents'):
            verify(elf, binary)

    def test_cli_rejects_mismatch_and_reads_only(self):
        elf, binary = fixture()
        with tempfile.TemporaryDirectory() as tmp:
            ep, bp = Path(tmp) / 'image.elf', Path(tmp) / 'image.bin'
            ep.write_bytes(elf)
            binary[0x2008] = 2
            bp.write_bytes(binary)
            run = subprocess.run([sys.executable, str(Path(__file__).with_name('verify_image.py')),
                                  '--elf', str(ep), '--bin', str(bp)], capture_output=True, timeout=5)
            self.assertEqual(run.returncode, 1)
            self.assertEqual(ep.read_bytes(), elf)
            self.assertEqual(bp.read_bytes(), binary)


if __name__ == '__main__':
    unittest.main()
