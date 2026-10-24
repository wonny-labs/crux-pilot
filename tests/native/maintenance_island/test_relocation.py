"""Actual preflight and init verifier with controlled Linux RAM/load arrays.

Only Cortex-M barriers and SYS_INIT registration are substituted. Linker symbol
values select mmap-backed arrays; rejected addresses deliberately remain unmapped.
"""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
SAMPLE = ROOT / 'samples/flash_target_candidate'
SYMBOLS = {
    '__itcm_text_reloc_start': 0x10000, '__itcm_text_reloc_size': 16,
    '__itcm_text_rom_start': 0x60001000,
    '__dtcm_rodata_reloc_start': 0x20000000, '__dtcm_rodata_reloc_size': 16,
    '__dtcm_rodata_rom_start': 0x60002000,
    '__dtcm_data_reloc_start': 0x20000010, '__dtcm_data_reloc_size': 16,
    '__dtcm_data_rom_start': 0x60003000,
    '__dtcm_bss_reloc_start': 0x20000020, '__dtcm_bss_reloc_size': 16,
    '__island_text_start': 0x10010, '__island_text_end': 0x10020,
    '__island_text_load': 0x60004000,
    '__island_vectors_start': 0x20000400, '__island_vectors_end': 0x20000800,
    '__island_vectors_load': 0x60005000,
    '__island_operation_bottom': 0x20000800, '__island_exception_top': 0x20001000,
}
FIXTURE = r'''
#include <assert.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <string.h>
static int entries, confirmations, barriers;
const volatile uint32_t pilot_reset_snapshot[16] = {0x50494c54U, 1};
int crux_island_enter(void) { ++entries; return 0; }
bool crux_rom_read_confirm_supported(void) { ++confirmations; return true; }
static void arrays(void) {
 const uintptr_t bases[] = {0x10000, 0x20000000, 0x60000000};
 const size_t sizes[] = {0x10000, 0x20000, 0x10000};
 for(unsigned i=0;i<3;i++) assert(mmap((void *)bases[i],sizes[i],PROT_READ|PROT_WRITE,
  MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0)==(void *)bases[i]);
 memset((void *)0x60000000,0x5a,0x10000);
 memcpy((void *)0x10000,(void *)0x60001000,16);
 memcpy((void *)0x20000000,(void *)0x60002000,16);
 memcpy((void *)0x20000010,(void *)0x60003000,16);
}
int main(int argc, char **argv) {
 assert(argc==4); arrays();
 unsigned mode=atoi(argv[1]),region=atoi(argv[2]),edge=atoi(argv[3]);
 const uintptr_t regions[]={0x10000,0x20000000,0x20000010,0x20000020};
 volatile unsigned char *byte=(volatile unsigned char *)(regions[region]+edge);
 if(mode==0) {
  assert(crux_island_relocation_init()==0);
  assert(crux_flash_island_inspect_and_refuse()==-EACCES && entries==1);
  /* Legitimate preparation/read-result state changes must survive re-entry. */
  *(volatile unsigned char *)0x20000010=0x37;
  *(volatile unsigned char *)0x20000020=0x48;
  assert(crux_flash_island_inspect_and_refuse()==-EACCES && entries==2);
  assert(*(volatile unsigned char *)0x20000010==0x37);
  assert(*(volatile unsigned char *)0x20000020==0x48);
 } else if(mode==1) {
  *byte^=1; assert(crux_island_relocation_init()==-EACCES);
  *byte^=1; assert(crux_island_relocation_init()==-EACCES);
  assert(crux_flash_island_inspect_and_refuse()==-EACCES && entries==0);
 } else if(mode==2) {
  assert(crux_island_relocation_init()==0); *byte^=1;
  assert(crux_flash_island_inspect_and_refuse()==-EACCES && entries==0);
  *byte^=1;
  assert(crux_flash_island_inspect_and_refuse()==-EACCES && entries==0);
 } else if(mode==3) {
  assert(crux_flash_island_inspect_and_refuse()==-EACCES && entries==0);
  assert(crux_island_relocation_init()==-EACCES);
  assert(crux_flash_island_inspect_and_refuse()==-EACCES && entries==0);
 } else if(mode==4) {
  assert(crux_island_relocation_init()==-EACCES);
  assert(crux_flash_island_inspect_and_refuse()==-EACCES && entries==0);
  assert(barriers==0);
 } else if(mode==5) {
  assert(crux_island_relocation_init()==0);
  assert(crux_island_relocation_init()==-EACCES);
  assert(crux_flash_island_inspect_and_refuse()==-EACCES && entries==0);
 } else if(mode==6) {
  assert(crux_island_relocation_init()==0);
  postcopy_address=(region==0 ? 0x10010U : 0x20000400U)+edge;
  assert(crux_flash_island_inspect_and_refuse()==-EACCES && entries==0);
  assert(barriers==1);
  postcopy_address=0;
  assert(crux_flash_island_inspect_and_refuse()==-EACCES && entries==0);
  assert(barriers==1);
 }
 return 0;
}
'''


