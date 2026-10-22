"""Selected placement positive/negative checks against an actual built ARM ELF."""
import importlib.util
from pathlib import Path
import struct
import sys
import unittest
ROOT=Path(__file__).resolve().parents[3]
SPEC=importlib.util.spec_from_file_location('verify',ROOT/'scripts/verify-flash-poc.py')
VERIFY=importlib.util.module_from_spec(SPEC);SPEC.loader.exec_module(VERIFY)
DATA=Path(sys.argv.pop(1)).read_bytes()
def mutate_symbol(name,value):
    data=bytearray(DATA);h=struct.unpack_from('<16sHHIIIIIHHHHHH',data)
    sections=[struct.unpack_from('<IIIIIIIIII',data,h[6]+i*h[11]) for i in range(h[12])]
    for section in sections:
        if section[1]!=2:continue
        string_section=sections[section[6]]
        strings=data[string_section[4]:string_section[4]+string_section[5]]
        for offset in range(section[4],section[4]+section[5],16):
            index=struct.unpack_from('<I',data,offset)[0]
            if strings[index:].split(b'\0',1)[0].decode()==name:
                struct.pack_into('<I',data,offset+4,value);return data
    raise AssertionError('missing symbol '+name)
class Placements(unittest.TestCase):
    def test_real_elf(self):
        self.assertIn('memc_flexspi_config_0',VERIFY.placements(DATA))
    def test_flash_terminal_rejected(self):
        with self.assertRaisesRegex(ValueError,'terminal'):
            VERIFY.placements(mutate_symbol('crux_flash_poc_terminal',0x60007133))
    def test_flash_config_rejected(self):
        with self.assertRaisesRegex(ValueError,'configuration'):
            VERIFY.placements(mutate_symbol('memc_flexspi_config_0',0x6001b4b0))
if __name__=='__main__':unittest.main()
