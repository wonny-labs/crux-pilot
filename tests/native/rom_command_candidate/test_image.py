"""Final ARM closure for the retained, unexecuted target command candidate."""
import importlib.util
import os
from pathlib import Path
import re
import subprocess
import unittest
ROOT=Path(__file__).resolve().parents[3]
@unittest.skipUnless(os.environ.get('PILOT_ELF'),'requires exact final ARM image')
class CommandImage(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        spec=importlib.util.spec_from_file_location('image',ROOT/'tests/native/flash_target_candidate/assess_image.py')
        cls.image=importlib.util.module_from_spec(spec);spec.loader.exec_module(cls.image)
        cls.elf=Path(os.environ['PILOT_ELF']).read_bytes();cls.symbols=cls.image.symbols(cls.elf)
        cls.disassembly=subprocess.check_output([os.environ['ARM_OBJDUMP'],'-d',os.environ['PILOT_ELF']],text=True)
        cls.bodies={m[1]:m[2] for m in re.finditer(r'^[0-9a-f]+ <([^>]+)>:\n(.*?)(?=^[0-9a-f]+ <|\Z)',cls.disassembly,re.M|re.S)}
    def test_command_state_and_consumed_metadata_are_internal_ram(self):
        for name in ['crux_rom_command','crux_rom_nor','crux_rom_memc_data','crux_rom_memc_config','crux_rom_deadline','crux_rom_identity','crux_rom_slot','crux_rom_expected_lut','crux_rom_id_lut']:
            self.assertIn(name,self.symbols)
            self.assertGreaterEqual(self.symbols[name],0x20000000)
            self.assertLess(self.symbols[name],0x20020000)
        self.assertLess(self.symbols['crux_rom_command_prepare'],0x20000)
    def test_dispatch_call_and_literal_closure(self):
        pending=['crux_rom_command_dispatch'];seen=set()
        forbidden=['set_device_config','update_clock','probe','sfdp','SetFlashConfig','SystemReset']
        while pending:
            name=pending.pop()
            if name in seen:continue
            seen.add(name)
            self.assertTrue(name in self.bodies,"missing function body: "+name)
            self.assertLess(self.symbols[name]&~1,0x20000,name)
            self.assertFalse(any(token in name for token in forbidden),name)
            body=self.bodies[name]
            self.assertNotRegex(body,r'\bv(?:push|pop|ldr|str|mov|add|sub|mul|div)')
            self.assertNotRegex(body,r'\bblx\s+(?:r\d+|ip|sb|sl|fp)|\bbx\s+(?:r(?:[0-9]|1[0-2])|ip|sb|sl|fp)\b|\b(?:ldr|mov)\s+pc,')
            addresses=[int(x,16) for x in re.findall(r'^\s*([0-9a-f]+):',body,re.M)]
            for address,target in re.findall(r'\b(?:bl|b(?:eq|ne|cs|cc|mi|pl|vs|vc|hi|ls|ge|lt|gt|le)?(?:\.[nw])?|cbz|cbnz)\s+(?:r\d+,\s*)?([0-9a-f]+)\s+<([^>]+)>',body):
                address=int(address,16)
                if min(addresses)<=address<=max(addresses):
                    continue  # objdump may label internal offsets with absolute CONFIG symbols.
                self.assertLess(address,0x20000,(name,target))
                matches=[key for key,value in self.bodies.items()
                         if int(re.search(r'^\s*([0-9a-f]+):',value,re.M)[1],16)==address]
                self.assertEqual(len(matches),1,(name,target))
                pending.append(matches[0])
            for value in re.findall(r'\.word\s+0x([0-9a-f]+)',body):
                self.assertFalse(0x60000000<=int(value,16)<0x60800000,(name,value))
        # Conservative static bound: sum every reachable function frame, even
        # mutually exclusive paths. No recursion or dynamic stack allocation.
        frames={}
        for name in seen:
            body=self.bodies[name]
            frame=sum(4*len(regs.split(',')) for regs in re.findall(
                r'\b(?:push|stmdb)\s+(?:sp!,\s*)?\{([^}]+)\}',body))
            frame+=sum(map(int,re.findall(r'\bsub(?:\.w)?\s+sp,(?:\s*sp,)?\s*#(\d+)',body)))
            self.assertNotRegex(body,r'\bsub(?:\.w)?\s+sp,(?:\s*sp,)?\s*r\d+')
            frames[name]=frame
        self.assertLessEqual(sum(frames.values()),1024)
        if os.environ.get('COMMAND_CLOSURE_JSON'):
            import json
            Path(os.environ['COMMAND_CLOSURE_JSON']).write_text(json.dumps({
                'functions':{name:{'address':self.symbols[name], 'frame_upper_bytes':frames[name]}
                             for name in sorted(seen)},
                'all_frames_conservative_sum':sum(frames.values()),
                'operation_stack_bytes':1024,
                'scope':'static software call/literal/stack closure; no hardware execution'},indent=2)+'\n')
        self.assertIn('FLEXSPI_UpdateLUT',seen)
        self.assertIn('crux_rom_identity_check',seen)
        self.assertIn('crux_rom_identity_restore',seen)
        self.assertIn('memc_flexspi_transfer',seen)
        self.assertIn('FLEXSPI_TransferBlocking',seen)
        self.assertIn('FLEXSPI_SoftwareReset',seen)
        self.assertIn('crux_flash_poc_terminal',seen)
        self.assertIn('crux_rom_elapsed_expired',seen)
        self.assertIn('crux_rom_candidate_wait_check',seen)
        self.assertIn('crux_rom_candidate_deadline_expired',seen)
    def test_context_calls_dispatch_after_ram_setup_and_terminals_on_error(self):
        body=self.bodies['crux_island_enter']
        calls=re.findall(r'\bbl\s+[^<]*<([^>]+)>',body)
        self.assertEqual(calls,['crux_rom_command_dispatch'])
        self.assertLess(body.lower().index('msr\tpsp'),body.index('<crux_rom_command_dispatch>'))
        after=body[body.index('<crux_rom_command_dispatch>'):]
        self.assertRegex(after,r'cmp\s+r0, #0[\s\S]*bne[^\n]*<crux_island_exception_terminal>')
        self.assertRegex(after,r'mov\s+ip, sl[\s\S]*msr\s+PRIMASK, r0[\s\S]*mov\s+r0, ip')
    def test_target_gate_has_no_command_or_timer_calls(self):
        gate=self.bodies['crux_rom_command_submit']
        self.assertNotRegex(gate,r'\b(?:bl|blx|str|ldr)\b')
        self.assertRegex(gate,r'mvn\.w\s+r0, #12')
        elapsed=self.bodies['crux_rom_elapsed_expired']
        self.assertNotRegex(elapsed,r'\b(?:bl|blx|str|ldr)\b')
        self.assertRegex(elapsed,r'movs\s+r0, #1')

    def test_timer_candidate_has_no_startup_or_dispatch_configuration_caller(self):
        for name in ['crux_rom_gpt2_candidate_init','crux_rom_candidate_deadline_begin','crux_rom_candidate_deadline_finish']:
            self.assertIn(name,self.bodies)
            self.assertLess(self.symbols[name]&~1,0x20000,name)
            for caller, body in self.bodies.items():
                self.assertNotRegex(body, r'\b(?:bl|b(?:\.[nw])?)\s+[^<]*<' + name + r'>',caller)
        for name in ['memc_flexspi_wait_bus_idle','FLEXSPI_SoftwareReset',
                     'FLEXSPI_WriteBlocking','FLEXSPI_ReadBlocking','FLEXSPI_TransferBlocking','FLEXSPI_UpdateLUT']:
            self.assertIn('crux_rom_candidate_wait_check',self.bodies[name],name)
        import json
        commands=json.loads((Path(os.environ['PILOT_ELF']).parents[1]/'compile_commands.json').read_text())
        self.assertFalse(any(Path(item['file']).name=='fsl_gpt.c' for item in commands))
        config=(Path(os.environ['PILOT_ELF']).parent/'.config').read_text()
        self.assertIn('# CONFIG_COUNTER is not set',config)
        self.assertIn('# CONFIG_CORTEX_M_DWT is not set',config)
        self.assertIn('CONFIG_CORTEX_M_SYSTICK=y',config)

    def test_full_inherited_table_matches_exact_pinned_fcb(self):
        import struct
        original=Path(os.environ['PILOT_ORIGINAL_FCB']).read_bytes()
        symbol=self.symbols['crux_rom_expected_lut']
        expected=struct.pack('<64I',*[0x0a1804eb if i==0 else 0x26043206 if i==1 else 0x24040405 if i==4 else 0x406 if i==12 else 0x08180420 if i==20 else 0x081804d8 if i==32 else 0x08180402 if i==36 else 0x2004 if i==37 else 0x460 if i==44 else 0 for i in range(64)])
        self.assertEqual(original[0x80:0x180],expected)
        header=struct.unpack_from('<16sHHIIIIIHHHHHH',self.elf)
        for i in range(header[10]):
            kind,offset,vma,_,length,_,_,_=struct.unpack_from('<8I',self.elf,header[5]+i*header[9])
            if kind==1 and vma<=symbol and symbol+256<=vma+length:
                self.assertEqual(self.elf[offset+symbol-vma:offset+symbol-vma+256],expected)
                return
        self.fail('expected table must be backed by final ELF DTCM relocation bytes')
