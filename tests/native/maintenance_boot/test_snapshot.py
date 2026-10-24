"""Exact compile-only ARM snapshot gate; run with PILOT_ELF and ARM_OBJDUMP."""
import importlib.util
import os
from pathlib import Path
import re
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location('image_assessment', ROOT / 'tests/native/flash_target_candidate/assess_image.py')
image = importlib.util.module_from_spec(spec)
spec.loader.exec_module(image)


@unittest.skipUnless(os.environ.get('PILOT_ELF'), 'requires exact candidate ELF; no hardware')
class EarlySnapshot(unittest.TestCase):
    def setUp(self):
        self.elf = Path(os.environ['PILOT_ELF']).read_bytes()
        self.symbols = image.symbols(self.elf)
        self.disassembly = subprocess.check_output(
            [os.environ['ARM_OBJDUMP'], '-d', os.environ['PILOT_ELF']], text=True)

    def body(self, name):
        output = subprocess.check_output([os.environ['ARM_OBJDUMP'], '-d',
            '--disassemble=' + name, os.environ['PILOT_ELF']], text=True)
        self.assertIn('<' + name + '>:', output, 'missing machine-code root ' + name)
        return output

    def test_snapshot_precedes_real_reset_hook_and_uses_reserved_ram(self):
        self.assertTrue('__wrap_soc_reset_hook' in self.symbols,
                      'reset snapshot not implemented')
        self.assertIn('<__wrap_soc_reset_hook>', self.body('__start'))
        self.assertIn('<soc_reset_hook>', self.body('__wrap_soc_reset_hook'))
        self.assertEqual(self.symbols['pilot_reset_snapshot'], 0x20200000)
        self.assertEqual(self.symbols['pilot_reset_snapshot_end'] -
                         self.symbols['pilot_reset_snapshot'], 64)

    def test_capture_has_no_stack_call_or_watchdog_write(self):
        body = self.body('__wrap_soc_reset_hook')
        self.assertNotRegex(body, r'\b(push|pop|vpush|vpop|bl|blx)\b|\bsp\b')
        # WDOG WCR and WMCR require four halfword loads; all stores use snapshot base r0.
        self.assertEqual(len(re.findall(r'\bldrh', body)), 4)
        for line in body.splitlines():
            if re.search(r'\bstr(?:\.w|h|b)?\s', line):
                self.assertRegex(line, r'\[r0(?:,|\])')
        for constant in ['400b8000', '400d0000', '400bc000', '400f8000',
                         '400ac000', 'e000ed14', 'e000ed94', '20200000']:
            self.assertIn(constant, body)
        self.assertIn('dsb', body)
        # No RTWDOG base should remain in SystemInit when DISABLE_WDOG=0.
        self.assertNotIn('400bc000', self.body('SystemInit'))

    def test_refusal_branches_precede_completed_marker_and_real_hook(self):
        body = self.body('__wrap_soc_reset_hook')
        checks = re.findall(r'tst\.w\s+r1, #(\d+).*?\n[^\n]*bne\.n[^\n]*<pilot_armed_refused>', body)
        self.assertEqual(checks, ['4', '1', '128'])
        first_store = body.index('str\t')
        pre_ram = body[:first_store]
        self.assertEqual(pre_ram.count('<pilot_entry_refused>'), 2)
        self.assertIn('cpsid', pre_ram)
        self.assertLess(body.rindex('<pilot_armed_refused>', 0, body.index('dsb')),
                        body.index('dsb'))
        self.assertLess(body.index('msr'), body.index('<soc_reset_hook>'))


if __name__ == '__main__':
    unittest.main()