class RelocationChecks(unittest.TestCase):
    def run_case(self, mode=0, region=0, edge=0, symbols=None):
        source = (SAMPLE / 'island.c').read_text()
        self.assertIn('crux_island_relocation_init', source,
                      'missing actual startup/consumed relocation verification')
        barrier = '__asm__ volatile("dsb\\n\\tisb" ::: "memory");'
        self.assertEqual(source.count(barrier), 1)
        source = source.replace(barrier, '++barriers; if(postcopy_address) '
                                '*(volatile unsigned char *)postcopy_address^=1;')
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root/'zephyr').mkdir()
            (root/'zephyr/init.h').write_text('#define SYS_INIT(fn, level, priority)\n')
            code = ('#define _GNU_SOURCE\n#include <stdint.h>\nstatic int barriers;\n'
                    'static uintptr_t postcopy_address;\n')
            # declarations before the actual adapter's definitions
            code += 'int crux_island_enter(void);\n'
            code += source + FIXTURE.replace(', barriers;', ';')
            (root/'case.c').write_text(code)
            definitions = SYMBOLS | (symbols or {})
            subprocess.run(['cc', '-std=c17', '-Wall', '-Wextra', '-Werror', '-no-pie',
                '-I'+str(root), '-I'+str(SAMPLE), str(root/'case.c'), '-o', str(root/'case'),
                *[f'-Wl,--defsym={name}={value}' for name,value in definitions.items()]], check=True)
            subprocess.run([str(root/'case'), str(mode), str(region), str(edge)], check=True)

    def test_actual_preflight_and_phase_correct_mutable_reentry(self):
        self.run_case()

    def test_initial_corruption_first_and_last_byte_each_region_latches_refusal(self):
        for region in range(4):
            for edge in [0, 15]:
                with self.subTest(region=region, edge=edge):
                    self.run_case(1, region, edge)

    def test_each_immutable_region_rechecked_before_context_entry(self):
        for region in range(2):
            for edge in [0, 15]:
                with self.subTest(region=region, edge=edge):
                    self.run_case(2, region, edge)

    def test_unsupported_preinit_entry_and_repeated_init_latch_refusal(self):
        self.run_case(3)
        self.run_case(5)

    def test_original_assembly_and_vector_comparison_retained(self):
        for region,size in [(0,16),(1,1024)]:
            for edge in [0,size-1]:
                with self.subTest(region=region,edge=edge):
                    self.run_case(6,region,edge)

    def test_invalid_ranges_refuse_before_any_memory_access(self):
        bad = [
            {'__itcm_text_reloc_start': 0x20000000},
            {'__dtcm_rodata_reloc_start': 0x10000},
            {'__dtcm_data_reloc_start': 0x60000000},
            {'__dtcm_bss_reloc_start': 0x20020000},
            {'__dtcm_rodata_reloc_start': 0x2001fff8},
            {'__itcm_text_reloc_start': 0x1fff8},
            {'__itcm_text_rom_start': 0x50000000},
            {'__dtcm_rodata_rom_start': 0x6061fff8},
            {'__dtcm_rodata_rom_start': 0x20000000},
            {'__dtcm_data_rom_start': 0xfffffffffffffff0},
            {'__itcm_text_reloc_size': 0},
            {'__dtcm_rodata_reloc_size': 0},
            {'__dtcm_data_reloc_size': 0},
            {'__dtcm_bss_reloc_size': 0},
            {'__itcm_text_reloc_size': 0xfffffffffffffff0},
            {'__dtcm_rodata_reloc_size': 0xfffffffffffffff0},
            {'__dtcm_data_reloc_size': 0xfffffffffffffff0},
            {'__dtcm_bss_reloc_size': 0xfffffffffffffff0},
            {'__dtcm_data_reloc_start': 0x20000011},
            {'__itcm_text_rom_start': 0x60001001},
            {'__dtcm_bss_reloc_size': 15},
            {'__dtcm_data_reloc_start': 0x20000000},
            {'__island_text_start': 0x10000},
            {'__island_text_end': 0xfffffffffffffff0},
            {'__island_text_end': 0x1000c},
            {'__island_text_load': 0x60001000},
            {'__island_vectors_start': 0x20000000, '__island_vectors_end': 0x20000400},
            {'__island_vectors_start': 0x20000404},
            {'__island_vectors_end': 0x200007fc},
            {'__island_vectors_end': 0x200003fc},
            {'__island_vectors_load': 0x50000000},
            {'__island_operation_bottom': 0x20000000},
            {'__island_exception_top': 0x200007fc},
        ]
        for values in bad:
            with self.subTest(symbols=values):
                self.run_case(4, symbols=values)

    def test_zero_itcm_start_is_valid_before_byte_comparison(self):
        # Address zero is a legitimate Cortex-M ITCM VMA. Linux cannot map it,
        # so use the actual pure extent predicate without dereferencing it.
        source = (SAMPLE/'island_checks.h').read_text()
        self.assertIn('crux_island_range_valid', source, 'missing overflow-safe extent predicate')
        code = '#include "island_checks.h"\n#include <assert.h>\nint main(void) {'
        code += 'assert(crux_island_range_valid(0,16,0,0x20000,4)); return 0;}\n'
        with tempfile.TemporaryDirectory() as tmp:
            src=Path(tmp)/'range.c';exe=Path(tmp)/'range';src.write_text(code)
            subprocess.run(['cc','-std=c17','-Wall','-Wextra','-Wpedantic','-Werror',
                            '-I'+str(SAMPLE),str(src),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)


if __name__ == '__main__':
    unittest.main()
