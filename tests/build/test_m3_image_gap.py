import sys
from pathlib import Path
import struct
import tempfile
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'target'))
from test_m3_image import fixture
from m3_image import check_image


def gap_fixture(root):
    binary,elf,config=fixture(root,'network')
    b=bytearray(binary.read_bytes());e=bytearray(elf.read_bytes())
    b[0x2010:0x2014]=bytes([255])*4
    # Split file-backed sections within the original bounded PT_LOAD.
    struct.pack_into('<I',e,0x2400+40+20,0x2010)
    struct.pack_into('<H',e,48,5)
    e.extend(bytes(40))
    struct.pack_into('<IIIIIIIIII',e,0x24a0,e[0x2200:0x2300].index(b'unused_stack'),1,2,0x60002014,0x2114,0x1c,0,0,4,0)
    binary.write_bytes(b);elf.write_bytes(e)
    return binary,elf,config


class SectionGapChecks(unittest.TestCase):
    def test_only_unowned_alignment_gap_uses_pinned_ff_fill(self):
        with tempfile.TemporaryDirectory() as d:
            paths=gap_fixture(Path(d))
            self.assertTrue(check_image(*paths,'network')['elf_sections_match_bin'])

    def test_code_gap_mapping_and_overlap_corruption_rejected(self):
        for change in ('code','gap','mapping','overlap','duplicate-load'):
            with self.subTest(change=change),tempfile.TemporaryDirectory() as d:
                binary,elf,config=gap_fixture(Path(d))
                if change in ('code','gap'):
                    data=bytearray(binary.read_bytes());data[0x2025 if change=='code' else 0x2011]^=1
                    binary.write_bytes(data)
                else:
                    data=bytearray(elf.read_bytes())
                    if change=='duplicate-load':
                        data[96:128]=data[64:96]
                        struct.pack_into('<H',data,44,2)
                        elf.write_bytes(data)
                        with self.assertRaises(ValueError):check_image(binary,elf,config,'network')
                        continue
                    if change=='mapping':struct.pack_into('<I',data,0x24a0+16,0x2115)
                    else:
                        struct.pack_into('<I',data,0x24a0+12,0x60000000)
                        struct.pack_into('<I',data,0x24a0+16,0x100)
                    elf.write_bytes(data)
                with self.assertRaises(ValueError):check_image(binary,elf,config,'network')


if __name__=='__main__':unittest.main()
