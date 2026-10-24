"""Exact final ARM artifact checks. Structural proof only; no target execution."""
import importlib.util
import os
from pathlib import Path
import re
import struct
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[3]

@unittest.skipUnless(os.environ.get('PILOT_ELF'), 'requires exact ARM candidate')
class ExactRomProfile(unittest.TestCase):
    def setUp(self):
        spec = importlib.util.spec_from_file_location('image', ROOT / 'tests/native/flash_target_candidate/assess_image.py')
        self.image = importlib.util.module_from_spec(spec); spec.loader.exec_module(self.image)
        self.elf = Path(os.environ['PILOT_ELF']).read_bytes()
        self.symbols = self.image.symbols(self.elf)

    def bytes_at(self, address, size):
        header = struct.unpack_from('<16sHHIIIIIHHHHHH', self.elf)
        for i in range(header[10]):
            kind, offset, vma, physical, length, memory, flags, align = struct.unpack_from(
                '<8I', self.elf, header[5] + i * header[9])
            if kind == 1 and vma <= address and address + size <= vma + length:
                return self.elf[offset + address - vma:offset + address - vma + size]
        self.fail('FCB is not file-backed')

    def body(self, name):
        result = subprocess.check_output([os.environ['ARM_OBJDUMP'], '-d', '--disassemble=' + name,
                                          os.environ['PILOT_ELF']], text=True)
        self.assertIn('<' + name + '>:', result)
        return result

    def assert_call_target(self, actual, expected):
        if actual == expected:
            return
        self.assertEqual(actual, '__' + expected + '_veneer')
        veneer = self.body(actual)
        self.assertIn(f".word\t0x{self.symbols[expected] | 1:08x}", veneer)
        self.assertNotRegex(veneer, r'\b(bl|blx|str|push|pop)\b')

    def test_final_fcb_has_only_the_approved_byte_change_in_elf_and_bin(self):
        original = Path(os.environ['PILOT_ORIGINAL_FCB']).read_bytes()
        self.assertEqual(len(original), 512)
        address = self.symbols['qspi_flash_config']
        self.assertEqual(address, 0x60000000)
        actual = self.bytes_at(address, 512)
        self.assertEqual(original[0x46], 7)
        self.assertEqual(actual[0x46], 6)
        self.assertEqual([i for i in range(512) if original[i] != actual[i]], [0x46])
        binary = Path(os.environ['PILOT_BIN']).read_bytes()
        self.assertEqual(binary[:512], actual)

    def test_consumed_preflight_runs_before_copy_and_internal_context(self):
        body = self.body('crux_flash_island_inspect_and_refuse')
        calls = re.findall(r'\bbl(?:x)?\s+[^<]*<([^>]+)>', body)
        self.assertEqual(len(calls), 3)
        self.assert_call_target(calls[0], 'crux_rom_read_confirm_supported')
        self.assert_call_target(calls[1], 'crux_island_relocations_valid')
        self.assert_call_target(calls[2], 'crux_island_enter')
        # Compiler places copy-loop bodies after the call in address order.
        # Their back edges target the earlier copy tests, before DSB and entry.
        instructions = re.findall(r'^\s*([0-9a-f]+):\s+(?:[0-9a-f]{4}\s+)+\s*([a-z][a-z0-9.]*)\s+([^\n]+)', body, re.M)
        barrier = next(int(address, 16) for address, op, args in instructions if op == 'dsb')
        preflight = next(int(address, 16) for address, op, args in instructions
                         if op in ['bl', 'blx'] and '<crux_island_relocations_valid>' in args)
        # The refusal latch can use a byte store too. Copy stores use a register
        # index; the phase latch uses a fixed global offset.
        stores = [i for i, (_, op, args) in enumerate(instructions)
                  if op.startswith('strb') and re.search(r'\[[^],]+, r\d+\]', args)]
        self.assertEqual(len(stores), 2)
        for index in stores:
            self.assertLess(preflight, int(instructions[index][0], 16))
            back = next(args for _, op, args in instructions[index + 1:] if op in ['b', 'b.n', 'b.w'])
            self.assertTrue(preflight < int(back.split()[0], 16) < barrier)
        self.assertRegex(body, r'mvn\.w\s+r0, #12')
        adapter = self.body('crux_rom_read_confirm_supported')
        self.assertIn('<CLOCK_GetClockRootFreq>', adapter)
        self.assertGreaterEqual(self.symbols['crux_rom_read_confirm_supported'], 0x60000000)
        for name in ['CLOCK_GetClockRootFreq', 'CLOCK_GetFreq', 'CLOCK_GetPllFreq',
                     'CLOCK_GetSysPfdFreq', 'CLOCK_GetUsb1PfdFreq']:
            # Store to stack is ordinary calculation; no MMIO store occurs in audited SDK getters.
            self.assertNotRegex(self.body(name), r'\bstr(?:b|h)?(?:\.w)?\s+[^\n]*\[(?!sp\b)')
        for name in ['memc_flexspi_init', 'flash_flexspi_nor_init']:
            calls = re.findall(r'\bbl(?:x)?\s+[^<]*<([^>]+)>', self.body(name))
            self.assert_call_target(calls[0], 'crux_flash_island_inspect_and_refuse')
        for name in ['flash_flexspi_nor_read', 'flash_flexspi_nor_write', 'flash_flexspi_nor_erase']:
            calls = re.findall(r'\bbl(?:x)?\s+[^<]*<([^>]+)>', self.body(name))
            self.assert_call_target(calls[0], 'crux_flash_island_refuse')

if __name__ == '__main__':
    unittest.main()
