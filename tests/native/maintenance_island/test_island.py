"""Bounded candidate checks. Host tests do not execute Cortex-M instructions."""
import os
import struct
import re
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
SAMPLE = ROOT / 'samples/flash_target_candidate'

class RefusalChecks(unittest.TestCase):
    def test_snapshot_and_copy_refusal(self):
        self.assertTrue((SAMPLE / 'island_checks.h').exists(), 'missing bounded island checks')
        self.assertIn('crux_island_serial_root_supported', (SAMPLE/'island_checks.h').read_text(),
                      'missing pre-clock selector refusal')
        code = r'''
#include "island_checks.h"
#include <assert.h>
int main(void) {
 uint32_t s[16] = {0x50494c54, 1};
 assert(crux_island_snapshot_supported(s));
 for(unsigned root=0;root<4;root++) {
  assert(crux_island_serial_root_supported(root<<29)==(root!=0));
  assert(crux_island_serial_root_supported((root<<29)|0x1fffffffU)==(root!=0));
 }
 const unsigned fields[] = {0,1,2,3,4,5,6,7,8,12,14,15};
 const uint32_t masks[] = {1,2,0x10000,1,4,1,4,1,0x80,1,1,1};
 for(unsigned i=0;i<sizeof(fields)/sizeof(fields[0]);i++) {
  s[fields[i]] ^= masks[i]; assert(!crux_island_snapshot_supported(s));
  s[fields[i]] ^= masks[i];
 }
 unsigned char a[7]={1,2,3,4,5,6,7},b[7]={0};
 crux_island_copy(b,a,7); assert(crux_island_equal(a,b,7));
 for(unsigned i=0;i<7;i++){b[i]^=1;assert(!crux_island_equal(a,b,7));b[i]^=1;}
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / 'case.c'; exe = Path(tmp) / 'case'
            src.write_text(code)
            subprocess.run([os.environ.get('CC','cc'), '-std=c17','-Wall','-Wextra',
                            '-Wpedantic','-Werror','-I'+str(SAMPLE),str(src),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)


@unittest.skipUnless(os.environ.get('PILOT_ELF'), 'requires exact ARM candidate')
class ExactIsland(unittest.TestCase):
    def setUp(self):
        import importlib.util
        spec=importlib.util.spec_from_file_location('assessment', ROOT/'tests/native/flash_target_candidate/assess_image.py')
        self.image=importlib.util.module_from_spec(spec);spec.loader.exec_module(self.image)
        self.elf=Path(os.environ['PILOT_ELF']).read_bytes()
        self.symbols=self.image.symbols(self.elf)
    def body(self,name):
        text=subprocess.check_output([os.environ['ARM_OBJDUMP'],'-d','--disassemble='+name,os.environ['PILOT_ELF']],text=True)
        self.assertIn('<'+name+'>:',text,'missing actual island root')
        return text
    def test_context_and_terminal_are_internal(self):
        self.assertTrue('crux_island_enter' in self.symbols, 'missing RAM context')
        for name in ['crux_island_enter','crux_island_exception_terminal']:
            self.assertLess(self.symbols[name],0x20000)
        for name in ['__island_vectors_start','__island_operation_top','__island_exception_top']:
            self.assertGreaterEqual(self.symbols[name],0x20000000)
            self.assertLess(self.symbols[name],0x20020000)
        self.assertEqual(self.symbols['__island_vectors_start'] % 1024,0)
        self.assertEqual(self.symbols['__island_vectors_end']-self.symbols['__island_vectors_start'],1024)
    def bytes_at(self,address,size):
        h=struct.unpack_from('<16sHHIIIIIHHHHHH',self.elf)
        for i in range(h[10]):
            p=struct.unpack_from('<8I',self.elf,h[5]+i*h[9])
            if p[0]==1 and p[2]<=address and address+size<=p[2]+p[4]:
                return self.elf[p[1]+address-p[2]:p[1]+address-p[2]+size]
        self.fail('range is not file-backed')
    def test_all_vectors_use_internal_terminal_and_exception_stack(self):
        words=struct.unpack('<256I',self.bytes_at(self.symbols['__island_vectors_start'],1024))
        self.assertEqual(words[0],self.symbols['__island_exception_top'])
        self.assertEqual(words[1:],(self.symbols['crux_island_exception_terminal']|1,)*255)
        self.assertEqual(self.symbols['__island_operation_top']-self.symbols['__island_operation_bottom'],1024)
        self.assertEqual(self.symbols['__island_exception_top']-self.symbols['__island_exception_bottom'],1024)
    def test_preclock_gate_precedes_real_hook(self):
        body=self.body('__wrap_soc_early_init_hook')
        self.assertNotRegex(body,r'\b(push|pop|bl|blx|str)\b|\bsp\b')
        self.assertIn('400fc01c',body)
        self.assertRegex(body,r'tst\.w\s+r0, #1610612736')
        self.assertRegex(body,r'beq\.[nw][^\n]*<crux_clock_entry_refused>')
        self.assertIn('<soc_early_init_hook>',body)
        self.assertIn('<__wrap_soc_early_init_hook>',self.body('z_cstart'))
    def test_native_entry_refusal_precedes_native_calls(self):
        for name in ['memc_flexspi_init','flash_flexspi_nor_init']:
            body=self.body(name)
            calls=re.findall(r'\bbl(?:x)?\s+[^<]*<([^>]+)>',body)
            self.assertIn('crux_flash_island_inspect_and_refuse',calls[0])
        for name in ['flash_flexspi_nor_write','flash_flexspi_nor_erase','flash_flexspi_nor_read']:
            calls=re.findall(r'\bbl(?:x)?\s+[^<]*<([^>]+)>',self.body(name))
            self.assertIn('crux_flash_island_refuse',calls[0])
        refusal=self.body('crux_flash_island_refuse')
        self.assertRegex(refusal,r'mvn\.w\s+r0, #12')
        self.assertNotRegex(refusal,r'\b(bl|blx|ldr|str)\b')
        prepare=self.body('crux_flash_island_inspect_and_refuse')
        calls=re.findall(r'\bbl(?:x)?\s+[^<]*<([^>]+)>',prepare)
        self.assertEqual(len(calls),2)
        self.assertEqual(calls[0],'crux_rom_read_confirm_supported')
        self.assertIn('crux_island_enter',calls[1])
        self.assertLess(prepare.index('<crux_rom_read_confirm_supported>'),prepare.index('strb'))
        self.assertIn('ldrb',prepare);self.assertIn('strb',prepare)
        self.assertNotIn('memcpy',prepare)
    def test_entry_only_calls_selected_dispatch_after_ram_setup_and_uses_both_stacks(self):
        body=self.body('crux_island_enter')
        self.assertNotRegex(body,r'\b(blx|vpush|vpop|vldr|vstr)\b')
        self.assertEqual(re.findall(r'\bbl\s+[^<]*<([^>]+)>',body), ['crux_rom_command_dispatch'])
        self.assertLess(body.lower().index('msr\tpsp'),body.index('<crux_rom_command_dispatch>'))
        self.assertRegex(body,r'<crux_rom_command_dispatch>[\s\S]*cmp\s+r0, #0[\s\S]*bne[^\n]*<crux_island_exception_terminal>')
        for token in ['MSP','PSP','CONTROL','PRIMASK','IPSR','dsb','isb']:
            self.assertIn(token.lower(),body.lower())
        self.assertIn('402a8000',body)
        self.assertIn('e000ed08',body)
        # Context literals close setup data; selected command transitive closure is checked separately.
        words=[int(x,16) for x in re.findall(r'\.word\s+0x([0-9a-f]+)',body)]
        self.assertEqual(set(words),{0xe000ed88,0xe000ef34,0xe000ed08,0x402a8000,
            self.symbols['__island_exception_top'],self.symbols['__island_operation_top'],
            self.symbols['__island_vectors_start']})
        # Every direct branch remains inside ITCM; only caller-return uses bx lr.
        for target in re.findall(r'\bb(?:ne|eq)?(?:\.[nw])?\s+([0-9a-f]+)\s+<',body):
            self.assertLess(int(target,16),0x20000)
        self.assertNotIn('blx',body)
        self.assertRegex(body,r'msr\s+MSP, r5[\s\S]*msr\s+CONTROL, r7[\s\S]*msr\s+PSP, r6[\s\S]*msr\s+PRIMASK, r0')
        terminal=self.body('crux_island_exception_terminal')
        self.assertNotRegex(terminal,r'\b(push|pop|bl|blx|ldr|str)\b|\bsp\b')
        self.assertIn('cpsid',terminal)

if __name__ == '__main__': unittest.main()
